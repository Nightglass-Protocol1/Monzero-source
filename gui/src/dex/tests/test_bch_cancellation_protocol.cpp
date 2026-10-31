#include "BchCancellationTestFixture.h"
class BchCancellationProtocolTests:public QObject {
    Q_OBJECT
private slots:
    void bothRolesAndHistoricalReview() {
        for (bool native:{false,true}) {Context c;QVERIFY(c.create(native));QVERIFY(c.valid());
            c.now+=7200;QVERIFY(c.valid());}
    }
    void mismatchedDetails_data() {
        QTest::addColumn<QString>("field");QTest::addColumn<QJsonValue>("value");
        for (const auto &suffix:QStringList{"",":00",":-1",":4294967296",":0:1"})
            QTest::newRow(qPrintable("outpoint"+suffix))<<QString("fundingOutpoint")<<QJsonValue(QString(64,'a')+suffix);
        QTest::newRow("object-outpoint")<<QString("fundingOutpoint")<<QJsonValue(QJsonObject{{"txid",QString(64,'a')},{"vout",0}});
        QTest::newRow("principal")<<QString("recoveryAtoms")<<QJsonValue("99001");
        QTest::newRow("fee")<<QString("feeAtoms")<<QJsonValue("999");
        QTest::newRow("noncanonical")<<QString("feeAtoms")<<QJsonValue("01000");
        QTest::newRow("delay")<<QString("cancelDelayBlocks")<<QJsonValue(19);
        QTest::newRow("confirmations")<<QString("minimumBchConfirmations")<<QJsonValue(1);
        QTest::newRow("trade")<<QString("swapId")<<QJsonValue("different-trade");
        QTest::newRow("deployment")<<QString("bchDeployment")<<QJsonValue(pinBytes(8));
        QTest::newRow("private")<<QString("refundBytes")<<QJsonValue(QJsonArray{1,2});
        QTest::newRow("future")<<QString("preparedAt")<<QJsonValue(double(QDateTime::currentSecsSinceEpoch()+3600));
        QTest::newRow("script")<<QString("recoveryScript")<<QJsonValue("ABC");
    }
    void mismatchedDetails() {
        QFETCH(QString,field);QFETCH(QJsonValue,value);Context c;QVERIFY(c.create());QVERIFY(c.valid());
        c.details[field]=value;QVERIFY(!c.valid()); // recomputes digest after the mutation
    }
    void observationAndRelayAreSeparate() {
        Context c;QVERIFY(c.create());const auto review=bound(c.details);
        QJsonObject relay{{"state","acknowledged"},{"txid",QString(64,'b')},{"confirmed",false}};
        QVERIFY(BchCancellationProtocol::relay(relay,review));relay["confirmed"]=true;QVERIFY(!BchCancellationProtocol::relay(relay,review));
        QJsonObject observation{{"state","confirmed"},{"txid",QString(64,'b')},{"block",QString(64,'c')},{"tip",QString(64,'d')},
            {"height","10"},{"confirmations","2"},{"observedAt",QString::number(c.now)}};
        QVERIFY(BchCancellationProtocol::observation(observation,review,c.now,c.now));
        observation["confirmations"]="1";QVERIFY(!BchCancellationProtocol::observation(observation,review,c.now,c.now));
        observation["confirmations"]="2";observation["observedAt"]=QString::number(c.now-1);
        QVERIFY(!BchCancellationProtocol::observation(observation,review,c.now,c.now));
    }
};
QTEST_GUILESS_MAIN(BchCancellationProtocolTests)
#include "test_bch_cancellation_protocol.moc"
