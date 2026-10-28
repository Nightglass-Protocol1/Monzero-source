#include "../BitcoinSettlementTemplate.h"

#include <QJsonArray>
#include <QtTest>
#include <limits>

namespace {
const QString sharedScript = "0020" + QString(64, 'a');

QJsonObject input(const QChar &txid, int vout, quint32 sequence)
{
    return {{"txid", QString(64, txid)}, {"vout", vout},
            {"scriptSig", QJsonObject{{"asm", ""}, {"hex", ""}}},
            {"sequence", static_cast<double>(sequence)}};
}

QJsonObject output(double amount, int index, const QString &address,
                   const QString &script, const QString &type)
{
    return {{"value", amount}, {"n", index},
            {"scriptPubKey", QJsonObject{{"address", address}, {"hex", script},
                                          {"type", type}}}};
}

QJsonObject lockTransaction()
{
    return {{"version", 2}, {"locktime", 0},
            {"vin", QJsonArray{input('a', 1, std::numeric_limits<quint32>::max())}},
            {"vout", QJsonArray{
                output(0.00010000, 0, "shared", sharedScript, "witness_v0_scripthash"),
                output(0.00002000, 1, "change", "0014ccdd", "witness_v0_keyhash")}}};
}

QJsonObject spendTransaction()
{
    return {{"version", 2}, {"locktime", 0},
            {"vin", QJsonArray{input('b', 0, 72)}},
            {"vout", QJsonArray{
                output(0.00009700, 0, "refund", "00141122", "witness_v0_keyhash")}}};
}
}

class BitcoinSettlementTemplateTest : public QObject
{
    Q_OBJECT
private slots:
    void validatesCommittedLock()
    {
        const QJsonObject tx = lockTransaction();
        const QByteArray commitment = BitcoinSettlementTemplate::commitment(tx);
        QCOMPARE(commitment.size(), 32);
        const QVariantMap result = BitcoinSettlementTemplate::validateLock(
            tx, commitment, "shared", sharedScript, "10000", "250", "300");
        QVERIFY(result.value("valid").toBool());
        QVERIFY(result.value("committed").toBool());
        QVERIFY(!result.value("executable").toBool());
        QCOMPARE(result.value("feeAtomic").toString(), QString("250"));
    }

    void rejectsLockMutationAndUnsafeFee()
    {
        const QJsonObject tx = lockTransaction();
        const QByteArray commitment = BitcoinSettlementTemplate::commitment(tx);
        QJsonObject changed = tx;
        QJsonArray outputs = changed.value("vout").toArray();
        QJsonObject target = outputs[0].toObject();
        target["value"] = 0.00009999;
        outputs[0] = target;
        changed["vout"] = outputs;
        QVERIFY(BitcoinSettlementTemplate::commitment(changed) != commitment);
        QVERIFY(!BitcoinSettlementTemplate::validateLock(
            changed, commitment, "shared", sharedScript, "10000", "250", "300")
                     .value("valid").toBool());
        QVERIFY(!BitcoinSettlementTemplate::validateLock(
            tx, commitment, "shared", sharedScript, "10000", "301", "300")
                     .value("valid").toBool());
        changed = tx;
        QJsonArray inputs = changed.value("vin").toArray();
        QJsonObject signedInput = inputs[0].toObject();
        signedInput["txinwitness"] = QJsonArray{"signature"};
        inputs[0] = signedInput;
        changed["vin"] = inputs;
        QVERIFY(BitcoinSettlementTemplate::commitment(changed).isEmpty());
        changed = tx;
        changed["locktime"] = 1;
        QVERIFY(BitcoinSettlementTemplate::commitment(changed).isEmpty());
        changed = tx;
        outputs = changed.value("vout").toArray();
        QJsonObject change = outputs[1].toObject();
        change["n"] = 2;
        outputs[1] = change;
        changed["vout"] = outputs;
        QVERIFY(BitcoinSettlementTemplate::commitment(changed).isEmpty());
    }

    void validatesCommittedCsvSpend()
    {
        const QJsonObject tx = spendTransaction();
        const QByteArray commitment = BitcoinSettlementTemplate::commitment(tx);
        const QVariantMap result = BitcoinSettlementTemplate::validateSpend(
            tx, commitment, QString(64, 'b'), "0", "10000", "refund", "00141122",
            "72", "300");
        QVERIFY(result.value("valid").toBool());
        QCOMPARE(result.value("feeAtomic").toString(), QString("300"));
        QVERIFY(!result.value("executable").toBool());
    }

    void rejectsSpendSubstitutionAndFeeExcess()
    {
        const QJsonObject tx = spendTransaction();
        const QByteArray commitment = BitcoinSettlementTemplate::commitment(tx);
        QVERIFY(!BitcoinSettlementTemplate::validateSpend(
            tx, commitment, QString(64, 'c'), "0", "10000", "refund", "00141122",
            "72", "300").value("valid").toBool());
        QVERIFY(!BitcoinSettlementTemplate::validateSpend(
            tx, commitment, QString(64, 'b'), "0", "10000", "refund", "00141122",
            "71", "300").value("valid").toBool());
        QVERIFY(!BitcoinSettlementTemplate::validateSpend(
            tx, commitment, QString(64, 'b'), "0", "10000", "attacker", "00141122",
            "72", "300").value("valid").toBool());
        QVERIFY(!BitcoinSettlementTemplate::validateSpend(
            tx, commitment, QString(64, 'b'), "0", "10000", "refund", "00141122",
            "72", "299").value("valid").toBool());
    }
};

QTEST_GUILESS_MAIN(BitcoinSettlementTemplateTest)
#include "test_bitcoin_settlement_template.moc"
