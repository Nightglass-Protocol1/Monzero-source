#include "BchCancellationTestFixture.h"
#include "../BchSuccessReleaseProtocol.h"
class BchSuccessReleaseProtocolTests:public QObject {
    Q_OBJECT
    Context c;QJsonObject details,proof,context,source;
    bool valid() const {return BchSuccessReleaseProtocol::review(bound(details),c.config,c.record,source,bound(proof),context);}
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
        QVERIFY(valid());
    }
    void rejectsReboundAlterations_data() {
        QTest::addColumn<QString>("field");QTest::addColumn<QJsonValue>("value");QTest::addColumn<int>("operation");
        const auto row=[](const char *name,const char *field,const QJsonValue &value) {
            for (int action=0;action<3;++action) {
                const auto label=QByteArray(name)+"-"+QByteArray::number(action);
                QTest::newRow(label.constData())<<QString(field)<<value<<action;
            }
        };
        row("action","action","release");row("trade","swapId",QUuid::createUuid().toString(QUuid::WithoutBraces));
        for (const auto key:{"quote","agreement","admission","bchDeployment"}) row(key,key,pinBytes(99));
        row("receipt","nativeProof",QJsonArray{});row("outpoint","bchFundingOutpoint",QString(64,'a')+":1");
        row("principal","bchAtoms","100001");row("numeric-principal","bchAtoms",100000);
        row("recipient","bchPayoutScript","52");row("payout","bchPayoutAtoms","98999");
        row("payout-overflow","bchPayoutAtoms","18446744073709551616");row("fee","bchFeeAtoms","1001");
        row("zero-fee","bchFeeAtoms","0");row("native-tx","nativeTxid",QString(64,'c'));
        row("native-address","nativeDestination",QString(95,'C'));row("native-amount","nativeAmountAtoms","2");
        row("bch-depth","minimumBchConfirmations",3);row("native-depth","minimumNativeConfirmations",0);
        row("margin","cancellationMarginBlocks",6);row("zero-margin","cancellationMarginBlocks",0);
        row("fractional-margin","cancellationMarginBlocks",5.5);row("private-frame","frame","private");
        row("confirmation-claim","confirmed",true);
    }
    void rejectsReboundAlterations() {
        QFETCH(QString,field);QFETCH(QJsonValue,value);QFETCH(int,operation);
        const auto action=static_cast<BchSuccessReleaseProtocol::Action>(operation);
        details["action"]=BchSuccessReleaseProtocol::actionName(action);
        QVERIFY(BchSuccessReleaseProtocol::review(bound(details),c.config,c.record,source,bound(proof),context,action));
        details[field]=value;
        QVERIFY(!BchSuccessReleaseProtocol::review(bound(details),c.config,c.record,source,bound(proof),context,action));
    }
    void approvalsAreBoundToTheirOperation() {
        using namespace BchSuccessReleaseProtocol;
        const auto preparation=bound(details);
        for (const auto action:{Action::Prepare,Action::RetainSignature,Action::ReleaseSigned}) {
            details["action"]=actionName(action);
            const auto checked=bound(details);
            for (const auto expected:{Action::Prepare,Action::RetainSignature,Action::ReleaseSigned}) {
                QCOMPARE(review(checked,c.config,c.record,source,bound(proof),context,expected),action==expected);
            }
            if (action!=Action::Prepare) {
                QVERIFY(checked.value("commitment")!=preparation.value("commitment"));
                auto reused=checked;reused["commitment"]=preparation.value("commitment");
                QVERIFY(!review(reused,c.config,c.record,source,bound(proof),context,action));
                // Existing preparation/status callers keep their strict default.
                QVERIFY(!review(checked,c.config,c.record,source,bound(proof),context));
            }
        }
        QVERIFY(!review(bound(details),c.config,c.record,source,bound(proof),context,static_cast<Action>(99)));
    }
    void requiresValidIndependentProofContext() {
        context["admission"]=QString(64,'9');QVERIFY(!valid());
        context["admission"]=QString(64,'1');proof["amountAtoms"]="2";QVERIFY(!valid());
    }
    void rejectsMarginAtCancellationBoundary() {
        source["safetyBlocks"]=18;details["cancellationMarginBlocks"]=18;QVERIFY(!valid());
        source["safetyBlocks"]=17;details["cancellationMarginBlocks"]=17;QVERIFY(valid());
    }
    void rejectsWrongCommitmentAndRole() {
        auto review=bound(details);review["commitment"]=QString(64,'0');
        QVERIFY(!BchSuccessReleaseProtocol::review(review,c.config,c.record,source,bound(proof),context));
        c.config["role"]="native-funder";QVERIFY(!valid());
    }
    void savedPreparationIsHistoricalAndStrictlyValidated() {
        const auto id=QUuid::createUuid().toString(QUuid::WithoutBraces);
        QJsonObject value{{"schema","monzero-bch-success-preparation-status-response-v1"},{"requestId",id},
            {"tradeId",c.config.value("tradeId")},{"executable",false},{"chainObserved",false},
            {"review",bound(details)},{"preparation",QJsonValue::Null}};
        const auto check=[&]{return BchSuccessReleaseProtocol::preparationStatusResponse(
            value,id,c.config,c.record,source,bound(proof),context,c.now);};
        QVERIFY(check());value.remove("preparation");QVERIFY(!check());
        QJsonObject saved{{"state","prepared"},{"preparedAt",QString::number(c.now)},{"signatureRetained",false}};
        value["preparation"]=saved;QVERIFY(check());saved["signatureRetained"]=true;value["preparation"]=saved;QVERIFY(check());
        saved["signatureRetained"]="true";value["preparation"]=saved;QVERIFY(!check());saved["signatureRetained"]=false;
        for (const auto at:{QJsonValue(QString::number(c.now+1)),QJsonValue("0"),QJsonValue("18446744073709551616"),QJsonValue(double(c.now))}) {
            saved["preparedAt"]=at;value["preparation"]=saved;QVERIFY(!check());
        }
        saved["preparedAt"]=QString::number(c.now);saved["signature"]="private";value["preparation"]=saved;QVERIFY(!check());
        saved.remove("signature");saved["state"]="released";value["preparation"]=saved;QVERIFY(!check());
        saved["state"]="prepared";value["preparation"]=saved;QVERIFY(check());
        value["chainObserved"]=true;QVERIFY(!check());value["chainObserved"]=false;
        value["requestId"]=QUuid::createUuid().toString(QUuid::WithoutBraces);QVERIFY(!check());value["requestId"]=id;
        value["executable"]=true;QVERIFY(!check());value["executable"]=false;
        value["frame"]=QJsonValue::Null;QVERIFY(!check());value.remove("frame");
        context["agreement"]=QString(64,'9');QVERIFY(!check());
    }
    void preparationRequiresExactConsentAndPublicResponse() {
        const auto id=QUuid::createUuid().toString(QUuid::WithoutBraces);
        const auto approved=bound(details).value("commitment").toString();
        QJsonObject value{{"schema","monzero-bch-success-preparation-response-v1"},{"requestId",id},
            {"tradeId",c.config.value("tradeId")},{"executable",false},{"chainObserved",true},
            {"state","prepared"},{"review",bound(details)}};
        const auto check=[&](const QString &consent){return BchSuccessReleaseProtocol::preparationResponse(
            value,id,consent,c.config,c.record,source,bound(proof),context);};
        QVERIFY(check(approved));QVERIFY(!check(QString(64,'0')));QVERIFY(!check(""));
        auto changed=details;changed["nativeProof"]=pinBytes(8);
        value["review"]=bound(changed);QVERIFY(!check(approved));value["review"]=bound(details);
        value["chainObserved"]=false;QVERIFY(!check(approved));value["chainObserved"]=true;
        value["chainObserved"]="true";QVERIFY(!check(approved));value["chainObserved"]=true;
        value["state"]="released";QVERIFY(!check(approved));value["state"]="prepared";
        value["executable"]=true;QVERIFY(!check(approved));value["executable"]=false;
        value["requestId"]=QUuid::createUuid().toString(QUuid::WithoutBraces);QVERIFY(!check(approved));value["requestId"]=id;
        value["tradeId"]=QUuid::createUuid().toString(QUuid::WithoutBraces);QVERIFY(!check(approved));value["tradeId"]=c.config.value("tradeId");
        for (const auto field:{"frame","signature","outputFile"}) {
            value[field]=QJsonValue::Null;QVERIFY(!check(approved));value.remove(field);
        }
        auto payout=details;payout["bchPayoutAtoms"]="98000";value["review"]=bound(payout);
        QVERIFY(!check(bound(payout).value("commitment").toString()));
        value["review"]=bound(details);QVERIFY(check(approved));
    }
    void reviewResponsesCannotCrossOperations() {
        using namespace BchSuccessReleaseProtocol;
        const auto id=QUuid::createUuid().toString(QUuid::WithoutBraces);
        for (const auto action:{Action::Prepare,Action::RetainSignature,Action::ReleaseSigned}) {
            details["action"]=actionName(action);
            QJsonObject value{{"schema",responseSchema(action)},{"requestId",id},
                {"tradeId",c.config.value("tradeId")},{"executable",false},{"chainObserved",false},{"review",bound(details)}};
            const auto check=[&](Action expected){return response(value,id,c.config,c.record,source,bound(proof),context,expected);};
            for (const auto expected:{Action::Prepare,Action::RetainSignature,Action::ReleaseSigned}) {
                QCOMPARE(check(expected),action==expected);
                value["schema"]=responseSchema(expected);
                QCOMPARE(check(expected),action==expected); // Relabelling cannot change the inner action.
                value["schema"]=responseSchema(action);
            }
            QVERIFY(!check(static_cast<Action>(99)));
            value["chainObserved"]=true;QVERIFY(!check(action));value["chainObserved"]=false;
            value["executable"]=true;QVERIFY(!check(action));value["executable"]=false;
            value["requestId"]=QUuid::createUuid().toString(QUuid::WithoutBraces);QVERIFY(!check(action));value["requestId"]=id;
            for (const auto field:{"frame","signature","outputFile"}) {
                value[field]=QJsonValue::Null;QVERIFY(!check(action));value.remove(field);
            }
            QVERIFY(check(action));
        }
    }
    void responseIsReadOnlyAndRequestBound() {
        const auto id=QUuid::createUuid().toString(QUuid::WithoutBraces);
        QJsonObject value{{"schema","monzero-bch-success-release-review-response-v1"},{"requestId",id},
            {"tradeId",c.config.value("tradeId")},{"executable",false},{"chainObserved",false},{"review",bound(details)}};
        const auto check=[&]{return BchSuccessReleaseProtocol::response(value,id,c.config,c.record,source,bound(proof),context);};
        QVERIFY(check());value["chainObserved"]=true;QVERIFY(!check());value["chainObserved"]=false;
        value["executable"]="false";QVERIFY(!check());value["executable"]=false;
        value["requestId"]=QUuid::createUuid().toString(QUuid::WithoutBraces);QVERIFY(!check());value["requestId"]=id;
        value["frame"]="private";QVERIFY(!check());
    }
};
QTEST_GUILESS_MAIN(BchSuccessReleaseProtocolTests)
#include "test_bch_success_release_protocol.moc"
