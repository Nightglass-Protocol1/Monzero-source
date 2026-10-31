// Test-only adapter around the production signature/release review worker and pinned helper.
#include "../BchSuccessReleaseReviewJob.h"
#include "../BchAdmissionProtocol.h"
#include "../LitecoinJournalProtocol.h"
#include <QCoreApplication>
#include <QEventLoop>
#include <QFile>
#include <cstdio>
namespace {
bool run(const QStringList &args) {
    if (args.size()!=3) return false;
    using Action=BchSuccessReleaseProtocol::Action;
    Action action;QString operation;
    if (args[1]=="--success-signature-review-stdio-v1") {action=Action::RetainSignature;operation="signature";}
    else if (args[1]=="--success-signed-release-review-stdio-v1") {action=Action::ReleaseSigned;operation="signed-release";}
    else return false;
    QFile input;if (!input.open(stdin,QIODevice::ReadOnly)) return false;
    const auto bytes=input.read(513);QJsonObject request,context,proof;
    if (bytes.size()>512 || !BchAdmissionProtocol::canonicalObject(bytes,&request) || request.size()!=2
        || request.value("schema")!=QString("monzero-bch-success-%1-review-request-v1").arg(operation)
        || QUuid(request.value("requestId").toString()).isNull()
        || !BchAdmissionProtocol::canonicalObject(LitecoinJournalProtocol::privateConfig(args[2]+".public-context.json"),&context)
        || !BchAdmissionProtocol::canonicalObject(LitecoinJournalProtocol::privateConfig(args[2]+".public-proof.json"),&proof)) return false;
    BchSuccessReleaseReviewJob job;if (!job.configure(args[2],action)) return false;
    QEventLoop loop;QTimer timer;bool expired=false;timer.setSingleShot(true);
    QObject::connect(&job,&BchSuccessReleaseReviewJob::changed,&loop,[&]{if (!job.status().value("busy").toBool()) loop.quit();});
    QObject::connect(&timer,&QTimer::timeout,&loop,[&]{expired=true;loop.quit();});
    if (!job.review(proof,context)) return false;
    timer.start(190000);if (job.status().value("busy").toBool()) loop.exec();
    const auto status=job.status();
    if (expired || status.value("busy").toBool() || status.value("state")!="reviewed"
        || status.value("chainObserved").toBool() || status.value("executable").toBool()) return false;
    const QJsonObject response{{"schema",BchSuccessReleaseProtocol::responseSchema(action)},
        {"requestId",request.value("requestId")},{"tradeId",status.value("tradeId").toString()},
        {"executable",false},{"chainObserved",false},
        {"review",QJsonObject::fromVariantMap(status.value("review").toMap())}};
    const auto encoded=QJsonDocument(response).toJson(QJsonDocument::Compact);
    QFile output;if (!output.open(stdout,QIODevice::WriteOnly)) return false;
    return output.write(encoded)==encoded.size() && output.flush();
}
}
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);if (run(app.arguments())) return 0;
    std::fputs("BCH success signing review Qt fixture failed\n",stderr);return 1;
}
