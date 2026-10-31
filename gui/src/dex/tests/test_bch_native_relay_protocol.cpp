#include "BchCancellationTestFixture.h"
#include "../BchNativeRelayProtocol.h"
class BchNativeRelayProtocolTests:public QObject {
    Q_OBJECT
    Context c;QJsonObject source,preparedReview,preparation,details;
    bool valid() const {return BchNativeRelayProtocol::review(bound(details),c.config,c.record,source,preparedReview,preparation,c.now);}
private slots:
    void init() {
        QVERIFY(c.create(true));const auto t=c.record.value("terms").toObject();const auto quote=c.details.value("quote");
        source={{"deploymentCommitment",QString(64,'7')},{"wallet",QString(95,'A')},{"account",0},{"priority",1},{"safetyBlocks",5}};
        c.details={{"schema","monzero-bch-native-preparation-review-v1"},{"action","prepare-bch-native-funding"},
            {"swapId",c.config.value("tradeId")},{"quote",quote},{"admission",pinBytes(1)},{"receipt",pinBytes(2)},
            {"deployment",pinBytes(0x77)},{"genesis",t.value("xmzGenesis")},{"wallet",source.value("wallet")},
            {"account",0},{"priority",1},{"destination",QString(95,'B')},{"amountAtoms",t.value("xmzAtoms")},
            {"maximumFeeAtoms",t.value("xmzFundingFeeMaxAtoms")},{"safetyBlocks",5}};
        preparedReview=bound(c.details);
        preparation={{"state","prepared"},{"attemptedAt",QString::number(c.now)},
            {"payment",QJsonObject{{"txid",QString(64,'a')},{"feeAtoms","1"},{"weight",2000}}}};
        details=c.details;details.remove("receipt");details.remove("deployment");details.remove("safetyBlocks");
        details["schema"]="monzero-bch-native-relay-review-v1";details["action"]="broadcast-xmz-funding";
        details["fundingReceipt"]=pinBytes(2);details["bchDeployment"]=pinBytes(0x77);
        details["preparation"]=pinBytes(3);details["preparedPayment"]=pinBytes(4);
        details["network"]=t.value("xmzNetwork");details["expiresAt"]=t.value("expiresAt");
        details["minimumBchConfirmations"]=2;details["cancellationMarginBlocks"]=5;
        details["txid"]=QString(64,'a');details["feeAtoms"]="1";details["weight"]=2000;
        QVERIFY(valid());
    }
    void alteredPayment_data() {
        QTest::addColumn<QString>("field");QTest::addColumn<QJsonValue>("value");
        QTest::newRow("another-payment")<<QString("txid")<<QJsonValue(QString(64,'b'));
        QTest::newRow("another-destination")<<QString("destination")<<QJsonValue(QString(95,'C'));
        QTest::newRow("another-wallet")<<QString("wallet")<<QJsonValue(QString(95,'C'));
        QTest::newRow("another-fee")<<QString("feeAtoms")<<QJsonValue("2");
        QTest::newRow("numeric-amount")<<QString("amountAtoms")<<QJsonValue(1);
        QTest::newRow("weight")<<QString("weight")<<QJsonValue(2001);
        QTest::newRow("observer")<<QString("bchDeployment")<<QJsonValue(pinBytes(0x88));
        QTest::newRow("receipt")<<QString("fundingReceipt")<<QJsonValue(pinBytes(5));
        QTest::newRow("margin")<<QString("cancellationMarginBlocks")<<QJsonValue(6);
        QTest::newRow("confirmations")<<QString("minimumBchConfirmations")<<QJsonValue(3);
        QTest::newRow("expiry")<<QString("expiresAt")<<QJsonValue("1");
        QTest::newRow("network")<<QString("network")<<QJsonValue("other");
        QTest::newRow("missing-binding")<<QString("preparedPayment")<<QJsonValue(QJsonArray{});
        QTest::newRow("private-data")<<QString("metadata")<<QJsonValue("secret");
    }
    void alteredPayment() {QFETCH(QString,field);QFETCH(QJsonValue,value);details[field]=value;QVERIFY(!valid());}
    void requiresPreparedPayment() {
        preparation["state"]="attempted";preparation["payment"]=QJsonValue::Null;QVERIFY(!valid());
    }
    void relayStates() {
        QJsonObject state{{"state","attempted"},{"txid",details.value("txid")}};
        QVERIFY(BchNativeRelayProtocol::relayState(state,bound(details)));
        state["state"]="acknowledged";QVERIFY(BchNativeRelayProtocol::relayState(state,bound(details)));
        state["state"]="confirmed";QVERIFY(!BchNativeRelayProtocol::relayState(state,bound(details)));
        state["state"]="acknowledged";state["txid"]=QString(64,'b');
        QVERIFY(!BchNativeRelayProtocol::relayState(state,bound(details)));
        state["txid"]=details.value("txid");state["chainObserved"]=true;
        QVERIFY(!BchNativeRelayProtocol::relayState(state,bound(details)));
        state.remove("chainObserved");state.remove("txid");
        QVERIFY(!BchNativeRelayProtocol::relayState(state,bound(details)));
    }
    void rejectsChangedReviewCommitment() {
        auto value=bound(details);value["commitment"]=QString(64,'0');
        QVERIFY(!BchNativeRelayProtocol::review(value,c.config,c.record,source,preparedReview,preparation,c.now));
    }
    void rejectsCorruptSavedPreparation() {
        auto p=preparation.value("payment").toObject();p["feeAtoms"]="18446744073709551615";
        preparation["payment"]=p;details["feeAtoms"]=p.value("feeAtoms");QVERIFY(!valid());
    }
};
QTEST_GUILESS_MAIN(BchNativeRelayProtocolTests)
#include "test_bch_native_relay_protocol.moc"
