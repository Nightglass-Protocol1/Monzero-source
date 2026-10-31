#include "BchRefundReleaseTestFixture.h"
#include "../BchRefundReleaseProtocol.h"
class BchRefundReleaseProtocolTests:public QObject {
    Q_OBJECT
    RefundContext c;
    bool valid(quint32 margin=5) const {
        return BchRefundReleaseProtocol::review(bound(c.details),c.config,c.record,c.deployment,margin,c.now);
    }
private slots:
    void init() {
        QVERIFY(c.create());QVERIFY(valid());
    }
    void historicalReviewAndMargin() {
        c.now+=7200;QVERIFY(valid());QVERIFY(!valid(6));
        c.details["punishmentMarginBlocks"]=6;QVERIFY(valid(6));
        c.details["punishmentMarginBlocks"]=28;QVERIFY(!valid(28));
    }
    void alteredPublicTerms_data() {
        QTest::addColumn<QString>("field");QTest::addColumn<QJsonValue>("value");
        QTest::newRow("destination")<<QString("destinationScript")<<QJsonValue("52");
        QTest::newRow("amount")<<QString("refundAtoms")<<QJsonValue("97001");
        QTest::newRow("fee")<<QString("feeAtoms")<<QJsonValue("1999");
        QTest::newRow("disclosure")<<QString("revealsNativeSpendShare")<<QJsonValue(false);
        QTest::newRow("disclosure-type")<<QString("revealsNativeSpendShare")<<QJsonValue("true");
        QTest::newRow("recovery-output")<<QString("recoveryOutpoint")<<QJsonValue(QString(64,'c')+":1");
        QTest::newRow("ambiguous-index")<<QString("fundingOutpoint")<<QJsonValue(QString(64,'a')+":00");
        QTest::newRow("margin")<<QString("punishmentMarginBlocks")<<QJsonValue(0);
        QTest::newRow("private-bytes")<<QString("refundBytes")<<QJsonValue("deadbeef");
    }
    void alteredPublicTerms() {
        QFETCH(QString,field);QFETCH(QJsonValue,value);c.details[field]=value;QVERIFY(!valid());
    }
};
QTEST_GUILESS_MAIN(BchRefundReleaseProtocolTests)
#include "test_bch_refund_release_protocol.moc"
