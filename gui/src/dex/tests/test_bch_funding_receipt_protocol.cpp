#include "BchCancellationTestFixture.h"
#include "../BchFundingReceiptProtocol.h"
class BchFundingReceiptProtocolTests:public QObject {
    Q_OBJECT
    Context c;
    QString message=QString(64,'2'),transaction=QString(64,'a');
    bool valid() const {return BchFundingReceiptProtocol::review(bound(c.details),c.config,c.record,message,transaction);}
private slots:
    void init() {
        QVERIFY(c.create(true));auto t=c.record.value("terms").toObject();auto quote=c.details.value("quote");
        c.details={{"schema","monzero-bch-funding-receipt-review-v1"},{"action","accept-bch-funding-message"},
            {"swapId",c.config.value("tradeId")},{"quote",quote},{"admission",pinBytes(1)},
            {"messageCommitment",pinBytes(0x22)},{"bchNetwork",t.value("bchNetwork")},{"bchGenesis",t.value("bchGenesis")},
            {"fundingOutpoint",transaction+":0"},{"contractScript","aa20"+QString(64,'b')+"87"},
            {"principalAtoms","100000"},{"feeAtoms","1"}};
        QVERIFY(valid());
    }
    void historicalReceiptAndExactMessage() {
        QVERIFY(BchFundingReceiptProtocol::savedReview(bound(c.details),c.config,c.record));
        QVERIFY(!BchFundingReceiptProtocol::review(bound(c.details),c.config,c.record,message,{}));
        QVERIFY(!BchFundingReceiptProtocol::review(bound(c.details),c.config,c.record,{},transaction));
        QVERIFY(!BchFundingReceiptProtocol::review(bound(c.details),c.config,c.record,{},{}));
        message=QString(64,'3');QVERIFY(!valid());
        message=QString(64,'2');transaction=QString(64,'c');QVERIFY(!valid());
    }
    void wrongRoleAndDigest() {
        auto result=bound(c.details);result["commitment"]=QString(64,'0');
        QVERIFY(!BchFundingReceiptProtocol::review(result,c.config,c.record,message,transaction));
        c.config["role"]="bch-funder";QVERIFY(!valid());
    }
    void changedTerms_data() {
        QTest::addColumn<QString>("field");QTest::addColumn<QJsonValue>("value");
        QTest::newRow("principal")<<QString("principalAtoms")<<QJsonValue("100001");
        QTest::newRow("fee-cap")<<QString("feeAtoms")<<QJsonValue("2");
        QTest::newRow("zero-fee")<<QString("feeAtoms")<<QJsonValue("0");
        QTest::newRow("numeric-fee")<<QString("feeAtoms")<<QJsonValue(1);
        QTest::newRow("leading-zero")<<QString("feeAtoms")<<QJsonValue("01");
        QTest::newRow("fee-overflow")<<QString("feeAtoms")<<QJsonValue("18446744073709551616");
        QTest::newRow("missing-index")<<QString("fundingOutpoint")<<QJsonValue(QString(64,'a'));
        QTest::newRow("ambiguous-index")<<QString("fundingOutpoint")<<QJsonValue(QString(64,'a')+":00");
        QTest::newRow("index-overflow")<<QString("fundingOutpoint")<<QJsonValue(QString(64,'a')+":4294967296");
        QTest::newRow("empty-pin")<<QString("admission")<<QJsonValue(QJsonArray{});
        QTest::newRow("script-shape")<<QString("contractScript")<<QJsonValue("51");
        QTest::newRow("script-opcode")<<QString("contractScript")<<QJsonValue("a820"+QString(64,'b')+"87");
        QTest::newRow("network")<<QString("bchNetwork")<<QJsonValue("mainnet");
        QTest::newRow("unexpected-bytes")<<QString("signed")<<QJsonValue("deadbeef");
    }
    void changedTerms() {QFETCH(QString,field);QFETCH(QJsonValue,value);c.details[field]=value;QVERIFY(!valid());}
};
QTEST_GUILESS_MAIN(BchFundingReceiptProtocolTests)
#include "test_bch_funding_receipt_protocol.moc"
