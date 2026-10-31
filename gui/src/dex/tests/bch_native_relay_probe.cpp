// Test-only adapter: the production worker and real helper retain all validation.
#include "../BchNativeRelayJob.h"
#include "../BchAdmissionProtocol.h"
#include <QCoreApplication>
#include <QEventLoop>
#include <QFile>
#include <QJsonDocument>
#include <functional>
#include <cstdio>
namespace {
bool waitFor(BchNativeRelayJob &job,const std::function<bool()> &start) {
    QEventLoop loop;QTimer deadline;bool expired=false;deadline.setSingleShot(true);
    QObject::connect(&job,&BchNativeRelayJob::changed,&loop,[&]{if (!job.status().value("busy").toBool()) loop.quit();});
    QObject::connect(&deadline,&QTimer::timeout,&loop,[&]{expired=true;loop.quit();});
    if (!start()) return false;
    deadline.start(190000);if (job.status().value("busy").toBool()) loop.exec();
    return !expired && !job.status().value("busy").toBool() && job.status().value("checked").toBool();
}
bool run(const QStringList &args) {
    if (args.size()!=3) return false;
    QString action;
    for (const auto &candidate:{QString("review"),QString("submit"),QString("status")})
        if (args[1]=="--native-relay-"+candidate+"-stdio-v1") action=candidate;
    if (action.isEmpty()) return false;
    QFile input;if (!input.open(stdin,QIODevice::ReadOnly)) return false;
    const auto bytes=input.read(513);QJsonObject request;
    if (bytes.size()>512 || !BchAdmissionProtocol::canonicalObject(bytes,&request)
        || request.size()!=(action=="submit"?3:2)
        || request.value("schema")!="monzero-bch-native-relay-"+action+"-request-v1"
        || QUuid(request.value("requestId").toString()).isNull()) return false;
    BchNativeRelayJob job;
    if (!job.configure(args[2]) || job.review() || !waitFor(job,[&]{return job.refresh();})) return false;
    if (action!="status") {
        if (!waitFor(job,[&]{return job.review();}) || job.status().value("state")!="reviewed") return false;
        if (action=="submit") {
            const auto commitment=request.value("commitment").toString();
            if (!waitFor(job,[&]{return job.submit(commitment);}) || job.status().value("state")!="acknowledged"
                || job.submit(commitment) || job.review()) return false;
        }
    }
    const auto state=job.status();
    if (state.value("executable").toBool() || state.value("chainObserved").toBool()) return false;
    const auto review=QJsonObject::fromVariantMap(state.value("review").toMap());
    const auto preparation=QJsonObject::fromVariantMap(state.value("preparation").toMap());
    const auto preparedReview=QJsonObject::fromVariantMap(state.value("preparationReview").toMap());
    const auto relay=QJsonObject::fromVariantMap(state.value("relay").toMap());
    const QJsonObject response{{"schema","monzero-bch-native-relay-"+action+"-response-v1"},
        {"requestId",request.value("requestId")},{"tradeId",state.value("tradeId").toString()},
        {"executable",false},{"chainObserved",false},
        {"review",review.isEmpty()?QJsonValue(QJsonValue::Null):QJsonValue(review)},
        {"preparation",preparation.isEmpty()?QJsonValue(QJsonValue::Null):QJsonValue(preparation)},
        {"preparationReview",preparedReview},{"relay",relay.isEmpty()?QJsonValue(QJsonValue::Null):QJsonValue(relay)}};
    const auto encoded=QJsonDocument(response).toJson(QJsonDocument::Compact);
    QFile output;if (!output.open(stdout,QIODevice::WriteOnly)) return false;
    return output.write(encoded)==encoded.size() && output.flush();
}
}
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);
    if (run(app.arguments())) return 0;
    std::fputs("BCH native relay Qt fixture failed\n",stderr);return 1;
}
