#include "BchCancellationTestFixture.h"
#include "../BchSuccessPreparationJob.h"
class BchSuccessPreparationTests:public QObject {
    Q_OBJECT
    Context c;QJsonObject details,proof,context,source;QString wrapper;
private slots:
    void init() {
        QVERIFY(c.create());const auto t=c.record.value("terms").toObject();
        context={{"bchFundingOutpoint",QString(64,'a')+":0"},{"destination",QString(95,'B')},
            {"admission",QString(64,'1')},{"agreement",QString(64,'4')}};
        source={{"deploymentCommitment",c.deployment},{"safetyBlocks",5}};
        proof={{"schema","monzero-bch-native-proof-review-v1"},{"action","retain-native-payment-proof"},
            {"swapId",c.config.value("tradeId")},{"quote",c.details.value("quote")},
            {"agreement",pinBytes(0x44)},{"admission",pinBytes(0x11)},
            {"bchFundingOutpoint",context.value("bchFundingOutpoint")},{"genesis",t.value("xmzGenesis")},
            {"destination",context.value("destination")},{"amountAtoms",t.value("xmzAtoms")},
            {"txid",QString(64,'b')},{"message",pinBytes(2)},{"signature",pinBytes(3)}};
        details={{"schema","monzero-bch-success-release-review-v1"},{"action","prepare-bch-success-release"},
            {"swapId",c.config.value("tradeId")},{"quote",proof.value("quote")},
            {"agreement",proof.value("agreement")},{"admission",proof.value("admission")},
            {"nativeProof",pinBytes(9)},{"bchDeployment",pinBytes(0x77)},
            {"bchFundingOutpoint",context.value("bchFundingOutpoint")},{"bchAtoms","100000"},
            {"bchPayoutScript",t.value("bchPayoutScript")},{"bchPayoutAtoms","99000"},{"bchFeeAtoms","1000"},
            {"nativeTxid",proof.value("txid")},{"nativeDestination",proof.value("destination")},
            {"nativeAmountAtoms",proof.value("amountAtoms")},{"minimumBchConfirmations",2},
            {"minimumNativeConfirmations",t.value("xmzConfirmations").toString().toInt()},
            {"cancellationMarginBlocks",5}};

        const QByteArray program=R"PY(#!/usr/bin/python3
import json,pathlib,sys,time
request=json.load(sys.stdin)
status=request['schema']=='monzero-bch-success-preparation-status-request-v1'
assert request['schema'] in ('monzero-bch-success-preparation-request-v1','monzero-bch-success-preparation-status-request-v1')
assert len(request)==(2 if status else 3)
w=json.loads(pathlib.Path(sys.argv[2]).read_text())
c=json.loads(pathlib.Path(w['admissionConfigFile']).read_text())
root=pathlib.Path(sys.argv[2]).parent
mode=(root/'mode').read_text()
with (root/'calls').open('a') as f:f.write(request['schema']+'\n')
if mode=='exit':sys.exit(1)
if mode=='slow':time.sleep(.2)
value={'schema':'monzero-bch-success-preparation-response-v1','requestId':request['requestId'],
 'tradeId':c['tradeId'],'executable':False,'chainObserved':True,'state':'prepared','review':json.loads((root/'release.json').read_text())}
if status:
 value.update(schema='monzero-bch-success-preparation-status-response-v1',chainObserved=False,
  preparation=None if mode=='empty' else {'state':'prepared','preparedAt':str(int(time.time())),'signatureRetained':False})
 del value['state']
else:assert request['commitment']==value['review']['commitment']
if mode=='nonce':value['requestId']='00000000-0000-4000-8000-000000000001'
if mode=='private':value['frame']='private'
if mode=='observed':value['chainObserved']=False
if mode=='stderr':sys.stderr.write('unexpected private diagnostic')
if mode=='oversized':sys.stdout.write(' '*16385);sys.exit(0)
sys.stdout.write(json.dumps(value,sort_keys=True,separators=(',',':')))
)PY";
        QVERIFY(save(c.helper,program,true));
        c.config["helperDigest"]=QString::fromLatin1(QCryptographicHash::hash(program,QCryptographicHash::Sha256).toHex());
        QVERIFY(c.writeConfig());
        QVERIFY(save(c.config.value("quoteFile").toString(),QJsonDocument(c.record).toJson(QJsonDocument::Compact)));
        QVERIFY(save(c.directory.filePath("release.json"),QJsonDocument(bound(details)).toJson(QJsonDocument::Compact)));
        QVERIFY(save(c.log,""));
        wrapper=c.directory.filePath("success.json");
        auto config=source;config["schema"]="monzero-private-bch-success-preparation-v1";config["admissionConfigFile"]=c.path;
        for (const auto key:{"bchRpcFile","nativeRpcFile"}) {
            config[key]=c.directory.filePath(key);QVERIFY(save(config.value(key).toString(),"{}"));
        }
        QVERIFY(save(wrapper,QJsonDocument(config).toJson(QJsonDocument::Compact)));
    }
    void explicitPreparationAndInvalidation() {
        BchSuccessPreparationJob job;QVERIFY(job.configure(wrapper));QCOMPARE(read(c.log),QByteArray());
        QVERIFY(job.submit(bound(details),bound(proof),context,bound(details).value("commitment").toString()));QTRY_VERIFY(!job.status().value("busy").toBool());
        QCOMPARE(job.status().value("state").toString(),QString("prepared"));
        QCOMPARE(QJsonObject::fromVariantMap(job.status().value("review").toMap()),bound(details));
        QVERIFY(job.status().value("chainObserved").toBool());QVERIFY(job.status().value("attempted").toBool());QVERIFY(!job.status().value("executable").toBool());
        job.invalidate();QVERIFY(job.status().value("review").toMap().isEmpty());
        QCOMPARE(job.status().value("state").toString(),QString("unknown"));
    }
    void wrongConsentAndChangedRpcNeverDispatch() {
        BchSuccessPreparationJob job;QVERIFY(job.configure(wrapper));
        QVERIFY(!job.submit(bound(details),bound(proof),context,QString(64,'0')));
        QCOMPARE(read(c.log),QByteArray());QVERIFY(!job.status().value("attempted").toBool());
        QVERIFY(save(c.directory.filePath("bchRpcFile"),"{\"changed\":true}"));
        QVERIFY(!job.submit(bound(details),bound(proof),context,bound(details).value("commitment").toString()));
        QCOMPARE(read(c.log),QByteArray());QVERIFY(!job.status().value("attempted").toBool());
    }
    void statusRestoresOfflineWithoutChainAuthorityOrSubmission() {
        QVERIFY(QFile::remove(c.directory.filePath("bchRpcFile")));QVERIFY(QFile::remove(c.directory.filePath("nativeRpcFile")));
        BchSuccessPreparationJob job;QVERIFY(job.configure(wrapper));QVERIFY(!job.status().value("submitAvailable").toBool());
        QVERIFY(job.refresh(bound(proof),context));QVERIFY(!job.status().value("chainObserved").toBool());
        QTRY_VERIFY(!job.status().value("busy").toBool());
        QCOMPARE(job.status().value("state").toString(),QString("saved"));QVERIFY(job.status().value("statusKnown").toBool());
        QVERIFY(!job.status().value("chainObserved").toBool());QVERIFY(!job.status().value("attempted").toBool());
        QCOMPARE(job.status().value("preparation").toMap().value("signatureRetained").toBool(),false);
        QCOMPARE(read(c.log),QByteArray("monzero-bch-success-preparation-status-request-v1\n"));
        QVERIFY(save(c.mode,"empty"));QVERIFY(job.refresh(bound(proof),context));QTRY_VERIFY(!job.status().value("busy").toBool());
        QCOMPARE(job.status().value("state").toString(),QString("checked"));QVERIFY(job.status().value("preparation").toMap().isEmpty());
        QVERIFY(save(c.mode,"private"));QVERIFY(job.refresh(bound(proof),context));QTRY_VERIFY(!job.status().value("busy").toBool());
        QCOMPARE(job.status().value("state").toString(),QString("unknown"));QVERIFY(!job.status().value("statusKnown").toBool());
        QVERIFY(job.status().value("review").toMap().isEmpty());
    }
    void checkingClearsLiveEvidenceAndAllowsOfflineRecoveryAfterRpcRemoval() {
        BchSuccessPreparationJob job;QVERIFY(job.configure(wrapper));
        QVERIFY(job.submit(bound(details),bound(proof),context,bound(details).value("commitment").toString()));
        QTRY_VERIFY(!job.status().value("busy").toBool());QVERIFY(job.status().value("chainObserved").toBool());
        QVERIFY(QFile::remove(c.directory.filePath("nativeRpcFile")));
        QVERIFY(job.refresh(bound(proof),context));QVERIFY(!job.status().value("chainObserved").toBool());
        QVERIFY(job.status().value("review").toMap().isEmpty());QTRY_VERIFY(!job.status().value("busy").toBool());
        QCOMPARE(job.status().value("state").toString(),QString("saved"));QVERIFY(job.status().value("attempted").toBool());
        const auto calls=read(c.log);
        QVERIFY(!job.submit(bound(details),bound(proof),context,bound(details).value("commitment").toString()));
        QCOMPARE(read(c.log),calls);
    }
    void invalidProofNeverDispatches() {
        BchSuccessPreparationJob job;QVERIFY(job.configure(wrapper));proof["amountAtoms"]="2";
        QVERIFY(!job.submit(bound(details),bound(proof),context,bound(details).value("commitment").toString()));QCOMPARE(read(c.log),QByteArray());
    }
    void cannotOverlapAndClearsReviewBeforeDispatch() {
        BchSuccessPreparationJob job;QVERIFY(job.configure(wrapper));QVERIFY(job.submit(bound(details),bound(proof),context,bound(details).value("commitment").toString()));
        QTRY_VERIFY(!job.status().value("busy").toBool());QVERIFY(save(c.mode,"slow"));
        QVERIFY(job.submit(bound(details),bound(proof),context,bound(details).value("commitment").toString()));QVERIFY(job.status().value("review").toMap().isEmpty());
        QVERIFY(!job.submit(bound(details),bound(proof),context,bound(details).value("commitment").toString()));QTRY_VERIFY(!job.status().value("busy").toBool());
        QCOMPARE(job.status().value("state").toString(),QString("prepared"));
    }
    void rejectsFailedOrInvalidResponse_data() {
        QTest::addColumn<QByteArray>("mode");
        for (const auto value:{"exit","nonce","private","observed","stderr","oversized"}) QTest::newRow(value)<<QByteArray(value);
    }
    void rejectsFailedOrInvalidResponse() {
        QFETCH(QByteArray,mode);BchSuccessPreparationJob job;QVERIFY(job.configure(wrapper));
        QVERIFY(job.submit(bound(details),bound(proof),context,bound(details).value("commitment").toString()));QTRY_VERIFY(!job.status().value("busy").toBool());
        QVERIFY(save(c.mode,mode));QVERIFY(job.submit(bound(details),bound(proof),context,bound(details).value("commitment").toString()));QTRY_VERIFY(!job.status().value("busy").toBool());
        QCOMPARE(job.status().value("state").toString(),QString("unknown"));QVERIFY(job.status().value("attempted").toBool());QVERIFY(job.status().value("review").toMap().isEmpty());
        QVERIFY(!job.status().value("chainObserved").toBool());
    }
    void failedFirstAttemptStaysUnknownWithoutAutomaticRetry() {
        BchSuccessPreparationJob job;QVERIFY(job.configure(wrapper));QVERIFY(save(c.mode,"exit"));
        QVERIFY(job.submit(bound(details),bound(proof),context,bound(details).value("commitment").toString()));
        QTRY_VERIFY(!job.status().value("busy").toBool());
        QCOMPARE(job.status().value("state").toString(),QString("unknown"));
        QVERIFY(job.status().value("attempted").toBool());QVERIFY(!job.status().value("chainObserved").toBool());
        const auto calls=read(c.log);QVERIFY(!calls.isEmpty());QTest::qWait(250);QCOMPARE(read(c.log),calls);
        QVERIFY(save(c.mode,"valid"));
        QVERIFY(job.submit(bound(details),bound(proof),context,bound(details).value("commitment").toString()));
        QTRY_VERIFY(!job.status().value("busy").toBool());
        QCOMPARE(job.status().value("state").toString(),QString("prepared"));
    }
    void invalidationCancelsPendingResponse() {
        BchSuccessPreparationJob job;QVERIFY(job.configure(wrapper));QVERIFY(save(c.mode,"slow"));
        QVERIFY(job.submit(bound(details),bound(proof),context,bound(details).value("commitment").toString()));QVERIFY(job.status().value("busy").toBool());
        job.invalidate();QVERIFY(!job.status().value("busy").toBool());
        QTest::qWait(300);
        QCOMPARE(job.status().value("state").toString(),QString("unknown"));
        QVERIFY(job.status().value("review").toMap().isEmpty());
        QVERIFY(save(c.mode,"valid"));QVERIFY(job.submit(bound(details),bound(proof),context,bound(details).value("commitment").toString()));
        QTRY_VERIFY(!job.status().value("busy").toBool());
        QCOMPARE(job.status().value("state").toString(),QString("prepared"));
    }
    void changedConfigurationStopsDispatch() {
        BchSuccessPreparationJob job;QVERIFY(job.configure(wrapper));QVERIFY(save(c.path,"{}"));
        QVERIFY(!job.submit(bound(details),bound(proof),context,bound(details).value("commitment").toString()));QCOMPARE(read(c.log),QByteArray());
    }
};
QTEST_GUILESS_MAIN(BchSuccessPreparationTests)
#include "test_bch_success_preparation.moc"
