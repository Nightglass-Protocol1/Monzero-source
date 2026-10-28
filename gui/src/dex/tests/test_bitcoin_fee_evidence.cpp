#include "../BitcoinFeeEvidence.h"

#include <QJsonArray>
#include <QtTest>

class BitcoinFeeEvidenceTest : public QObject
{
    Q_OBJECT
private slots:
    void acceptsEstimateWithinBothCeilings()
    {
        const QVariantMap result = BitcoinFeeEvidence::validate(
            {{"feerate", 0.00001000}, {"blocks", 6}}, 6, 200, "250", "300");
        QVERIFY(result.value("valid").toBool());
        QVERIFY(result.value("feeEstimateVerified").toBool());
        QCOMPARE(result.value("feeRateAtomicPerKvb").toString(), QString("1000"));
        QCOMPARE(result.value("estimatedFeeAtomic").toString(), QString("200"));
        QCOMPARE(result.value("networkPolicyCeilingAtomic").toString(), QString("400"));
        QVERIFY(!result.value("executable").toBool());
    }

    void rejectsRpcErrorsAndInvalidHorizon()
    {
        QVERIFY(!BitcoinFeeEvidence::validate(
            {{"feerate", 0.00001000}, {"blocks", 6},
             {"errors", QJsonArray{"insufficient data"}}},
            6, 200, "250", "300").value("valid").toBool());
        QVERIFY(!BitcoinFeeEvidence::validate(
            {{"feerate", 0.00001000}, {"blocks", 7}},
            6, 200, "250", "300").value("valid").toBool());
    }

    void rejectsMalformedAndExcessiveRates()
    {
        QVERIFY(!BitcoinFeeEvidence::validate(
            {{"feerate", 0.000010001}, {"blocks", 6}},
            6, 200, "250", "300").value("valid").toBool());
        QVERIFY(!BitcoinFeeEvidence::validate(
            {{"feerate", 22.0}, {"blocks", 6}},
            6, 200, "250", "300").value("valid").toBool());
        QVERIFY(!BitcoinFeeEvidence::validate(
            {{"feerate", 0.00001000}, {"blocks", 6}},
            6, 4000001, "250", "300").value("valid").toBool());
    }

    void enforcesNetworkAndSignedCeilings()
    {
        QVERIFY(!BitcoinFeeEvidence::validate(
            {{"feerate", 0.00000100}, {"blocks", 6}},
            6, 200, "250", "300").value("valid").toBool());
        QVERIFY(!BitcoinFeeEvidence::validate(
            {{"feerate", 0.00001000}, {"blocks", 6}},
            6, 200, "301", "300").value("valid").toBool());
        QVERIFY(!BitcoinFeeEvidence::validate(
            {{"feerate", 0.00001000}, {"blocks", 6}},
            6, 200, "250", "300", 9999).value("valid").toBool());
    }
};

QTEST_GUILESS_MAIN(BitcoinFeeEvidenceTest)
#include "test_bitcoin_fee_evidence.moc"
