// Test-only adapter around production saved-status and fresh-review operations.
#include "../BchRefundWalletJob.h"
#include <QCoreApplication>
#include <QEventLoop>
#include <QFile>
#include <QJsonDocument>
#include <QUuid>
#include <QFileInfo>
#include <QTimer>
#include "../LitecoinJournalProtocol.h"
#include <cstdio>
namespace {
bool run(const QStringList &args) {
    if (args.size()!=3) return false;
    const bool reviewing=args[1]=="--native-refund-wallet-review-stdio-v1";
    if (!reviewing && args[1]!="--native-refund-wallet-status-stdio-v1") return false;
    QFile input;if (!input.open(stdin,QIODevice::ReadOnly)) return false;
    const auto bytes=input.read(513);const auto document=QJsonDocument::fromJson(bytes);
    if (bytes.size()>512 || !document.isObject() || document.toJson(QJsonDocument::Compact)!=bytes) return false;
    const auto request=document.object();const auto id=request.value("requestId").toString();
    if (request.size()!=(reviewing?3:2) || QUuid(id).isNull()
        || request.value("schema")!=(reviewing?"monzero-bch-native-refund-wallet-review-request-v1":"monzero-bch-native-refund-wallet-status-request-v1")) return false;
    const auto action=request.value("action").toString();
    if (reviewing && action!="restore" && action!="open" && action!="check") return false;
    const auto outer=QJsonDocument::fromJson(LitecoinJournalProtocol::privateConfig(args[2])).object();
    const auto statusPath=reviewing?outer.value("statusConfigFile").toString():args[2];
    const auto wrapper=QJsonDocument::fromJson(LitecoinJournalProtocol::privateConfig(statusPath)).object();
    const auto admission=QJsonDocument::fromJson(LitecoinJournalProtocol::privateConfig(wrapper.value("admissionConfigFile").toString())).object();
    if (QFileInfo(admission.value("helper").toString()).canonicalFilePath()==QCoreApplication::applicationFilePath()) return false;
    BchRefundWalletJob job;
    if (!job.configure(statusPath) || (reviewing && !job.configureReview(args[2]))) return false;
    QEventLoop loop;QTimer timeout;bool expired=false;timeout.setSingleShot(true);
    QObject::connect(&job,&BchRefundWalletJob::changed,&loop,[&]{if (!job.status().value("busy").toBool()) loop.quit();});
    QObject::connect(&timeout,&QTimer::timeout,&loop,[&]{expired=true;loop.quit();});
    if (!job.refresh()) return false;
    timeout.start(190000);if (job.status().value("busy").toBool()) loop.exec();
    if (expired || job.status().value("state")=="unknown") return false;
    if (reviewing) {
        if (!job.requestReview(action)) return false;
        timeout.start(190000);if (job.status().value("busy").toBool()) loop.exec();
        if (job.status().value("state")!="reviewed" || !job.status().value("chainObserved").toBool()) return false;
    }
    const auto status=job.status();
    if (expired || status.value("busy").toBool() || status.value("state")=="unknown") return false;
    auto response=QJsonObject::fromVariantMap(status.value("result").toMap());
    if (response.isEmpty()) return false;
    // The worker validates inner correlation before adapting the fixture request ID.
    response["requestId"]=id;
    QFile output;if (!output.open(stdout,QIODevice::WriteOnly)) return false;
    const auto encoded=QJsonDocument(response).toJson(QJsonDocument::Compact);
    return encoded.size()<=16384 && output.write(encoded)==encoded.size() && output.flush();
}
}
int main(int argc,char **argv) {
    QCoreApplication application(argc,argv);
    if (run(application.arguments())) return 0;
    std::fputs("BCH saved-wallet status request failed\n",stderr);
    return 1;
}
