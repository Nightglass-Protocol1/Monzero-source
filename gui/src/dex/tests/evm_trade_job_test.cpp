// EvmTradeJob against the real monzero-dex-evm-helper and a copy of a real trade folder:
//   evm_trade_job_test <helper> <trade-folder-copy> <work-dir> [other-trade-journal]
#include "../EvmTradeJob.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QJsonDocument>
#include <QTimer>
#include <cstdio>

namespace {
int failures=0;
void check(bool ok,const char *what) {std::printf("%s %s\n",ok?"ok  ":"FAIL",what);if (!ok) ++failures;}
QString sha256(const QString &path) {
    QFile f(path);f.open(QIODevice::ReadOnly);return QString::fromLatin1(QCryptographicHash::hash(f.readAll(),QCryptographicHash::Sha256).toHex());
}
bool wait(EvmTradeJob &job) {
    if (!job.status().value("busy").toBool()) return true;
    QEventLoop loop;QTimer timer;timer.setSingleShot(true);
    QObject::connect(&job,&EvmTradeJob::changed,&loop,[&]{if (!job.status().value("busy").toBool()) loop.quit();});
    QObject::connect(&timer,&QTimer::timeout,&loop,&QEventLoop::quit);timer.start(60000);loop.exec();
    return !job.status().value("busy").toBool();
}
QString writeConfig(const QString &dir,const QString &name,QJsonObject config) {
    const auto path=dir+"/"+name;QFile f(path);
    f.open(QIODevice::WriteOnly|QIODevice::Truncate);f.write(QJsonDocument(config).toJson(QJsonDocument::Compact));f.close();
    f.setPermissions(QFileDevice::ReadOwner|QFileDevice::WriteOwner);return path;
}
}

int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);
    if (argc!=4 && argc!=5) {std::fputs("usage: evm_trade_job_test <helper> <trade-folder-copy> <work-dir> [other-trade-journal]\n",stderr);return 2;}
    const QString helper=QString::fromLocal8Bit(argv[1]),trade=QString::fromLocal8Bit(argv[2]),work=QString::fromLocal8Bit(argv[3]);
    const QJsonObject wallet{{"url","http://127.0.0.1:18083"}};
    QJsonObject config{{"schema","monzero-private-xmz-evm-trade-v1"},{"helper",helper},{"helperDigest",sha256(helper)},
        {"tradeDir",trade},{"evmRpc","https://bsc-testnet-rpc.publicnode.com"},{"accountIndex",2},{"mainWallet",wallet},{"swapWallet",wallet}};

    EvmTradeJob wrongDigest;auto bad=config;bad["helperDigest"]=QString(64,'0');
    check(!wrongDigest.configure(writeConfig(work,"bad-digest.json",bad)),"a helper with another digest is refused");
    EvmTradeJob extraKey;auto extra=config;extra["seed"]="00";
    check(!extraKey.configure(writeConfig(work,"extra.json",extra)),"unknown config keys are refused");
    EvmTradeJob remote;auto r=config;r["mainWallet"]=QJsonObject{{"url","http://203.0.113.5:18083"}};
    check(!remote.configure(writeConfig(work,"remote.json",r)),"a wallet RPC that is not local is refused");

    EvmTradeJob job;
    check(job.configure(writeConfig(work,"trade.json",config)),"configures with the pinned helper");
    check(!job.approve(QString("0x")+QString(64,'1')),"no approval before the user has seen a status");
    check(job.refresh() && wait(job),"status runs");
    auto s=job.status();const auto snap=s.value("snapshot").toMap();
    check(s.value("state")=="ok","status answered");
    check(snap.value("stage")=="Done(Swapped)" && snap.value("role").toString().size()>0,"shows stage and role");
    check(s.value("commitment").toString().startsWith("0x") && s.value("tradeId").toString().size()==66,"pins trade and commitment");
    check(!QJsonDocument::fromVariant(snap).toJson().contains("theirFrame"),"no frames in the snapshot");
    check(!job.approve(QString("0x")+QString(64,'2')),"approval for another commitment is refused without running the helper");
    check(job.approve(s.value("commitment").toString()) && wait(job) && job.status().value("state")=="ok"
        && job.status().value("snapshot").toMap().value("approved").toBool(),"approval for the shown commitment");
    // Approval clicked while a step runs: queued, then sent when it ends
    {
        EvmTradeJob queued;auto c2=config;c2["tradeDir"]=QString::fromLocal8Bit(argv[2])+"-queued";
        QDir().mkpath(c2["tradeDir"].toString());
        QFile::copy(trade+"/journal.json.fresh",c2["tradeDir"].toString()+"/journal.json");
        check(queued.configure(writeConfig(work,"queued.json",c2)) && queued.refresh() && wait(queued),"second job sees a fresh trade");
        const auto shown=queued.status().value("commitment").toString();
        check(!queued.status().value("snapshot").toMap().value("approved").toBool(),"fresh trade starts unapproved");
        check(queued.refresh() && queued.status().value("busy").toBool() && queued.approve(shown)
            && queued.status().value("approvalPending").toBool() && !queued.status().value("canApprove").toBool(),"approval during a step is queued");
        bool done=false;for (int i=0;i<4 && !done;++i) {wait(queued);done=queued.status().value("snapshot").toMap().value("approved").toBool();}
        check(done && !queued.status().value("approvalPending").toBool(),"queued approval is sent after the step");
    }
    // A step that fails (here: a seed that is not this trade's) keeps its error and refreshes
    // the shown stage from the journal
    QByteArray otherSeed(64,'\x07');
    check(job.tick(otherSeed) && otherSeed.isEmpty(),"a step runs and the seed is wiped");
    wait(job);QCoreApplication::processEvents();wait(job);   // the failed step, then the refresh it starts
    const auto afterFailure=job.status();
    if (afterFailure.value("stepError").toString().isEmpty() || afterFailure.value("state")!="ok")
        std::printf("     state=%s stepError=%s\n",qPrintable(afterFailure.value("state").toString()),qPrintable(afterFailure.value("stepError").toString()));
    check(afterFailure.value("state")=="ok" && !afterFailure.value("stepError").toString().isEmpty()
        && afterFailure.value("snapshot").toMap().value("stage")=="Done(Swapped)","failed step: error kept, stage refreshed");
    QByteArray shortSeed("short");
    check(!job.tick(shortSeed) && shortSeed.isEmpty(),"a malformed seed is refused and wiped");

    // Another trade's journal put in the folder: the job stays pinned to its trade
    if (argc==5) {
        QFile::remove(trade+"/journal.json");QFile::copy(QString::fromLocal8Bit(argv[4]),trade+"/journal.json");
        check(job.refresh() && wait(job) && job.status().value("state")=="mismatch"
            && job.status().value("commitment")==s.value("commitment"),"answers for another trade are refused");
    }

    // The config file may not change under the job
    QFile f(work+"/trade.json");f.setPermissions(QFileDevice::ReadOwner|QFileDevice::WriteOwner);
    f.open(QIODevice::Append);f.write(" ");f.close();
    check(!job.refresh() && job.status().value("state")=="source-changed","a changed config stops the job");

    std::printf("%s (%d failures)\n",failures?"FAILED":"passed",failures);
    return failures?1:0;
}
