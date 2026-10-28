#include "../BitcoinPsbtEvidence.h"
#include "../BitcoinSettlementTemplate.h"

#include <QJsonArray>
#include <QCryptographicHash>
#include <QtTest>
#include <limits>

namespace {
const QString keyA = "0279be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798";
const QString keyB = "02c6047f9441ed7d6d3045406e95c07cd85c778e4b8cef3ca7abac09b95c709ee5";
const QString witnessScript = "21" + keyA + "ad21" + keyB + "ac";
const QString sharedScript = "0020" + QString::fromLatin1(QCryptographicHash::hash(
    QByteArray::fromHex(witnessScript.toLatin1()), QCryptographicHash::Sha256).toHex());
const QString fundingScript = "0014" + QString(40, 'b');

QJsonObject transaction()
{
    return {{"version", 2}, {"locktime", 0},
            {"vin", QJsonArray{QJsonObject{
                {"txid", QString(64, 'd')}, {"vout", 1},
                {"scriptSig", QJsonObject{{"asm", ""}, {"hex", ""}}},
                {"sequence", static_cast<double>(std::numeric_limits<quint32>::max())}}}},
            {"vout", QJsonArray{
                QJsonObject{{"value", 0.00010000}, {"n", 0},
                    {"scriptPubKey", QJsonObject{{"address", "shared"},
                        {"hex", sharedScript}, {"type", "witness_v0_scripthash"}}}},
                QJsonObject{{"value", 0.00002000}, {"n", 1},
                    {"scriptPubKey", QJsonObject{{"address", "change"},
                        {"hex", "0014" + QString(40, 'c')},
                        {"type", "witness_v0_keyhash"}}}}}}};
}

QJsonObject psbt()
{
    const QJsonObject fundingScriptObject{{"hex", fundingScript},
                                           {"type", "witness_v0_keyhash"}};
    const QJsonObject witnessUtxo{{"amount", 0.00012250},
                                   {"scriptPubKey", fundingScriptObject}};
    const QJsonObject psbtInput{{"witness_utxo", witnessUtxo}, {"sighash", "ALL"}};
    return {{"tx", transaction()}, {"psbt_version", 0},
            {"inputs", QJsonArray{psbtInput}},
            {"outputs", QJsonArray{QJsonObject{}, QJsonObject{}}},
            {"fee", 0.00000250}};
}

QJsonArray prevouts()
{
    return {QJsonObject{{"txid", QString(64, 'd')}, {"vout", 1},
        {"bestblock", QString(64, 'e')}, {"confirmations", 6},
        {"value", 0.00012250}, {"coinbase", false},
        {"scriptPubKey", QJsonObject{{"hex", fundingScript},
            {"type", "witness_v0_keyhash"}}}}};
}
}

class BitcoinPsbtEvidenceTest : public QObject
{
    Q_OBJECT
private slots:
    void validatesActiveChainProvenance()
    {
        const QJsonObject decoded = psbt();
        const QByteArray commitment = BitcoinSettlementTemplate::commitment(transaction());
        const QVariantMap result = BitcoinPsbtEvidence::validateLock(
            decoded, prevouts(), commitment, "shared", sharedScript, keyA, keyB, "10000", "300",
            QString(64, 'e'), 6);
        QVERIFY(result.value("valid").toBool());
        QVERIFY(result.value("provenanceVerified").toBool());
        QCOMPARE(result.value("inputTotalAtomic").toString(), QString("12250"));
        QCOMPARE(result.value("feeAtomic").toString(), QString("250"));
        QVERIFY(!result.value("executable").toBool());
    }

    void rejectsChainAndWitnessSubstitution()
    {
        const QByteArray commitment = BitcoinSettlementTemplate::commitment(transaction());
        QJsonArray evidence = prevouts();
        QJsonObject changed = evidence[0].toObject();
        QJsonObject script = changed.value("scriptPubKey").toObject();
        script["hex"] = "0014" + QString(40, 'f');
        changed["scriptPubKey"] = script;
        evidence[0] = changed;
        QVERIFY(!BitcoinPsbtEvidence::validateLock(psbt(), evidence, commitment,
            "shared", sharedScript, keyA, keyB, "10000", "300", QString(64, 'e'), 6)
                     .value("valid").toBool());
        evidence = prevouts();
        changed = evidence[0].toObject(); changed["bestblock"] = QString(64, 'f');
        evidence[0] = changed;
        QVERIFY(!BitcoinPsbtEvidence::validateLock(psbt(), evidence, commitment,
            "shared", sharedScript, keyA, keyB, "10000", "300", QString(64, 'e'), 6)
                     .value("valid").toBool());
        evidence = prevouts();
        changed = evidence[0].toObject(); changed["confirmations"] = 5;
        evidence[0] = changed;
        QVERIFY(!BitcoinPsbtEvidence::validateLock(psbt(), evidence, commitment,
            "shared", sharedScript, keyA, keyB, "10000", "300", QString(64, 'e'), 6)
                     .value("valid").toBool());
    }

    void rejectsSigningMaterialAndFeeMismatch()
    {
        const QByteArray commitment = BitcoinSettlementTemplate::commitment(transaction());
        QJsonObject decoded = psbt();
        QJsonArray inputs = decoded.value("inputs").toArray();
        QJsonObject input = inputs[0].toObject();
        input["partial_signatures"] = QJsonObject{{"pubkey", "signature"}};
        inputs[0] = input; decoded["inputs"] = inputs;
        QVERIFY(!BitcoinPsbtEvidence::validateLock(decoded, prevouts(), commitment,
            "shared", sharedScript, keyA, keyB, "10000", "300", QString(64, 'e'), 6)
                     .value("valid").toBool());
        QVERIFY(!BitcoinPsbtEvidence::validateLock(psbt(), prevouts(), commitment,
            "shared", sharedScript, keyB, keyA, "10000", "300", QString(64, 'e'), 6)
                     .value("valid").toBool());
        decoded = psbt(); decoded["fee"] = 0.00000249;
        QVERIFY(!BitcoinPsbtEvidence::validateLock(decoded, prevouts(), commitment,
            "shared", sharedScript, keyA, keyB, "10000", "300", QString(64, 'e'), 6)
                     .value("valid").toBool());
        decoded = psbt(); decoded["tx_modifiable"] = QJsonArray{"inputs"};
        QVERIFY(!BitcoinPsbtEvidence::validateLock(decoded, prevouts(), commitment,
            "shared", sharedScript, keyA, keyB, "10000", "300", QString(64, 'e'), 6)
                     .value("valid").toBool());
    }
};

QTEST_GUILESS_MAIN(BitcoinPsbtEvidenceTest)
#include "test_bitcoin_psbt_evidence.moc"
