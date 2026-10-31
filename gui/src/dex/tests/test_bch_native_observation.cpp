#include "BchCancellationTestFixture.h"
#include "../BchNativeObservationJob.h"
class BchNativeObservationTests:public QObject {
    Q_OBJECT
    Context c;QJsonObject context;QString wrapper;
    bool setMode(const QByteArray &mode) {return save(c.mode,mode);}
private slots:
    void init() {
        QVERIFY(c.create());
        QFile::remove(c.directory.filePath("retained"));
        const QByteArray program=R"PY(#!/usr/bin/python3
import json,pathlib,sys,time
request=json.load(sys.stdin)
assert set(request)=={'schema','requestId'}
assert request['schema']=='monzero-bch-native-observation-request-v1'
assert sys.argv[1]=='--native-observation-stdio-v1'
w=json.loads(pathlib.Path(sys.argv[2]).read_text());c=json.loads(pathlib.Path(w['admissionConfigFile']).read_text())
root=pathlib.Path(sys.argv[2]).parent;mode=(root/'mode').read_text()
with (root/'calls').open('a') as f:f.write('observe\n')
if mode=='failure':sys.exit(1)
review=json.loads((root/'review.json').read_text());d=review['details']
observed={'state':'confirmed','txid':d['txid'],'destination':d['destination'],'amountAtoms':d['amountAtoms'],
 'genesis':d['genesis'],'tip':'c'*64,'height':'8','tipHeight':'10','confirmations':'3','observedAt':str(int(time.time()))}
value={'schema':'monzero-bch-native-observation-response-v1','requestId':request['requestId'],
 'tradeId':c['tradeId'],'executable':False,'chainObserved':True,'review':review,'observation':observed}
if mode=='stale':observed['observedAt']='1'
if mode=='future':observed['observedAt']=str(int(time.time())+3600)
if mode=='amount':observed['amountAtoms']='2'
if mode=='depth':observed['confirmations']='99'
if mode=='tip':observed['tip']='0'*64
if mode=='nonce':value['requestId']='00000000-0000-4000-8000-000000000001'
if mode=='secret':value['tx_key']='private'
if mode=='authority':value['executable']=True
if mode=='stderr':sys.stderr.write('unexpected')
if mode=='oversized':sys.stdout.write(' '*16385);sys.exit(0)
sys.stdout.write(json.dumps(value,sort_keys=True,separators=(',',':')))
)PY";
        QVERIFY(save(c.helper,program,true));
        c.config["helperDigest"]=QString::fromLatin1(QCryptographicHash::hash(program,QCryptographicHash::Sha256).toHex());
        QVERIFY(c.writeConfig());
        QVERIFY(save(c.config.value("quoteFile").toString(),QJsonDocument(c.record).toJson(QJsonDocument::Compact)));
        context={{"bchFundingOutpoint",QString(64,'a')+":0"},{"destination",QString(95,'B')},
            {"admission",QString(64,'1')},{"agreement",QString(64,'4')}};
        QVERIFY(save(c.directory.filePath("context.json"),QJsonDocument(context).toJson(QJsonDocument::Compact)));
        QVERIFY(save(c.log,""));
        QJsonObject details{{"schema","monzero-bch-native-proof-review-v1"},{"action","retain-native-payment-proof"},
            {"swapId",c.config.value("tradeId")},{"quote",c.details.value("quote")},
            {"agreement",pinBytes(0x44)},{"admission",pinBytes(0x11)},
            {"bchFundingOutpoint",context.value("bchFundingOutpoint")},
            {"genesis",c.record.value("terms").toObject().value("xmzGenesis")},
            {"destination",context.value("destination")},{"amountAtoms","1"},{"txid",QString(64,'b')},
            {"message",pinBytes(2)},{"signature",pinBytes(3)}};
        QVERIFY(save(c.directory.filePath("review.json"),QJsonDocument(bound(details)).toJson(QJsonDocument::Compact)));
        wrapper=c.directory.filePath("proof.json");
        QVERIFY(save(wrapper,QJsonDocument(QJsonObject{{"schema","monzero-private-bch-native-observation-v1"},
            {"admissionConfigFile",c.path},{"nativeRpcFile",c.directory.filePath("absent-rpc.json")}}).toJson(QJsonDocument::Compact)));
    }
    void explicitCheckAndFailureClearEvidence() {
        BchNativeObservationJob job;QVERIFY(job.configure(wrapper));QCOMPARE(read(c.log),QByteArray());
        QVERIFY(!job.status().value("chainObserved").toBool());QVERIFY(job.check(context));QVERIFY(!job.check(context));
        QVERIFY(job.status().value("observation").toMap().isEmpty());QTRY_VERIFY(!job.status().value("busy").toBool());
        QCOMPARE(job.status().value("state").toString(),QString("confirmed"));QVERIFY(job.status().value("chainObserved").toBool());
        QVERIFY(!job.status().value("executable").toBool());QVERIFY(setMode("failure"));QVERIFY(job.check(context));
        QVERIFY(job.status().value("observation").toMap().isEmpty());QVERIFY(!job.status().value("chainObserved").toBool());
        QTRY_VERIFY(!job.status().value("busy").toBool());QCOMPARE(job.status().value("state").toString(),QString("unknown"));
        QVERIFY(job.status().value("review").toMap().isEmpty());
        QVERIFY(setMode("normal"));QVERIFY(job.check(context));QTRY_VERIFY(!job.status().value("busy").toBool());
        QCOMPARE(job.status().value("state").toString(),QString("confirmed"));
    }
    void rejectsEvidence_data() {
        QTest::addColumn<QByteArray>("mode");
        for (const auto mode:{"stale","future","amount","depth","tip","nonce","secret","authority","stderr","oversized"})
            QTest::newRow(mode)<<QByteArray(mode);
    }
    void rejectsEvidence() {
        QFETCH(QByteArray,mode);QVERIFY(setMode(mode));BchNativeObservationJob job;QVERIFY(job.configure(wrapper));QVERIFY(job.check(context));
        QTRY_VERIFY(!job.status().value("busy").toBool());QCOMPARE(job.status().value("state").toString(),QString("unknown"));
        QVERIFY(job.status().value("observation").toMap().isEmpty());QVERIFY(!job.status().value("chainObserved").toBool());
    }
    void changedConfigurationClearsEvidence() {
        BchNativeObservationJob job;QVERIFY(job.configure(wrapper));QVERIFY(job.check(context));QTRY_VERIFY(!job.status().value("busy").toBool());
        QCOMPARE(job.status().value("state").toString(),QString("confirmed"));QVERIFY(save(c.path,"{}"));QVERIFY(!job.check(context));
        QVERIFY(job.status().value("observation").toMap().isEmpty());QVERIFY(!job.status().value("chainObserved").toBool());
    }
    void requiresRestoredContext() {
        BchNativeObservationJob job;QVERIFY(job.configure(wrapper));QVERIFY(!job.check({}));QCOMPARE(read(c.log),QByteArray());
    }
};
QTEST_GUILESS_MAIN(BchNativeObservationTests)
#include "test_bch_native_observation.moc"
