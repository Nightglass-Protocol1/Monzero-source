#include "BchPunishmentTestFixture.h"
class BchPunishmentProtocolTests:public QObject {
    Q_OBJECT
private slots:
    void historicalNativeReview() {PunishContext c;QVERIFY(c.create());QVERIFY(c.valid());c.now+=7200;QVERIFY(c.valid());
        c.config["role"]="bch-funder";QVERIFY(!c.valid());}
    void mutatedTerms_data() {
        QTest::addColumn<QString>("field");QTest::addColumn<QJsonValue>("value");
        QTest::newRow("fee")<<QString("feeAtoms")<<QJsonValue("2");
        QTest::newRow("payout")<<QString("payoutAtoms")<<QJsonValue("999999");
        QTest::newRow("destination")<<QString("destinationScript")<<QJsonValue("6a");
        QTest::newRow("backend")<<QString("bchDeployment")<<QJsonValue(pinBytes(8));
        QTest::newRow("timeout")<<QString("punishDelayBlocks")<<QJsonValue(2);
        QTest::newRow("depth")<<QString("minimumXmzConfirmations")<<QJsonValue(0);
        QTest::newRow("outpoint")<<QString("recoveryOutpoint")<<QJsonValue(QString(64,'d')+":1");
        QTest::newRow("noncanonical")<<QString("fundingOutpoint")<<QJsonValue(QString(64,'a')+":00");
        QTest::newRow("payment")<<QString("nativeFundingTxid")<<QJsonValue(QString(64,'0'));
        QTest::newRow("future")<<QString("preparedAt")<<QJsonValue(double(QDateTime::currentSecsSinceEpoch()+3600));
        QTest::newRow("private")<<QString("privateKey")<<QJsonValue("unexpected");
    }
    void mutatedTerms() {QFETCH(QString,field);QFETCH(QJsonValue,value);PunishContext c;QVERIFY(c.create());QVERIFY(c.valid());
        c.details[field]=value;QVERIFY(!c.valid());}
    void changedPolicy() {PunishContext c;QVERIFY(c.create());QVERIFY(c.valid());
        auto policy=c.config.value("policy").toObject();policy["punish_blocks"]=99;c.config["policy"]=policy;QVERIFY(!c.valid());}
    void policyLimits_data() {
        QTest::addColumn<QString>("field");QTest::addColumn<int>("limit");QTest::addColumn<bool>("accepted");
        QTest::newRow("principal-exact")<<QString("maximum_bch_atoms")<<100000<<true;
        QTest::newRow("principal-too-small")<<QString("maximum_bch_atoms")<<99999<<false;
        QTest::newRow("lock-fee-floor")<<QString("minimum_covenant_fee")<<1000<<true;
        QTest::newRow("lock-fee-below-floor")<<QString("minimum_covenant_fee")<<1001<<false;
        QTest::newRow("recovery-fee-ceiling")<<QString("maximum_covenant_fee")<<2000<<true;
        QTest::newRow("recovery-fee-above-ceiling")<<QString("maximum_covenant_fee")<<1999<<false;
        QTest::newRow("changed-output-policy")<<QString("minimum_output_atoms")<<547<<false;
    }
    void policyLimits() {QFETCH(QString,field);QFETCH(int,limit);QFETCH(bool,accepted);
        PunishContext c;QVERIFY(c.create());QVERIFY(c.valid());
        auto policy=c.config.value("policy").toObject();policy[field]=limit;c.config["policy"]=policy;QCOMPARE(c.valid(),accepted);}
    void minimumOutputBoundary_data() {
        QTest::addColumn<int>("minimum");QTest::addColumn<bool>("accepted");
        QTest::newRow("below-payout")<<96999<<true;
        QTest::newRow("exact-payout")<<97000<<true;
        QTest::newRow("above-payout")<<97001<<false;
    }
    void minimumOutputBoundary() {QFETCH(int,minimum);QFETCH(bool,accepted);
        PunishContext c;QVERIFY(c.create());QVERIFY(c.valid());
        auto policy=c.config.value("policy").toObject();policy["minimum_output_atoms"]=minimum;c.config["policy"]=policy;
        auto terms=c.record.value("terms").toObject();terms["bchMinimumOutputAtoms"]=QString::number(minimum);
        c.record["terms"]=terms;c.bindQuote();QCOMPARE(c.valid(),accepted);}
    void excessiveConfirmationThreshold() {PunishContext c;QVERIFY(c.create());QVERIFY(c.valid());
        auto terms=c.record.value("terms").toObject();terms["xmzConfirmations"]="1001";c.record["terms"]=terms;c.bindQuote();
        auto policy=c.config.value("policy").toObject();policy["xmz_confirmations"]=1001;c.config["policy"]=policy;
        c.details["minimumXmzConfirmations"]=1001;QVERIFY(!c.valid());}
    void missingPaymentAndBadCommitment() {PunishContext c;QVERIFY(c.create());QVERIFY(c.valid());
        auto value=bound(c.details);value["commitment"]=QString(64,'0');
        QVERIFY(!BchPunishmentProtocol::review(value,c.config,c.record,c.deployment,c.now));
        c.details.remove("nativePayment");QVERIFY(!c.valid());}
};
QTEST_GUILESS_MAIN(BchPunishmentProtocolTests)
#include "test_bch_punishment_protocol.moc"
