#include "BchCancellationTestFixture.h"
#include "../BchNativeProofJob.h"
class BchNativeProofTests:public QObject {
    Q_OBJECT
    Context c;QJsonObject context;QString wrapper;
    bool setMode(const QByteArray &mode) {return save(c.mode,mode);}
    QString approval(const BchNativeProofJob &job) const {return job.status().value("review").toMap().value("commitment").toString();}
private slots:
    void init() {
        QVERIFY(c.create());
        QFile::remove(c.directory.filePath("retained"));
        const QByteArray program=R"PY(#!/usr/bin/python3
import hashlib,json,pathlib,sys,time
request=json.load(sys.stdin)
w=json.loads(pathlib.Path(sys.argv[2]).read_text())
c=json.loads(pathlib.Path(w['admissionConfigFile']).read_text())
root=pathlib.Path(sys.argv[2]).parent
mode=(root/'mode').read_text()
action=request['schema'].split('-')[-3]
with (root/'calls').open('a') as f:f.write(action+'\n')
if action=='context':
 context=json.loads((root/'context.json').read_text())
 value={'schema':'monzero-bch-native-proof-context-response-v1','requestId':request['requestId'],
  'tradeId':c['tradeId'],'executable':False,'chainObserved':False,'context':context}
 if mode=='context-secret':value['context']['tx_key']='secret'
 if mode=='context-nonce':value['requestId']='00000000-0000-4000-8000-000000000001'
 if mode=='context-observed':value['chainObserved']=True
 sys.stdout.write(json.dumps(value,sort_keys=True,separators=(',',':')));sys.exit(0)
review=json.loads((root/'review.json').read_text())
saved=root/'retained'
compact=lambda v:json.dumps(v,sort_keys=True,separators=(',',':')).encode()
if action=='submit':
 if request['commitment']!=review['commitment']:sys.exit(2)
 saved.write_text(str(int(time.time())))
 if mode=='lost':sys.exit(1)
receipt={'state':'retained','receivedAt':saved.read_text()} if saved.exists() else None
value={'schema':'monzero-bch-native-proof-'+action+'-response-v1','requestId':request['requestId'],
 'tradeId':c['tradeId'],'executable':False,'chainObserved':False,
 'review':review if action!='status' or receipt else None,'receipt':receipt}
if mode=='nonce':value['requestId']='00000000-0000-4000-8000-000000000001'
if mode=='secret':value['tx_key']='private'
if mode=='confirmed':value['chainObserved']=True
if mode=='amount':
 value['review']['details']['amountAtoms']='2'
 value['review']['commitment']=hashlib.sha256(b'monzero-bch-native-proof-review-v1\0'+compact(value['review']['details'])).hexdigest()
if mode=='changed' and action=='submit':
 value['review']['details']['txid']='c'*64
 value['review']['commitment']=hashlib.sha256(b'monzero-bch-native-proof-review-v1\0'+compact(value['review']['details'])).hexdigest()
if mode=='stderr':sys.stderr.write('unexpected')
if mode=='oversized':sys.stdout.write(' '*16385);sys.exit(0)
sys.stdout.buffer.write(compact(value))
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
        QVERIFY(save(wrapper,QJsonDocument(QJsonObject{{"schema","monzero-private-bch-native-proof-v1"},
            {"admissionConfigFile",c.path},{"peerMessageFile",c.directory.filePath("absent-private-peer.json")}}).toJson(QJsonDocument::Compact)));
    }
    void loadsContextBeforeStatus() {
        BchNativeProofJob job;QVERIFY(job.configure(wrapper));QVERIFY(!job.review());QVERIFY(job.refresh());
        QTRY_VERIFY(!job.status().value("busy").toBool());
        QCOMPARE(read(c.log),QByteArray("context\nstatus\n"));
        QCOMPARE(job.status().value("state").toString(),QString("not-saved"));
        QVERIFY(!job.submit(""));QVERIFY(job.review());QTRY_VERIFY(!job.status().value("busy").toBool());
        QCOMPARE(job.status().value("state").toString(),QString("reviewed"));
    }
    void rejectsContextBeforeStatus_data() {
        QTest::addColumn<QByteArray>("mode");
        for (const auto mode:{"context-secret","context-nonce","context-observed"}) QTest::newRow(mode)<<QByteArray(mode);
    }
    void rejectsContextBeforeStatus() {
        QFETCH(QByteArray,mode);QVERIFY(setMode(mode));BchNativeProofJob job;QVERIFY(job.configure(wrapper));QVERIFY(job.refresh());
        QTRY_VERIFY(!job.status().value("busy").toBool());
        QCOMPARE(read(c.log),QByteArray("context\n"));QCOMPARE(job.status().value("state").toString(),QString("unknown"));
        QVERIFY(!job.review());QVERIFY(!job.submit(""));
    }
    void requiresStatusAndExplicitConsent() {
        BchNativeProofJob job;QVERIFY(job.configure(wrapper,context));QVERIFY(!job.review());QVERIFY(!job.submit(""));
        QVERIFY(job.refresh());QTRY_VERIFY(!job.status().value("busy").toBool());QVERIFY(job.review());
        QTRY_VERIFY(!job.status().value("busy").toBool());QCOMPARE(job.status().value("state").toString(),QString("reviewed"));
        QVERIFY(!job.submit(QString(64,'0')));QVERIFY(job.submit(approval(job)));
        QTRY_VERIFY(!job.status().value("busy").toBool());QCOMPARE(job.status().value("state").toString(),QString("saved"));
        QVERIFY(!job.submit(approval(job)));QVERIFY(!job.status().value("chainObserved").toBool());
    }
    void lostReplyAndRestart() {
        BchNativeProofJob job;QVERIFY(job.configure(wrapper,context));QVERIFY(job.refresh());QTRY_VERIFY(!job.status().value("busy").toBool());
        QVERIFY(job.review());QTRY_VERIFY(!job.status().value("busy").toBool());QVERIFY(setMode("lost"));
        QVERIFY(job.submit(approval(job)));QTRY_VERIFY(!job.status().value("busy").toBool());
        QCOMPARE(job.status().value("state").toString(),QString("unknown"));QVERIFY(!job.review());
        QCOMPARE(job.status().value("failureReason").toString(),QString("process-failed"));
        QVERIFY(job.refresh());QTRY_VERIFY(!job.status().value("busy").toBool());
        QCOMPARE(job.status().value("state").toString(),QString("saved"));
        BchNativeProofJob restarted;QVERIFY(restarted.configure(wrapper,context));QVERIFY(restarted.refresh());
        QTRY_VERIFY(!restarted.status().value("busy").toBool());
        QCOMPARE(restarted.status().value("state").toString(),QString("saved"));QVERIFY(!restarted.submit(approval(restarted)));
    }
    void rejectsInvalidResponse_data() {
        QTest::addColumn<QByteArray>("mode");
        for (const auto mode:{"nonce","secret","confirmed","amount","stderr","oversized"}) QTest::newRow(mode)<<QByteArray(mode);
    }
    void rejectsInvalidResponse() {
        QFETCH(QByteArray,mode);BchNativeProofJob job;QVERIFY(job.configure(wrapper,context));QVERIFY(job.refresh());
        QTRY_VERIFY(!job.status().value("busy").toBool());QVERIFY(setMode(mode));QVERIFY(job.review());
        QTRY_VERIFY(!job.status().value("busy").toBool());QCOMPARE(job.status().value("state").toString(),QString("unknown"));
        QVERIFY(job.status().value("review").toMap().isEmpty());QVERIFY(!job.submit(""));
        const QString reason=mode=="stderr"?"unexpected-stderr":mode=="oversized"?"output-limit":"invalid-response";
        QCOMPARE(job.status().value("failureReason").toString(),reason);
        QCOMPARE(job.status().value("operation").toString(),QString("review"));
        QVERIFY(!job.status().value("failureReason").toString().contains("private"));
        QVERIFY(setMode("valid"));QVERIFY(job.refresh());
        QCOMPARE(job.status().value("failureReason").toString(),QString());
        QTRY_VERIFY(!job.status().value("busy").toBool());
        QCOMPARE(job.status().value("state").toString(),QString("not-saved"));
    }
    void changedSubmissionCannotPublishReceipt() {
        BchNativeProofJob job;QVERIFY(job.configure(wrapper,context));QVERIFY(job.refresh());QTRY_VERIFY(!job.status().value("busy").toBool());
        QVERIFY(job.review());QTRY_VERIFY(!job.status().value("busy").toBool());QVERIFY(setMode("changed"));
        QVERIFY(job.submit(approval(job)));QTRY_VERIFY(!job.status().value("busy").toBool());
        QCOMPARE(job.status().value("state").toString(),QString("unknown"));QVERIFY(!job.review());
    }
    void changedConfigStopsDispatch() {
        BchNativeProofJob job;QVERIFY(job.configure(wrapper,context));QVERIFY(save(c.path,"{}"));
        QVERIFY(!job.refresh());QCOMPARE(job.status().value("state").toString(),QString("unknown"));
        QCOMPARE(job.status().value("failureReason").toString(),QString("sources-changed"));
    }
    void rejectsIncompleteContext() {BchNativeProofJob job;context.remove("agreement");QVERIFY(!job.configure(wrapper,context));}
};
QTEST_GUILESS_MAIN(BchNativeProofTests)
#include "test_bch_native_proof.moc"
