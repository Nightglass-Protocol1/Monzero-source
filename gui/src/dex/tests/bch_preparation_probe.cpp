// Test-only process boundary around the production preparation job.
#include "../BchPreparationJob.h"
#include "../BchAdmissionProtocol.h"
#include "../LitecoinJournalProtocol.h"
#include <QCoreApplication>
#include <QEventLoop>
#include <QFile>
#include <cstdio>
#include <functional>
namespace {
bool waitFor(BchPreparationJob &job,const std::function<bool()> &start) {
    QEventLoop loop;QTimer deadline;deadline.setSingleShot(true);bool expired=false;
    QObject::connect(&job,&BchPreparationJob::changed,&loop,[&]{if (!job.status().value("busy").toBool()) loop.quit();});
    QObject::connect(&deadline,&QTimer::timeout,&loop,[&]{expired=true;loop.quit();});
    if (!start()) return false;
    deadline.start(190000);if (job.status().value("busy").toBool()) loop.exec();
    return !expired && !job.status().value("busy").toBool();
}
bool run(const QStringList &args) {
    if (args.size()!=3) return false;
    QString action;
    if (args[1]=="--funding-prepare-stdio-v1") action="prepare";
    if (args[1]=="--funding-preparation-status-stdio-v1") action="preparation-status";
    for (const auto &candidate:QStringList{"lock-review","lock-status","lock-submit","signing-review","signing-status","signing-submit","signing-recovery-review","signing-recovery-submit"})
        if (args[1]=="--funding-"+candidate+"-stdio-v1") action=candidate;
    if (action.isEmpty()) return false;
    QFile input;if (!input.open(stdin,QIODevice::ReadOnly)) return false;
    QJsonObject request;const auto bytes=input.read(513);
    if (bytes.size()>512 || !BchAdmissionProtocol::canonicalObject(bytes,&request) || request.size()!=(action.endsWith("-submit")?3:2)
        || request.value("schema")!="monzero-bch-funding-"+action+"-request-v1"
        || QUuid(request.value("requestId").toString()).isNull()) return false;
    BchPreparationJob job;
    if (action.startsWith("signing-recovery-")) {
        QJsonObject config;
        if (!BchAdmissionProtocol::canonicalObject(LitecoinJournalProtocol::privateConfig(args[2]),&config)
            || !job.configure(config.value("preparationConfigFile").toString()) || !job.configureSigningRecovery(args[2])) return false;
    } else if (!job.configure(args[2])) return false;
    if (action=="signing-recovery-submit") {
        if (!request.value("commitment").isString() || !waitFor(job,[&]{return job.reviewSigningRecovery();})
            || !waitFor(job,[&]{return job.importSigningRecovery(request.value("commitment").toString());})
            || job.status().value("state")!="signing-recovered") return false;
    } else if (action=="signing-submit") {
        if (!request.value("commitment").isString() || !waitFor(job,[&]{return job.reviewSigning();})
            || !waitFor(job,[&]{return job.sign(request.value("commitment").toString());})
            || job.status().value("state")!="signed") return false;
    } else if (action=="lock-submit") {
        if (!request.value("commitment").isString() || !waitFor(job,[&]{return job.reviewLocks();})
            || !waitFor(job,[&]{return job.lockInputs(request.value("commitment").toString());})
            || job.status().value("state")!="locks-checked") return false;
    } else {
        if (!waitFor(job,[&]{return action=="signing-recovery-review"?job.reviewSigningRecovery():action=="signing-review"?job.reviewSigning():action=="signing-status"?job.checkSigning():action=="prepare"?job.prepare():action=="lock-review"?job.reviewLocks():
            action=="lock-status"?job.checkLocks():job.refresh();})) return false;
        const QString expected=action=="signing-recovery-review"?"recovery-reviewed":action=="signing-review"?"signing-reviewed":action=="signing-status"?"signing-status":action=="lock-review"?"lock-reviewed":action=="lock-status"?"lock-status":"prepared";
        if (job.status().value("state")!=expected) return false;
    }
    auto response=QJsonObject::fromVariantMap(job.status().value("result").toMap());
    response["requestId"]=request.value("requestId");
    QFile output;if (!output.open(stdout,QIODevice::WriteOnly)) return false;
    const auto encoded=QJsonDocument(response).toJson(QJsonDocument::Compact);
    return output.write(encoded)==encoded.size() && output.flush();
}
}
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);
    if (run(app.arguments())) return 0;
    std::fputs("BCH admission request failed; inspect retained state before retrying\n",stderr);return 1;
}
