#include "BchCancellationTestFixture.h"
#include "../BchNativePreparationProtocol.h"
class BchNativePreparationProtocolTests:public QObject {
    Q_OBJECT
    Context c;QJsonObject source;
    bool valid() const {return BchNativePreparationProtocol::review(bound(c.details),c.config,c.record,source);}
private slots:
    void init() {
        QVERIFY(c.create(true));const auto t=c.record.value("terms").toObject();const auto quote=c.details.value("quote");
        source={{"deploymentCommitment",QString(64,'7')},{"wallet",QString(95,'A')},{"account",0},{"priority",1},{"safetyBlocks",5}};
        c.details={{"schema","monzero-bch-native-preparation-review-v1"},{"action","prepare-bch-native-funding"},
            {"swapId",c.config.value("tradeId")},{"quote",quote},{"admission",pinBytes(1)},{"receipt",pinBytes(2)},
            {"deployment",pinBytes(0x77)},{"genesis",t.value("xmzGenesis")},{"wallet",source.value("wallet")},
            {"account",0},{"priority",1},{"destination",QString(95,'B')},{"amountAtoms",t.value("xmzAtoms")},
            {"maximumFeeAtoms",t.value("xmzFundingFeeMaxAtoms")},{"safetyBlocks",5}};
        QVERIFY(valid());
    }
    void changedFields_data() {
        QTest::addColumn<QString>("field");QTest::addColumn<QJsonValue>("value");
        QTest::newRow("numeric-amount")<<QString("amountAtoms")<<QJsonValue(1);
        QTest::newRow("leading-zero")<<QString("amountAtoms")<<QJsonValue("01");
        QTest::newRow("overflow")<<QString("amountAtoms")<<QJsonValue("18446744073709551616");
        QTest::newRow("zero-cap")<<QString("maximumFeeAtoms")<<QJsonValue("0");
        QTest::newRow("wrong-wallet")<<QString("wallet")<<QJsonValue(QString(95,'C'));
        QTest::newRow("invalid-destination")<<QString("destination")<<QJsonValue(QString(95,'0'));
        QTest::newRow("fractional-account")<<QString("account")<<QJsonValue(0.5);
        QTest::newRow("changed-priority")<<QString("priority")<<QJsonValue(2);
        QTest::newRow("zero-margin")<<QString("safetyBlocks")<<QJsonValue(0);
        QTest::newRow("wrong-network")<<QString("genesis")<<QJsonValue(QString(64,'d'));
        QTest::newRow("missing-receipt")<<QString("receipt")<<QJsonValue(QJsonArray{});
        QTest::newRow("private-metadata")<<QString("metadata")<<QJsonValue("deadbeef");
    }
    void changedFields() {QFETCH(QString,field);QFETCH(QJsonValue,value);c.details[field]=value;QVERIFY(!valid());}
    void wrongRoleDigestAndBoundary() {
        auto v=bound(c.details);v["commitment"]=QString(64,'0');QVERIFY(!BchNativePreparationProtocol::review(v,c.config,c.record,source));
        c.config["role"]="bch-funder";QVERIFY(!valid());c.config["role"]="native-funder";
        source["safetyBlocks"]=18;c.details["safetyBlocks"]=18;QVERIFY(!valid());
    }
    void retainedStates() {
        auto status=QJsonObject{{"state","attempted"},{"attemptedAt",QString::number(c.now)},{"payment",QJsonValue::Null}};
        QVERIFY(BchNativePreparationProtocol::saved(status,bound(c.details),c.config,c.now));
        status["state"]="prepared";QVERIFY(!BchNativePreparationProtocol::saved(status,bound(c.details),c.config,c.now));
        status["payment"]=QJsonObject{{"txid",QString(64,'a')},{"feeAtoms","1"},{"weight",2000}};
        QVERIFY(BchNativePreparationProtocol::saved(status,bound(c.details),c.config,c.now));
        status["state"]="attempted";QVERIFY(!BchNativePreparationProtocol::saved(status,bound(c.details),c.config,c.now));
        status["state"]="prepared";status["attemptedAt"]=QString::number(c.now+1);
        QVERIFY(!BchNativePreparationProtocol::saved(status,bound(c.details),c.config,c.now));
    }
};
QTEST_GUILESS_MAIN(BchNativePreparationProtocolTests)
#include "test_bch_native_preparation_protocol.moc"
