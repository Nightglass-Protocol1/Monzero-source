// Test-only process driver: production worker and real helper keep all validation.
#include "../BchRecoveryRetentionJob.h"
#include <QCoreApplication>
#include <QEventLoop>
#include <QFile>
#include <QJsonDocument>
#include <functional>
#include <cstdio>
namespace {
bool waitFor(BchRecoveryRetentionJob &job,const std::function<bool()> &start) {
    QEventLoop loop;QTimer deadline;bool expired=false;deadline.setSingleShot(true);
    QObject::connect(&job,&BchRecoveryRetentionJob::changed,&loop,[&]{if (!job.status().value("busy").toBool()) loop.quit();});
    QObject::connect(&deadline,&QTimer::timeout,&loop,[&]{expired=true;loop.quit();});
    if (!start()) return false;
    deadline.start(190000);if (job.status().value("busy").toBool()) loop.exec();
    return !expired && !job.status().value("busy").toBool();
}
bool run(const QStringList &args) {
    if (args.size()!=3 || (args[1]!="--accept" && args[1]!="--status" && args[1]!="--reject-review")) return false;
    BchRecoveryRetentionJob job;
    if (!job.configure(args[2]) || job.review() || !waitFor(job,[&]{return job.refresh();})) return false;
    if (args[1]=="--reject-review") {
        if (job.status().value("state")!="not-saved"
            || !waitFor(job,[&]{return job.review();}) || job.status().value("state")!="unknown"
            || !job.status().value("recovery").toMap().isEmpty()
            || !job.status().value("review").toMap().isEmpty()
            || job.status().value("submitAvailable").toBool() || job.submit(QString(64,'0'))) return false;
        QFile output;if (!output.open(stdout,QIODevice::WriteOnly)) return false;
        const QByteArray result="{\"rejected\":true}";
        return output.write(result)==result.size() && output.flush();
    }
    if (args[1]=="--accept") {
        if (job.status().value("state")!="not-saved"
            || !waitFor(job,[&]{return job.review();}) || job.status().value("state")!="reviewed") return false;
        const auto commitment=job.status().value("review").toMap().value("commitment").toString();
        if (commitment.size()!=64 || job.submit(QString(64,'0'))
            || !waitFor(job,[&]{return job.submit(commitment);})
            || job.submit(commitment) || job.review()) return false;
    }
    if (job.status().value("state")!="saved" || job.status().value("recovery").toMap().isEmpty()
        || job.status().value("executable").toBool() || job.status().value("chainObserved").toBool()) return false;
    const auto encoded=QJsonDocument::fromVariant(job.status()).toJson(QJsonDocument::Compact);
    QFile output;if (!output.open(stdout,QIODevice::WriteOnly)) return false;
    return output.write(encoded)==encoded.size() && output.flush();
}
}
int main(int argc,char **argv) {
    QCoreApplication application(argc,argv);
    if (run(application.arguments())) return 0;
    std::fputs("BCH recovery retention Qt fixture failed\n",stderr);return 1;
}
