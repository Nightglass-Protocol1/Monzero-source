#include "BchCancellationTestFixture.h"
#include "../BchSuccessReleaseReviewJob.h"
class BchSuccessReleaseReviewTests:public QObject {
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
operation=sys.argv[1][len('--success-'):-len('-review-stdio-v1')]
assert operation in ('release','signature','signed-release')
assert set(request)=={'schema','requestId'}
assert request['schema']==f'monzero-bch-success-{operation}-review-request-v1' 
w=json.loads(pathlib.Path(sys.argv[2]).read_text())
c=json.loads(pathlib.Path(w['admissionConfigFile']).read_text())
root=pathlib.Path(sys.argv[2]).parent
mode=(root/'mode').read_text()
with (root/'calls').open('a') as f:f.write(request['schema']+'\n')
if mode=='exit':sys.exit(1)
if mode=='slow':time.sleep(.2)
value={'schema':f'monzero-bch-success-{operation}-review-response-v1','requestId':request['requestId'],
 'tradeId':c['tradeId'],'executable':False,'chainObserved':False,'review':json.loads((root/'release.json').read_text())}
if mode=='operation':value['schema']='monzero-bch-success-signature-review-response-v1' if operation=='release' else 'monzero-bch-success-release-review-response-v1'
if mode=='nonce':value['requestId']='00000000-0000-4000-8000-000000000001'
if mode=='private':value['frame']='private'
if mode=='observed':value['chainObserved']=True
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
        auto config=source;config["schema"]="monzero-private-bch-success-release-review-v1";config["admissionConfigFile"]=c.path;
        QVERIFY(save(wrapper,QJsonDocument(config).toJson(QJsonDocument::Compact)));
    }
    void explicitReadOnlyReviewAndInvalidation() {
        BchSuccessReleaseReviewJob job;QVERIFY(job.configure(wrapper));QCOMPARE(read(c.log),QByteArray());
        QVERIFY(job.review(bound(proof),context));QTRY_VERIFY(!job.status().value("busy").toBool());
        QCOMPARE(job.status().value("state").toString(),QString("reviewed"));
        QCOMPARE(QJsonObject::fromVariantMap(job.status().value("review").toMap()),bound(details));
        QVERIFY(!job.status().value("chainObserved").toBool());QVERIFY(!job.status().value("executable").toBool());
        job.invalidate();QVERIFY(job.status().value("review").toMap().isEmpty());
        QCOMPARE(job.status().value("state").toString(),QString("unknown"));
    }
    void operationIsFixedAndExplicit() {
        using namespace BchSuccessReleaseProtocol;
        for (const auto action:{Action::RetainSignature,Action::ReleaseSigned}) {
            details["action"]=actionName(action);
            QVERIFY(save(c.directory.filePath("release.json"),QJsonDocument(bound(details)).toJson(QJsonDocument::Compact)));
            BchSuccessReleaseReviewJob job;
            QVERIFY(!job.configure(wrapper,static_cast<Action>(99)));
            QVERIFY(job.configure(wrapper,action));
            QVERIFY(!job.configure(wrapper,Action::Prepare));
            QCOMPARE(job.status().value("action").toString(),actionName(action));
            QVERIFY(job.review(bound(proof),context));QTRY_VERIFY(!job.status().value("busy").toBool());
            QCOMPARE(job.status().value("state").toString(),QString("reviewed"));
            QCOMPARE(QJsonObject::fromVariantMap(job.status().value("review").toMap()),bound(details));
            QVERIFY(save(c.mode,"slow"));QVERIFY(job.review(bound(proof),context));
            QVERIFY(job.status().value("review").toMap().isEmpty());job.invalidate();
            QTest::qWait(300);QVERIFY(job.status().value("review").toMap().isEmpty());
            QCOMPARE(job.status().value("state").toString(),QString("unknown"));
            QVERIFY(save(c.mode,"valid"));
        }
    }
    void invalidProofNeverDispatches() {
        BchSuccessReleaseReviewJob job;QVERIFY(job.configure(wrapper));proof["amountAtoms"]="2";
        QVERIFY(!job.review(bound(proof),context));QCOMPARE(read(c.log),QByteArray());
    }
    void cannotOverlapAndClearsReviewBeforeDispatch() {
        BchSuccessReleaseReviewJob job;QVERIFY(job.configure(wrapper));QVERIFY(job.review(bound(proof),context));
        QTRY_VERIFY(!job.status().value("busy").toBool());QVERIFY(save(c.mode,"slow"));
        QVERIFY(job.review(bound(proof),context));QVERIFY(job.status().value("review").toMap().isEmpty());
        QVERIFY(!job.review(bound(proof),context));QTRY_VERIFY(!job.status().value("busy").toBool());
        QCOMPARE(job.status().value("state").toString(),QString("reviewed"));
    }
    void rejectsFailedOrInvalidResponse_data() {
        QTest::addColumn<QByteArray>("mode");QTest::addColumn<int>("operation");
        for (const auto value:{"exit","nonce","private","observed","stderr","oversized","operation"}) {
            for (int action=0;action<3;++action) {
                const auto label=QByteArray(value)+"-"+QByteArray::number(action);
                QTest::newRow(label.constData())<<QByteArray(value)<<action;
            }
        }
    }
    void rejectsFailedOrInvalidResponse() {
        QFETCH(QByteArray,mode);QFETCH(int,operation);
        const auto action=static_cast<BchSuccessReleaseProtocol::Action>(operation);
        details["action"]=BchSuccessReleaseProtocol::actionName(action);
        QVERIFY(save(c.directory.filePath("release.json"),QJsonDocument(bound(details)).toJson(QJsonDocument::Compact)));
        BchSuccessReleaseReviewJob job;QVERIFY(job.configure(wrapper,action));
        QVERIFY(job.review(bound(proof),context));QTRY_VERIFY(!job.status().value("busy").toBool());
        QVERIFY(save(c.mode,mode));QVERIFY(job.review(bound(proof),context));QTRY_VERIFY(!job.status().value("busy").toBool());
        QCOMPARE(job.status().value("state").toString(),QString("unknown"));QVERIFY(job.status().value("review").toMap().isEmpty());
        QVERIFY(!job.status().value("chainObserved").toBool());
    }
    void invalidationCancelsPendingResponse() {
        BchSuccessReleaseReviewJob job;QVERIFY(job.configure(wrapper));QVERIFY(save(c.mode,"slow"));
        QVERIFY(job.review(bound(proof),context));QVERIFY(job.status().value("busy").toBool());
        job.invalidate();QVERIFY(!job.status().value("busy").toBool());
        QTest::qWait(300);
        QCOMPARE(job.status().value("state").toString(),QString("unknown"));
        QVERIFY(job.status().value("review").toMap().isEmpty());
        QVERIFY(save(c.mode,"valid"));QVERIFY(job.review(bound(proof),context));
        QTRY_VERIFY(!job.status().value("busy").toBool());
        QCOMPARE(job.status().value("state").toString(),QString("reviewed"));
    }
    void changedConfigurationStopsDispatch() {
        BchSuccessReleaseReviewJob job;QVERIFY(job.configure(wrapper));QVERIFY(save(c.path,"{}"));
        QVERIFY(!job.review(bound(proof),context));QCOMPARE(read(c.log),QByteArray());
    }
};
QTEST_GUILESS_MAIN(BchSuccessReleaseReviewTests)
#include "test_bch_success_release_review.moc"
