#include "../BitcoinSettlementTemplate.h"
#include "../BitcoinSpendPsbtEvidence.h"

#include <QJsonArray>
#include <QCryptographicHash>
#include <QtTest>

namespace {
const QString parentTxid = QString(64, 'b');
const QString bestBlock = QString(64, 'e');
const QString keyA = "0279be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798";
const QString keyB = "02c6047f9441ed7d6d3045406e95c07cd85c778e4b8cef3ca7abac09b95c709ee5";
const QString witnessScript = "21" + keyA + "ad21" + keyB + "ac";
const QString parentScript = "0020" + QString::fromLatin1(QCryptographicHash::hash(
    QByteArray::fromHex(witnessScript.toLatin1()), QCryptographicHash::Sha256).toHex());
const QString recipientScript = "0014" + QString(40, 'c');

QJsonObject transaction()
{
    return {{"version", 2}, {"locktime", 0},
        {"vin", QJsonArray{QJsonObject{{"txid", parentTxid}, {"vout", 0},
            {"scriptSig", QJsonObject{{"asm", ""}, {"hex", ""}}},
            {"sequence", 144}}}},
        {"vout", QJsonArray{QJsonObject{{"value", 0.00009700}, {"n", 0},
            {"scriptPubKey", QJsonObject{{"address", "refund"},
                {"hex", recipientScript}, {"type", "witness_v0_keyhash"}}}}}}};
}

QJsonObject decodedPsbt()
{
    return {{"tx", transaction()}, {"psbt_version", 0},
        {"inputs", QJsonArray{QJsonObject{
            {"witness_utxo", QJsonObject{{"amount", 0.00010000},
                {"scriptPubKey", QJsonObject{{"hex", parentScript},
                    {"type", "witness_v0_scripthash"}}}}}, {"sighash", "ALL"}}}},
        {"outputs", QJsonArray{QJsonObject{}}}, {"fee", 0.00000300}};
}

QJsonObject prevout()
{
    return {{"txid", parentTxid}, {"vout", 0}, {"bestblock", bestBlock},
        {"confirmations", 12}, {"value", 0.00010000}, {"coinbase", false},
        {"scriptPubKey", QJsonObject{{"hex", parentScript},
            {"type", "witness_v0_scripthash"}}}};
}

QVariantMap validate(const QJsonObject &psbt, const QJsonObject &parent,
                     const QString &a = keyA, const QString &b = keyB)
{
    return BitcoinSpendPsbtEvidence::validate(psbt, parent,
        BitcoinSettlementTemplate::commitment(transaction()), parentTxid, "0", "10000",
        parentScript, a, b, "refund", recipientScript, "144", "500", bestBlock, 6);
}
}

class BitcoinSpendPsbtEvidenceTest : public QObject
{
    Q_OBJECT
private slots:
    void validatesRefundProvenance()
    {
        const QVariantMap result = validate(decodedPsbt(), prevout());
        QVERIFY(result.value("valid").toBool());
        QVERIFY(result.value("provenanceVerified").toBool());
        QVERIFY(result.value("sighashVerified").toBool());
        QCOMPARE(result.value("sighashHex").toString().size(), 64);
        QCOMPARE(result.value("feeAtomic").toString(), QString("300"));
        QCOMPARE(result.value("parentConfirmations").toString(), QString("12"));
        QVERIFY(!result.value("executable").toBool());
    }

    void rejectsParentSubstitutionAndSpentEvidence()
    {
        QJsonObject changed = prevout(); changed["txid"] = QString(64, 'f');
        QVERIFY(!validate(decodedPsbt(), changed).value("valid").toBool());
        changed = prevout(); changed["scriptPubKey"] =
            QJsonObject{{"hex", "0020" + QString(64, 'f')}};
        QVERIFY(!validate(decodedPsbt(), changed).value("valid").toBool());
        changed = prevout(); changed["confirmations"] = 5;
        QVERIFY(!validate(decodedPsbt(), changed).value("valid").toBool());
        changed = prevout(); changed["bestblock"] = QString(64, 'f');
        QVERIFY(!validate(decodedPsbt(), changed).value("valid").toBool());
    }

    void rejectsSigningMaterialAndFeeMismatch()
    {
        QJsonObject changed = decodedPsbt();
        QJsonArray inputs = changed.value("inputs").toArray();
        QJsonObject input = inputs[0].toObject();
        input["partial_signatures"] = QJsonObject{{"key", "signature"}};
        inputs[0] = input; changed["inputs"] = inputs;
        QVERIFY(!validate(changed, prevout()).value("valid").toBool());
        changed = decodedPsbt(); changed["fee"] = 0.00000299;
        QVERIFY(!validate(changed, prevout()).value("valid").toBool());
        changed = decodedPsbt(); changed["tx_modifiable"] = QJsonArray{"inputs"};
        QVERIFY(!validate(changed, prevout()).value("valid").toBool());
    }

    void rejectsWrongRefundSequenceAndRecipient()
    {
        QJsonObject changed = decodedPsbt();
        QJsonObject tx = changed.value("tx").toObject();
        QJsonArray inputs = tx.value("vin").toArray();
        QJsonObject input = inputs[0].toObject(); input["sequence"] = 143;
        inputs[0] = input; tx["vin"] = inputs; changed["tx"] = tx;
        QVERIFY(!validate(changed, prevout()).value("valid").toBool());
        QVERIFY(!validate(decodedPsbt(), prevout(), keyB, keyA).value("valid").toBool());
        changed = decodedPsbt(); tx = changed.value("tx").toObject();
        QJsonArray outputs = tx.value("vout").toArray();
        QJsonObject output = outputs[0].toObject();
        QJsonObject script = output.value("scriptPubKey").toObject();
        script["address"] = "attacker"; output["scriptPubKey"] = script;
        outputs[0] = output; tx["vout"] = outputs; changed["tx"] = tx;
        QVERIFY(!validate(changed, prevout()).value("valid").toBool());
    }
};

QTEST_GUILESS_MAIN(BitcoinSpendPsbtEvidenceTest)
#include "test_bitcoin_spend_psbt_evidence.moc"
