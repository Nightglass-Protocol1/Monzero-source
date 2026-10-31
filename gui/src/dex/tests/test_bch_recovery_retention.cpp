#include "../BchRecoveryRetentionJob.h"
#include "BchAdmissionTestFixture.h"
namespace {
struct RecoveryFixture:Fixture {
    bool create() {
        if (!Fixture::create()) return false;
        const QByteArray program=R"PY(#!/usr/bin/python3
import hashlib,json,pathlib,sys,time
compact=lambda x:json.dumps(x,sort_keys=True,separators=(',',':')).encode()
c=json.loads(pathlib.Path(sys.argv[2]).read_text());root=pathlib.Path(sys.argv[2]).parent
mode=(root/'mode').read_text();r=json.load(sys.stdin);action=r['schema'].split('-')[-3]
with (root/'calls').open('a') as f:f.write(action+'\n')
d=dict(schema='monzero-bch-recovery-retention-review-v1',action='retain-private-recovery-only',swapId=c['tradeId'],quote=c['quoteCommitment'],admission='aa'*32,funding='bb'*32,receipt='cc'*32,fundingTxid='dd'*32,cancelTxid='11'*32,refundTxid='22'*32,punishTxid='33'*32)
if mode=='trade':d['swapId']='00000000-0000-4000-8000-000000000001'
if mode=='quote':d['quote']='00'*32
if mode=='private':d['refundBytes']=[1,2,3]
review=dict(details=d,commitment=hashlib.sha256(d['schema'].encode()+b'\0'+compact(d)).hexdigest())
if action=='submit':
 if r['commitment']!=review['commitment']:sys.exit(1)
 (root/'saved').write_text(str(int(time.time())))
 if mode=='lost':sys.exit(1)
saved=dict(commitment='44'*32,preparedAt=(root/'saved').read_text(),cancelTxid='11'*32,refundTxid='22'*32,punishTxid='33'*32) if (root/'saved').exists() else None
if mode=='mismatch' and saved:saved['refundTxid']='55'*32
if mode=='future' and saved:saved['preparedAt']=str(int(time.time())+3600)
v=dict(schema='monzero-bch-recovery-retention-'+action+'-response-v1',requestId=r['requestId'],tradeId=c['tradeId'],executable=False,chainObserved=False,review=None if action=='status' else review,recovery=saved)
sys.stdout.buffer.write(compact(v))
)PY";
        if (!save(helper,program,true)) return false;
        config["role"]="bch-funder";
        config["helperDigest"]=QString::fromLatin1(QCryptographicHash::hash(program,QCryptographicHash::Sha256).toHex());
        return writeConfig();
    }
};
}
#define WAIT_JOB(job) QTRY_VERIFY_WITH_TIMEOUT(!(job).status().value("busy").toBool(),10000)
class BchRecoveryRetentionTests:public QObject {
    Q_OBJECT
private slots:
    void approvalAndRestart() {
        RecoveryFixture f;QVERIFY(f.create());BchRecoveryRetentionJob job;QVERIFY(job.configure(f.path));
        QVERIFY(!job.review());QVERIFY(job.refresh());WAIT_JOB(job);QVERIFY(job.review());WAIT_JOB(job);
        const auto approval=job.status().value("review").toMap().value("commitment").toString();
        QVERIFY(!job.submit(QString(64,'0')));QVERIFY(job.submit(approval));WAIT_JOB(job);
        QCOMPARE(job.status().value("state").toString(),QString("saved"));
        QVERIFY(!job.submit(approval));QVERIFY(!job.review());
        const auto receipt=job.status().value("recovery").toMap();
        BchRecoveryRetentionJob restarted;QVERIFY(restarted.configure(f.path));QVERIFY(restarted.refresh());WAIT_JOB(restarted);
        QCOMPARE(restarted.status().value("recovery").toMap(),receipt);
    }
    void reviewBinding_data() {
        QTest::addColumn<QString>("mode");for (const auto mode:{"trade","quote","private"}) QTest::newRow(mode)<<QString(mode);
    }
    void reviewBinding() {
        QFETCH(QString,mode);RecoveryFixture f;QVERIFY(f.create());BchRecoveryRetentionJob job;QVERIFY(job.configure(f.path));
        QVERIFY(job.refresh());WAIT_JOB(job);QVERIFY(save(f.mode,mode.toUtf8()));QVERIFY(job.review());WAIT_JOB(job);
        QCOMPARE(job.status().value("state").toString(),QString("unknown"));QVERIFY(!job.status().value("submitAvailable").toBool());
    }
    void uncertainAcceptance_data() {
        QTest::addColumn<QString>("mode");for (const auto mode:{"lost","mismatch","future"}) QTest::newRow(mode)<<QString(mode);
    }
    void uncertainAcceptance() {
        QFETCH(QString,mode);RecoveryFixture f;QVERIFY(f.create());BchRecoveryRetentionJob job;QVERIFY(job.configure(f.path));
        QVERIFY(job.refresh());WAIT_JOB(job);QVERIFY(job.review());WAIT_JOB(job);
        const auto approval=job.status().value("review").toMap().value("commitment").toString();
        QVERIFY(save(f.mode,mode.toUtf8()));QVERIFY(job.submit(approval));WAIT_JOB(job);
        QCOMPARE(job.status().value("state").toString(),QString("unknown"));QVERIFY(!job.submit(approval));QVERIFY(!job.review());
        QVERIFY(save(f.mode,"normal"));QVERIFY(job.refresh());WAIT_JOB(job);
        QCOMPARE(job.status().value("state").toString(),QString("saved"));
        QCOMPARE(read(f.log),QByteArray("status\nreview\nsubmit\nstatus\n"));
    }
};
QTEST_GUILESS_MAIN(BchRecoveryRetentionTests)
#include "test_bch_recovery_retention.moc"
