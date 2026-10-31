// Test-only process boundary around the production Qt worker and pinned helper.
#include "../BchPublicationRepairJob.h"
#include "../BchPublicationRepairProtocol.h"
#include <QCoreApplication>
#include <QEventLoop>
#include <QFile>
#include <cstdio>
namespace {
bool run(const QStringList &args) {
    if (args.size()!=3) return false;
    const bool submit=args[1]=="--success-publication-repair-submit-stdio-v1";
    if (!submit && args[1]!="--success-publication-repair-review-stdio-v1") return false;
    QFile input;if (!input.open(stdin,QIODevice::ReadOnly)) return false;
    const auto bytes=input.read(513);QJsonObject request;
    const QString action=submit?"submit":"review";
    if (bytes.size()>512 || !BchAdmissionProtocol::canonicalObject(bytes,&request)
        || request.size()!=(submit?4:3)
        || request.value("schema")!=QString("monzero-bch-success-publication-repair-%1-request-v1").arg(action)
        || !BchPublicationRepairProtocol::stage(request.value("stage").toString())) return false;
    const auto id=QUuid(request.value("requestId").toString());
    if (id.isNull() || id.toString(QUuid::WithoutBraces)!=request.value("requestId").toString()
        || (submit && !BchAdmissionProtocol::hex(request.value("commitment").toString(),64))) return false;
    BchPublicationRepairJob job;if (!job.configure(args[2])) return false;
    QEventLoop loop;QTimer timer;timer.setSingleShot(true);bool expired=false;
    QObject::connect(&job,&BchPublicationRepairJob::changed,&loop,[&]{if (!job.status().value("busy").toBool()) loop.quit();});
    QObject::connect(&timer,&QTimer::timeout,&loop,[&]{expired=true;loop.quit();});
    const auto wait=[&](const QString &state) {
        expired=false;timer.start(190000);
        if (job.status().value("busy").toBool()) loop.exec();
        timer.stop();return !expired && !job.status().value("busy").toBool() && job.status().value("state")==state;
    };
    // A new Qt process must independently review even when the fixture supplies
    // earlier approval. The worker must never accept a bare submit on reopen.
    if (!job.review(request.value("stage").toString()) || !wait("reviewed")) return false;
    if (submit && (!job.submit(request.value("commitment").toString()) || !wait("repaired"))) return false;
    const auto status=job.status();
    if (status.value("executable").toBool() || status.value("chainObserved").toBool()
        || status.value("releasesPrivateMaterial").toBool()) return false;
    const QJsonObject result{{"schema",QString("monzero-bch-success-publication-repair-%1-response-v1").arg(action)},
        {"requestId",request.value("requestId")},{"tradeId",status.value("tradeId").toString()},
        {"executable",false},{"chainObserved",false},{"releasesPrivateMaterial",false},
        {"state",submit?"repaired":"reviewed"},{"review",QJsonObject::fromVariantMap(status.value("review").toMap())}};
    const auto encoded=QJsonDocument(result).toJson(QJsonDocument::Compact);
    QFile output;if (!output.open(stdout,QIODevice::WriteOnly)) return false;
    return output.write(encoded)==encoded.size() && output.flush();
}
}
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);if (run(app.arguments())) return 0;
    std::fputs("BCH publication repair Qt fixture failed\n",stderr);return 1;
}
