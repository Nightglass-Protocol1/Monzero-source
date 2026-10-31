// Test-only process adapter around the production refund job.
#include "../BchRefundReleaseJob.h"
#include <QCoreApplication>
#include <QEventLoop>
#include <QFile>
#include <QJsonDocument>
#include <QUuid>
#include <functional>
#include <cstdio>
namespace {
bool waitFor(BchRefundReleaseJob &job,const std::function<bool()> &start) {
    QEventLoop loop;QTimer timeout;bool expired=false;timeout.setSingleShot(true);
    QObject::connect(&job,&BchRefundReleaseJob::changed,&loop,[&]{if (!job.status().value("busy").toBool()) loop.quit();});
    QObject::connect(&timeout,&QTimer::timeout,&loop,[&]{expired=true;loop.quit();});
    if (!start()) return false;
    timeout.start(190000);if (job.status().value("busy").toBool()) loop.exec();
    return !expired && !job.status().value("busy").toBool();
}
bool run(const QStringList &args) {
    if (args.size()!=3) return false;
    QString action;
    for (const auto &candidate:QStringList{"review","submit","status","observe"})
        if (args[1]=="--refund-"+candidate+"-stdio-v1") action=candidate;
    if (action.isEmpty()) return false;
    QFile input;if (!input.open(stdin,QIODevice::ReadOnly)) return false;
    const auto bytes=input.read(513);const auto document=QJsonDocument::fromJson(bytes);
    if (bytes.size()>512 || !document.isObject() || document.toJson(QJsonDocument::Compact)!=bytes) return false;
    const auto request=document.object();const auto id=request.value("requestId").toString();
    if (request.size()!=(action=="submit"?3:2) || QUuid(id).isNull()
        || request.value("schema")!="monzero-bch-refund-"+action+"-request-v1") return false;
    BchRefundReleaseJob job;QJsonObject response;
    if (action=="submit") {
        if (!request.value("commitment").isString() || !job.configureSubmission(args[2])
            || !waitFor(job,[&]{return job.refresh();}) || job.status().value("relayState")!="not-attempted"
            || !waitFor(job,[&]{return job.review();}) || !job.status().value("submitAvailable").toBool()
            || !waitFor(job,[&]{return job.submit(request.value("commitment").toString());})
            || job.status().value("state")!="acknowledged") return false;
        response=QJsonObject::fromVariantMap(job.status().value("result").toMap());
    } else if (action=="observe") {
        if (!job.configureSubmission(args[2]) || !waitFor(job,[&]{return job.observe();})
            || !job.status().value("chainObserved").toBool()) return false;
        response=QJsonObject::fromVariantMap(job.status().value("result").toMap());
    } else {
        if (!job.configure(args[2])) return false;
        if (action=="status") {
            if (!waitFor(job,[&]{return job.refresh();}) || !job.status().value("statusKnown").toBool()) return false;
            response=QJsonObject::fromVariantMap(job.status().value("result").toMap());
        } else {
            if (!waitFor(job,[&]{return job.refresh();}) || !waitFor(job,[&]{return job.review();}) || job.status().value("state")!="reviewed") return false;
            response=QJsonObject::fromVariantMap(job.status().value("result").toMap());
        }
    }
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
