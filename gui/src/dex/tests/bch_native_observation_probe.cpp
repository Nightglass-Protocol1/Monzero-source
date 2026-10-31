// Test-only adapter around the production observation worker and pinned helper.
#include "../BchNativeObservationJob.h"
#include "../BchAdmissionProtocol.h"
#include "../LitecoinJournalProtocol.h"
#include <QCoreApplication>
#include <QEventLoop>
#include <QFile>
#include <cstdio>
namespace {
bool run(const QStringList &args) {
    if (args.size()!=3 || args[1]!="--native-observation-stdio-v1") return false;
    QFile input;if (!input.open(stdin,QIODevice::ReadOnly)) return false;
    const auto bytes=input.read(513);QJsonObject request,context;
    if (bytes.size()>512 || !BchAdmissionProtocol::canonicalObject(bytes,&request) || request.size()!=2
        || request.value("schema")!="monzero-bch-native-observation-request-v1"
        || QUuid(request.value("requestId").toString()).isNull()
        || !BchAdmissionProtocol::canonicalObject(LitecoinJournalProtocol::privateConfig(args[2]+".public-context.json"),&context)) return false;
    BchNativeObservationJob job;if (!job.configure(args[2])) return false;
    QEventLoop loop;QTimer timer;bool expired=false;timer.setSingleShot(true);
    QObject::connect(&job,&BchNativeObservationJob::changed,&loop,[&]{if (!job.status().value("busy").toBool()) loop.quit();});
    QObject::connect(&timer,&QTimer::timeout,&loop,[&]{expired=true;loop.quit();});
    if (!job.check(context)) return false;
    timer.start(190000);if (job.status().value("busy").toBool()) loop.exec();
    const auto status=job.status();
    if (expired || status.value("busy").toBool() || status.value("state")!="confirmed"
        || !status.value("chainObserved").toBool() || status.value("executable").toBool()) return false;
    const QJsonObject response{{"schema","monzero-bch-native-observation-response-v1"},
        {"requestId",request.value("requestId")},{"tradeId",status.value("tradeId").toString()},
        {"executable",false},{"chainObserved",true},
        {"review",QJsonObject::fromVariantMap(status.value("review").toMap())},
        {"observation",QJsonObject::fromVariantMap(status.value("observation").toMap())}};
    const auto encoded=QJsonDocument(response).toJson(QJsonDocument::Compact);
    QFile output;if (!output.open(stdout,QIODevice::WriteOnly)) return false;
    return output.write(encoded)==encoded.size() && output.flush();
}
}
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);if (run(app.arguments())) return 0;
    std::fputs("BCH native observation Qt fixture failed\n",stderr);return 1;
}
