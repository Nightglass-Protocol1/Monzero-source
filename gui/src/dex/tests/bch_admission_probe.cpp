// Test-only bridge for the Rust retained-negotiation fixture. No wallet manager.
#include "../BchAdmissionJob.h"
#include <QCoreApplication>
#include <QEventLoop>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <QUuid>
#include <functional>
#include <cstdio>

namespace {
bool waitForOperation(BchAdmissionJob &job,const std::function<bool()> &start) {
    QEventLoop loop;QTimer deadline;bool timedOut=false;
    deadline.setSingleShot(true);
    QObject::connect(&job,&BchAdmissionJob::changed,&loop,[&]{
        if (!job.status().value("busy").toBool()) loop.quit();
    });
    QObject::connect(&deadline,&QTimer::timeout,&loop,[&]{timedOut=true;loop.quit();});
    if (!start()) return false;
    deadline.start(190000);
    if (job.status().value("busy").toBool()) loop.exec();
    return !timedOut && !job.status().value("busy").toBool();
}
bool run(const QStringList &args) {
    if (args.size()!=3) return false;
    QString action;
    for (const auto &candidate:QStringList{"review","submit","status"})
        if (args[1]=="--admission-"+candidate+"-stdio-v1") action=candidate;
    if (action.isEmpty()) return false;
    QFile input;
    if (!input.open(stdin,QIODevice::ReadOnly)) return false;
    const auto bytes=input.read(513);const auto document=QJsonDocument::fromJson(bytes);
    if (bytes.size()>512 || !document.isObject() || document.toJson(QJsonDocument::Compact)!=bytes) return false;
    const auto request=document.object();const auto id=request.value("requestId").toString();
    if (QUuid(id).isNull() || request.size()!=(action=="submit"?3:2)
        || request.value("schema")!="monzero-bch-admission-"+action+"-request-v1") return false;
    BchAdmissionJob job;
    if (!job.configure(args[2])) return false;
    QJsonObject response;
    if (action=="status") {
        if (!waitForOperation(job,[&]{return job.refresh();}) || job.status().value("state")!="admitted") return false;
        response=QJsonObject::fromVariantMap(job.status().value("admission").toMap());
    } else {
        if (!waitForOperation(job,[&]{return job.review();}) || job.status().value("state")!="reviewed") return false;
        if (action=="submit") {
            if (!request.value("commitment").isString()
                || !waitForOperation(job,[&]{return job.submit(request.value("commitment").toString());})
                || job.status().value("state")!="admitted") return false;
            response=QJsonObject::fromVariantMap(job.status().value("admission").toMap());
        } else {
            response={{"schema","monzero-bch-admission-review-response-v1"},
                {"tradeId",job.status().value("tradeId").toString()},{"admitted",false},{"executable",false},
                {"review",QJsonObject::fromVariantMap(job.status().value("review").toMap())}};
        }
    }
    // The job has already correlated and verified the inner helper response.
    // This test adapter echoes the outer fixture's separate request identifier.
    response["requestId"]=id;
    QFile output;
    if (!output.open(stdout,QIODevice::WriteOnly)) return false;
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
