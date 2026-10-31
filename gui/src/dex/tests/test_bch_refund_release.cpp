#include "../BchRefundReleaseJob.h"
#include "BchRefundReleaseTestFixture.h"
namespace {
struct JobFixture:RefundContext {
    QString wrapper,submission;
    bool create() {
        if (!RefundContext::create()) return false;
        const QByteArray program=R"PY(#!/usr/bin/python3
import json,pathlib,sys,time
compact=lambda x:json.dumps(x,sort_keys=True,separators=(',',':')).encode()
root=pathlib.Path(sys.argv[2]).parent;r=json.load(sys.stdin);action=r['schema'].split('-')[-3]
mode=(root/'mode').read_text();review=json.loads((root/'review.json').read_text())
with (root/'calls').open('a') as f:f.write(action+'\n')
if action=='submit':
 if r['commitment']!=review['commitment']:sys.exit(1)
 (root/'relay-state').write_text('attempted' if mode=='lost' else 'acknowledged')
 if mode=='lost':sys.exit(1)
state=(root/'relay-state').read_text() if (root/'relay-state').exists() else 'not-attempted'
txid=review['details']['txid']
obs=dict(state='confirmed',txid=txid,block='cc'*32,tip='dd'*32,height='10',confirmations='2',observedAt=str(int(time.time()))) if action=='observe' else None
v=dict(schema='monzero-bch-refund-'+action+'-response-v1',requestId=r['requestId'],tradeId=review['details']['swapId'],executable=False,chainObserved=action=='observe',review=review,relay=dict(state=state,txid=txid,confirmed=False),observation=obs)
if mode=='request':v['requestId']='00000000-0000-4000-8000-000000000001'
if mode=='extra':v['privateRefund']=[1,2,3]
if mode=='confirmed':v['relay']['confirmed']=True
if mode=='txid':v['relay']['txid']='ee'*32
if mode=='stderr':sys.stderr.write('unexpected')
if mode=='stale' and obs:obs['observedAt']='1'
sys.stdout.buffer.write(compact(v))
)PY";
        if (!save(helper,program,true)) return false;
        config["helperDigest"]=QString::fromLatin1(QCryptographicHash::hash(program,QCryptographicHash::Sha256).toHex());
        const QString genesis="0f9188f13cb7b2c71f2a335e3a4fc328bf5beb436012afca590b1a11466e2206";
        auto terms=record.value("terms").toObject();terms["bchGenesis"]=genesis;record["terms"]=terms;
        auto policy=config.value("policy").toObject();policy["bch_genesis"]=genesis;config["policy"]=policy;
        QByteArray binding="monzero-native-bch-xmz-quote-v1";binding.append('\0');binding+=QJsonDocument(terms).toJson(QJsonDocument::Compact);
        const auto quote=QCryptographicHash::hash(binding,QCryptographicHash::Sha256);
        config["quoteCommitment"]=QString::fromLatin1(quote.toHex());record["commitment"]=config.value("quoteCommitment");
        auto bytes=[](const QByteArray &b){QJsonArray a;for (auto c:b) a.append(int(static_cast<unsigned char>(c)));return a;};
        const auto rpcPath=directory.filePath("rpc.json");
        const QJsonObject rpc{{"schema","monzero-private-bch-funding-rpc-v1"},{"port",12345},{"wallet","test"},
            {"anchor","bchreg:qanchor"},{"anchorScript","51"},{"cookieFile",directory.filePath("cookie")},
            {"checkpointHeight",1},{"checkpoint",QString(64,'7')},{"subversion","/Bitcoin Cash Node:29.0.0/"}};
        const QJsonObject context{{"schema","monzero-bch-wallet-deployment-v1"},{"host","127.0.0.1"},{"port",12345},
            {"network","bch-regtest"},{"genesis",genesis},{"checkpointHeight",1},{"checkpoint",QString(64,'7')},
            {"wallet","test"},{"anchor","bchreg:qanchor"},{"script","51"},{"subversion","/Bitcoin Cash Node:29.0.0/"}};
        const auto pin=QCryptographicHash::hash(QJsonDocument(context).toJson(QJsonDocument::Compact),QCryptographicHash::Sha256);
        deployment=QString::fromLatin1(pin.toHex());details["bchDeployment"]=bytes(pin);details["quote"]=bytes(quote);details["bchGenesis"]=genesis;
        wrapper=directory.filePath("refund.json");submission=directory.filePath("submission.json");
        auto jsonSave=[](const QString &p,const QJsonObject &o){return save(p,QJsonDocument(o).toJson(QJsonDocument::Compact));};
        return writeConfig() && jsonSave(config.value("quoteFile").toString(),record) && jsonSave(directory.filePath("review.json"),bound(details))
            && jsonSave(rpcPath,rpc) && jsonSave(wrapper,{{"schema","monzero-private-bch-refund-v1"},{"admissionConfigFile",path},{"deploymentCommitment",deployment},{"safetyBlocks",5}})
            && jsonSave(submission,{{"schema","monzero-private-bch-refund-submission-v1"},{"refundConfigFile",wrapper},{"rpcFile",rpcPath}});
    }
};
}
#define WAIT_JOB(job) QTRY_VERIFY_WITH_TIMEOUT(!(job).status().value("busy").toBool(),10000)
class BchRefundReleaseTests:public QObject {
    Q_OBJECT
private slots:
    void explicitApprovalAndConfirmation() {
        JobFixture f;QVERIFY(f.create());BchRefundReleaseJob job;QVERIFY(job.configureSubmission(f.submission));
        QVERIFY(!job.review());QVERIFY(read(f.log).isEmpty());QVERIFY(job.refresh());WAIT_JOB(job);
        QVERIFY(job.review());WAIT_JOB(job);const auto approval=job.status().value("review").toMap().value("commitment").toString();
        QVERIFY(!job.submit(QString(64,'0')));QVERIFY(job.submit(approval));WAIT_JOB(job);
        QCOMPARE(job.status().value("relayState").toString(),QString("acknowledged"));QVERIFY(!job.status().value("chainObserved").toBool());
        QVERIFY(!job.submit(approval));QVERIFY(job.observe());WAIT_JOB(job);QVERIFY(job.status().value("chainObserved").toBool());
        QVERIFY(save(f.mode,"stale"));QVERIFY(job.observe());WAIT_JOB(job);QVERIFY(!job.status().value("chainObserved").toBool());
        QVERIFY(!job.status().value("submitAvailable").toBool());
    }
    void lostReplyCannotResend() {
        JobFixture f;QVERIFY(f.create());BchRefundReleaseJob job;QVERIFY(job.configureSubmission(f.submission));
        QVERIFY(job.refresh());WAIT_JOB(job);QVERIFY(job.review());WAIT_JOB(job);
        const auto approval=job.status().value("review").toMap().value("commitment").toString();
        QVERIFY(save(f.mode,"lost"));QVERIFY(job.submit(approval));WAIT_JOB(job);
        QCOMPARE(job.status().value("state").toString(),QString("unknown"));QVERIFY(!job.submit(approval));
        QVERIFY(save(f.mode,"normal"));QVERIFY(job.refresh());WAIT_JOB(job);
        QCOMPARE(job.status().value("relayState").toString(),QString("attempted"));QVERIFY(!job.review());
        BchRefundReleaseJob restarted;QVERIFY(restarted.configureSubmission(f.submission));QVERIFY(restarted.refresh());WAIT_JOB(restarted);
        QVERIFY(!restarted.review());QVERIFY(!restarted.submit(approval));
    }
    void invalidResponse_data() {
        QTest::addColumn<QString>("mode");for (const auto m:{"request","extra","confirmed","txid","stderr"}) QTest::newRow(m)<<QString(m);
    }
    void invalidResponse() {
        QFETCH(QString,mode);JobFixture f;QVERIFY(f.create());BchRefundReleaseJob job;QVERIFY(job.configure(f.wrapper));
        QVERIFY(save(f.mode,mode.toUtf8()));QVERIFY(job.refresh());WAIT_JOB(job);
        QCOMPARE(job.status().value("state").toString(),QString("unknown"));QVERIFY(!job.review());
    }
};
QTEST_GUILESS_MAIN(BchRefundReleaseTests)
#include "test_bch_refund_release.moc"
