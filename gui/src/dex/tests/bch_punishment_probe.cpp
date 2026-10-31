// Test-only process adapter around the production punishment job.
#include "../BchPunishmentJob.h"
#include <QCoreApplication>
#include <QEventLoop>
#include <QFile>
#include <QJsonDocument>
#include <QUuid>
#include <QTemporaryDir>
#include "../LitecoinJournalProtocol.h"
#include "../BchAdmissionProtocol.h"
#include <functional>
#include <cstdio>
namespace {
bool waitFor(BchPunishmentJob &job,const std::function<bool()> &start) {
    QEventLoop loop;QTimer timeout;bool expired=false;timeout.setSingleShot(true);
    QObject::connect(&job,&BchPunishmentJob::changed,&loop,[&]{if (!job.status().value("busy").toBool()) loop.quit();});
    QObject::connect(&timeout,&QTimer::timeout,&loop,[&]{expired=true;loop.quit();});
    if (!start()) return false;
    timeout.start(190000);if (job.status().value("busy").toBool()) loop.exec();
    return !expired && !job.status().value("busy").toBool();
}
bool run(const QStringList &args) {
    if (args.size()!=3) return false;
    QString action;
    for (const auto &candidate:QStringList{"review","status","observe","submit"})
        if (args[1]=="--punish-"+candidate+"-stdio-v1") action=candidate;
    if (action.isEmpty()) return false;
    QFile input;if (!input.open(stdin,QIODevice::ReadOnly)) return false;
    const auto bytes=input.read(513);const auto document=QJsonDocument::fromJson(bytes);
    if (bytes.size()>512 || !document.isObject() || document.toJson(QJsonDocument::Compact)!=bytes) return false;
    const auto request=document.object();const auto id=request.value("requestId").toString();
    if (request.size()!=(action=="submit"?3:2) || QUuid(id).isNull()
        || request.value("schema")!="monzero-bch-punish-"+action+"-request-v1") return false;
    BchPunishmentJob job;QJsonObject response;QTemporaryDir observationDirectory;
    if (action=="submit") {
        const auto commitment=request.value("commitment").toString();
        if (!BchAdmissionProtocol::hex(commitment,64)) return false;
        QJsonObject config;
        if (!BchAdmissionProtocol::canonicalObject(LitecoinJournalProtocol::privateConfig(args[2]),&config)
            || !observationDirectory.isValid()) return false;
        // The submission descriptor contains the same owned-node observation
        // configuration. Keep a private, temporary observation wrapper alive
        // until the production job has finished; never copy credentials.
        const auto observationPath=observationDirectory.path()+"/observation.json";
        const QJsonObject observation{{"schema","monzero-private-bch-punish-observation-v1"},
            {"punishConfigFile",config.value("punishConfigFile")},
            {"rpcFile",config.value("rpcFile")},{"bchObserverKind",config.value("bchObserverKind")}};
        QFile file(observationPath);
        if (!file.open(QIODevice::WriteOnly|QIODevice::NewOnly)
            || !file.setPermissions(QFile::ReadOwner|QFile::WriteOwner)) return false;
        const auto encoded=QJsonDocument(observation).toJson(QJsonDocument::Compact);
        if (file.write(encoded)!=encoded.size() || !file.flush()) return false;
        file.close();
        if (!job.configure(config.value("punishConfigFile").toString())
            || !job.configureObservation(observationPath) || !job.configureSubmission(args[2])
            || !waitFor(job,[&]{return job.review();})
            || !waitFor(job,[&]{return job.submit(commitment);})
            || job.status().value("state")!="acknowledged") return false;
    } else if (action=="observe") {
        QFile file(args[2]);if (!file.open(QIODevice::ReadOnly)) return false;
        const auto config=QJsonDocument::fromJson(file.read(16385)).object();
        if (!job.configure(config.value("punishConfigFile").toString()) || !job.configureObservation(args[2])
            || !waitFor(job,[&]{return job.observe();}) || !job.status().value("chainObserved").toBool()) return false;
    } else if (!job.configure(args[2]) || !waitFor(job,[&]{return action=="review"?job.review():job.refresh();})) return false;
    response=QJsonObject::fromVariantMap(job.status().value("result").toMap());
    if (response.isEmpty() || job.status().value("state")=="unknown") return false;
    // The production job checked the inner helper request id before this adapter
    // correlates the outer fixture request. No validation is bypassed here.
    response["requestId"]=id;
    QFile output;if (!output.open(stdout,QIODevice::WriteOnly)) return false;
    const auto encoded=QJsonDocument(response).toJson(QJsonDocument::Compact);
    return encoded.size()<=16384 && output.write(encoded)==encoded.size() && output.flush();
}
}
int main(int argc,char **argv) {
    QCoreApplication application(argc,argv);
    if (run(application.arguments())) return 0;
    std::fputs("BCH admission request failed; inspect retained state before retrying\n",stderr);
    return 1;
}
