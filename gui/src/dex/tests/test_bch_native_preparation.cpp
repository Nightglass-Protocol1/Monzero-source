#include "../BchNativePreparationJob.h"
#include "BchCancellationTestFixture.h"
namespace {
struct NativeFixture:Context {
    QString wrapper;
    bool create() {
        if (!Context::create(true) || !save(config.value("quoteFile").toString(),QJsonDocument(record).toJson(QJsonDocument::Compact))) return false;
        const QByteArray program=R"PY(#!/usr/bin/python3
import hashlib,json,pathlib,sys,time
compact=lambda x:json.dumps(x,sort_keys=True,separators=(',',':')).encode()
root=pathlib.Path(sys.argv[2]).parent
w=json.loads(pathlib.Path(sys.argv[2]).read_text());c=json.loads(pathlib.Path(w['admissionConfigFile']).read_text())
t=json.loads(pathlib.Path(c['quoteFile']).read_text())['terms'];mode=(root/'mode').read_text()
r=json.load(sys.stdin);action=r['schema'].split('-')[-3]
with (root/'calls').open('a') as f:f.write(action+'\n')
d={'schema':'monzero-bch-native-preparation-review-v1','action':'prepare-bch-native-funding',
'swapId':c['tradeId'],'quote':list(bytes.fromhex(c['quoteCommitment'])),'admission':[1]*32,'receipt':[2]*32,
'deployment':list(bytes.fromhex(w['deploymentCommitment'])),'genesis':t['xmzGenesis'],'wallet':w['wallet'],
'account':w['account'],'priority':w['priority'],'destination':'B'*95,'amountAtoms':t['xmzAtoms'],
'maximumFeeAtoms':t['xmzFundingFeeMaxAtoms'],'safetyBlocks':w['safetyBlocks']}
review={'details':d,'commitment':hashlib.sha256(d['schema'].encode()+b'\0'+compact(d)).hexdigest()}
if action=='submit':
 if r.get('commitment')!=review['commitment']:sys.exit(1)
 (root/'saved').write_text(str(int(time.time())))
 if mode=='lost':sys.exit(1)
prepared={'state':'prepared','attemptedAt':(root/'saved').read_text(),'payment':{'txid':'ab'*32,'feeAtoms':'1','weight':2000}} if (root/'saved').exists() else None
if mode=='attempted':prepared={'state':'attempted','attemptedAt':str(int(time.time())),'payment':None}
if mode=='bad-fee' and prepared:prepared['payment']['feeAtoms']='18446744073709551616'
value={'schema':'monzero-bch-native-preparation-'+action+'-response-v1','requestId':r['requestId'],
 'tradeId':c['tradeId'],'executable':False,'chainObserved':False,'review':review if action!='status' or prepared else None,'preparation':prepared}
if mode=='nonce':value['requestId']='00000000-0000-4000-8000-000000000001'
if mode=='private':value['metadata']='deadbeef'
if mode=='stderr':sys.stderr.write('unexpected')
sys.stdout.buffer.write(compact(value))
)PY";
        if (!save(helper,program,true)) return false;
        config["role"]="native-funder";config["helperDigest"]=QString::fromLatin1(QCryptographicHash::hash(program,QCryptographicHash::Sha256).toHex());
        if (!save(path,QJsonDocument(config).toJson(QJsonDocument::Compact))) return false;
        wrapper=directory.filePath("native-preparation.json");
        return save(wrapper,QJsonDocument(QJsonObject{{"schema","monzero-private-bch-native-preparation-v1"},
            {"admissionConfigFile",path},{"bchRpcFile",directory.filePath("absent-bch.json")},
            {"nativeRpcFile",directory.filePath("absent-native.json")},{"deploymentCommitment",QString(64,'7')},
            {"wallet",QString(95,'A')},{"account",0},{"priority",1},{"safetyBlocks",1}}).toJson(QJsonDocument::Compact));
    }
};
}
#define WAIT_JOB(job) QTRY_VERIFY_WITH_TIMEOUT(!(job).status().value("busy").toBool(),10000)
class BchNativePreparationTests:public QObject {
    Q_OBJECT
private slots:
    void configurationVersions() {
        NativeFixture f;QVERIFY(f.create());
        const auto original=QJsonDocument::fromJson(read(f.wrapper)).object();
        for (int mode=0;mode<14;++mode) {
            auto config=original;
            if (mode!=0) config["schema"]="monzero-private-bch-native-preparation-v2";
            if (mode==1 || mode==5 || mode==6 || mode==7 || mode==8) config["bchObserverKind"]="owned-node";
            if (mode==3) config["bchObserverKind"]=QJsonValue(QJsonValue::Null);
            if (mode==4) config["bchObserverKind"]="node-wallet";
            if (mode==5) config["schema"]="monzero-private-bch-native-preparation-v1";
            if (mode==6) config["schema"]="monzero-private-bch-native-preparation-v3";
            if (mode==7) config["unexpected"]=true;
            if (mode==8) {config.remove("wallet");config["unexpected"]="replacement";}
            if (mode==9) config["bchObserverKind"]=true;
            if (mode==10) config["bchObserverKind"]=1;
            if (mode==11) config["bchObserverKind"]="Owned-Node";
            if (mode==12) config["schema"]=true;
            if (mode==13) {config["schema"]="monzero-private-bch-native-preparation-v1";config["bchObserverKind"]=QJsonValue(QJsonValue::Null);}
            QVERIFY(save(f.wrapper,QJsonDocument(config).toJson(QJsonDocument::Compact)));
            BchNativePreparationJob job;QCOMPARE(job.configure(f.wrapper),mode==0 || mode==1);
            if (mode>1) QVERIFY(!job.refresh());
        }
        QVERIFY(read(f.log).isEmpty());
    }
    void explicitConsentAndRestart_data() {
        QTest::addColumn<bool>("owned");
        QTest::newRow("node-wallet-v1")<<false;
        QTest::newRow("owned-observer-v2")<<true;
    }
    void explicitConsentAndRestart() {
        QFETCH(bool,owned);NativeFixture f;QVERIFY(f.create());
        if (owned) {
            auto config=QJsonDocument::fromJson(read(f.wrapper)).object();
            config["schema"]="monzero-private-bch-native-preparation-v2";config["bchObserverKind"]="owned-node";
            QVERIFY(save(f.wrapper,QJsonDocument(config).toJson(QJsonDocument::Compact)));
        }
        BchNativePreparationJob job;QVERIFY(job.configure(f.wrapper));
        QVERIFY(!job.review());QVERIFY(job.refresh());WAIT_JOB(job);
        QVERIFY(job.review());WAIT_JOB(job);
        const auto approval=job.status().value("review").toMap().value("commitment").toString();QVERIFY(!approval.isEmpty());
        QVERIFY(!job.submit(QString(64,'0')));QVERIFY(job.submit(approval));WAIT_JOB(job);
        QCOMPARE(job.status().value("state").toString(),QString("prepared"));QVERIFY(!job.review());QVERIFY(!job.submit(approval));
        BchNativePreparationJob restarted;QVERIFY(restarted.configure(f.wrapper));QVERIFY(restarted.refresh());WAIT_JOB(restarted);
        QCOMPARE(restarted.status().value("state").toString(),QString("prepared"));QVERIFY(!restarted.review());
        QCOMPARE(read(f.log).count("submit\n"),1);
    }
    void lostReplyRequiresStatus() {
        NativeFixture f;QVERIFY(f.create());BchNativePreparationJob job;QVERIFY(job.configure(f.wrapper));
        QVERIFY(job.refresh());WAIT_JOB(job);QVERIFY(job.review());WAIT_JOB(job);
        auto approval=job.status().value("review").toMap().value("commitment").toString();
        QVERIFY(save(f.mode,"lost"));QVERIFY(job.submit(approval));WAIT_JOB(job);
        QCOMPARE(job.status().value("state").toString(),QString("unknown"));QVERIFY(!job.submit(approval));QVERIFY(!job.review());
        QVERIFY(save(f.mode,"ok"));QVERIFY(job.refresh());WAIT_JOB(job);
        QCOMPARE(job.status().value("state").toString(),QString("prepared"));QCOMPARE(read(f.log).count("submit\n"),1);
    }
    void retainedAttemptBlocksConstruction() {
        NativeFixture f;QVERIFY(f.create());QVERIFY(save(f.mode,"attempted"));BchNativePreparationJob job;
        QVERIFY(job.configure(f.wrapper));QVERIFY(job.refresh());WAIT_JOB(job);
        QCOMPARE(job.status().value("state").toString(),QString("attempted"));QVERIFY(!job.review());
        QVERIFY(!job.status().value("submitAvailable").toBool());
    }
    void malformedResponse_data() {
        QTest::addColumn<QByteArray>("mode");
        for (auto mode:{"nonce","private","stderr"}) QTest::newRow(mode)<<QByteArray(mode);
    }
    void malformedResponse() {
        QFETCH(QByteArray,mode);NativeFixture f;QVERIFY(f.create());QVERIFY(save(f.mode,mode));BchNativePreparationJob job;
        QVERIFY(job.configure(f.wrapper));QVERIFY(job.refresh());WAIT_JOB(job);
        QCOMPARE(job.status().value("state").toString(),QString("unknown"));QVERIFY(!job.review());
    }
    void changedConfigRefusesSubmission_data() {
        QTest::addColumn<int>("change");
        QTest::newRow("priority")<<0;
        QTest::newRow("v1-to-v2")<<1;
        QTest::newRow("v2-to-v1")<<2;
    }
    void changedConfigRefusesSubmission() {
        QFETCH(int,change);NativeFixture f;QVERIFY(f.create());
        auto wrapper=QJsonDocument::fromJson(read(f.wrapper)).object();
        if (change==2) {
            wrapper["schema"]="monzero-private-bch-native-preparation-v2";wrapper["bchObserverKind"]="owned-node";
            QVERIFY(save(f.wrapper,QJsonDocument(wrapper).toJson(QJsonDocument::Compact)));
        }
        BchNativePreparationJob job;QVERIFY(job.configure(f.wrapper));
        QVERIFY(job.refresh());WAIT_JOB(job);QVERIFY(job.review());WAIT_JOB(job);
        auto approval=job.status().value("review").toMap().value("commitment").toString();
        if (change==0) wrapper["priority"]=2;
        if (change==1) {wrapper["schema"]="monzero-private-bch-native-preparation-v2";wrapper["bchObserverKind"]="owned-node";}
        if (change==2) {wrapper["schema"]="monzero-private-bch-native-preparation-v1";wrapper.remove("bchObserverKind");}
        QVERIFY(save(f.wrapper,QJsonDocument(wrapper).toJson(QJsonDocument::Compact)));
        QVERIFY(!job.submit(approval));QCOMPARE(read(f.log).count("submit\n"),0);
    }
};
QTEST_GUILESS_MAIN(BchNativePreparationTests)
#include "test_bch_native_preparation.moc"
