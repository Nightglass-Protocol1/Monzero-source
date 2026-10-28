#include "BitcoinSpendPsbtEvidence.h"
#include "BitcoinSettlementTemplate.h"
#include "BitcoinSharedOutputScript.h"
#include "BitcoinSegwitV0Sighash.h"

#include <QJsonArray>
#include <QRegularExpression>
#include <cmath>
#include <limits>

namespace {
constexpr quint64 satoshisPerBitcoin = 100000000ULL;
constexpr quint64 maximumBitcoinSatoshis = 21000000ULL * satoshisPerBitcoin;
const QRegularExpression hash64("^[0-9a-f]{64}$");
const QRegularExpression lowercaseHex("^[0-9a-f]+$");
const QRegularExpression p2wshHex("^0020[0-9a-f]{64}$");

QVariantMap rejected(const QString &error)
{
    return {{"valid", false}, {"provenanceVerified", false},
            {"executable", false}, {"error", error}};
}

bool exactInteger(const QJsonValue &input, quint64 maximum, quint64 &value)
{
    if (!input.isDouble()) return false;
    const double number = input.toDouble();
    if (!std::isfinite(number) || number < 0 || number != std::floor(number)
        || number > static_cast<double>(maximum)) return false;
    value = static_cast<quint64>(number);
    return true;
}

bool amount(const QJsonValue &input, quint64 &satoshis)
{
    if (!input.isDouble()) return false;
    const double coins = input.toDouble();
    if (!std::isfinite(coins) || coins < 0 || coins > 21000000.0) return false;
    const double scaled = coins * static_cast<double>(satoshisPerBitcoin);
    const double rounded = std::round(scaled);
    if (std::fabs(scaled - rounded) > 0.000001 || rounded > maximumBitcoinSatoshis)
        return false;
    satoshis = static_cast<quint64>(rounded);
    return true;
}

bool populated(const QJsonObject &object, const QString &key)
{
    if (!object.contains(key)) return false;
    const QJsonValue value = object.value(key);
    if (value.isArray()) return !value.toArray().isEmpty();
    if (value.isObject()) return !value.toObject().isEmpty();
    return !value.isNull() && !value.isUndefined() && value != QJsonValue("");
}

bool cleanMap(const QJsonObject &object)
{
    static const QStringList forbidden{
        "partial_signatures", "final_scriptSig", "final_scriptwitness",
        "ripemd160_preimages", "sha256_preimages", "hash160_preimages",
        "hash256_preimages", "taproot_key_path_sig", "taproot_script_path_sigs",
        "musig2_pubnonces", "musig2_partial_sigs", "unknown", "proprietary"};
    for (const QString &key : forbidden)
        if (populated(object, key)) return false;
    return !object.contains("non_witness_utxo")
        && (!object.contains("sighash") || object.value("sighash") == QJsonValue("ALL"));
}

bool script(const QJsonObject &object, QString &hex)
{
    hex = object.value("hex").toString();
    return !hex.isEmpty() && hex.size() <= 10000 && hex.size() % 2 == 0
        && lowercaseHex.match(hex).hasMatch();
}
}

namespace BitcoinSpendPsbtEvidence {
QVariantMap validate(const QJsonObject &psbt, const QJsonObject &parentPrevout,
                     const QByteArray &expectedCommitment,
                     const QString &expectedParentTxid, const QString &expectedParentVout,
                     const QString &expectedParentAmount, const QString &expectedParentScript,
                     const QString &firstContractKeyHex, const QString &secondContractKeyHex,
                     const QString &expectedRecipient, const QString &expectedRecipientScript,
                     const QString &expectedSequence, const QString &maximumFee,
                     const QString &expectedBestBlock, int minimumConfirmations)
{
    const QVariantMap scriptEvidence = BitcoinSharedOutputScript::validateProgram(
        expectedParentScript, firstContractKeyHex, secondContractKeyHex);
    if (!scriptEvidence.value("valid").toBool())
        return rejected(scriptEvidence.value("error").toString());
    quint64 psbtVersion = 0, confirmations = 0, witnessAmount = 0, chainAmount = 0;
    if (!psbt.value("tx").isObject() || !psbt.value("inputs").isArray()
        || !psbt.value("outputs").isArray()
        || !exactInteger(psbt.value("psbt_version"), 2, psbtVersion)
        || (psbtVersion != 0 && psbtVersion != 2)
        || psbt.contains("tx_modifiable") || populated(psbt, "unknown")
        || populated(psbt, "proprietary") || !hash64.match(expectedBestBlock).hasMatch()
        || !hash64.match(expectedParentTxid).hasMatch()
        || !p2wshHex.match(expectedParentScript).hasMatch()
        || minimumConfirmations < 1 || minimumConfirmations > 1000)
        return rejected("Invalid or modifiable Bitcoin settlement PSBT policy");

    const QJsonArray inputs = psbt.value("inputs").toArray();
    const QJsonArray outputs = psbt.value("outputs").toArray();
    if (inputs.size() != 1 || outputs.size() != 1 || !inputs[0].isObject()
        || !outputs[0].isObject())
        return rejected("Bitcoin settlement PSBT must have one input and one output map");
    const QJsonObject input = inputs[0].toObject();
    if (!cleanMap(input) || populated(outputs[0].toObject(), "unknown")
        || populated(outputs[0].toObject(), "proprietary")
        || !input.value("witness_utxo").isObject())
        return rejected("Bitcoin settlement PSBT contains signing or ambiguous material");

    bool voutOk = false;
    const quint64 parentVout = expectedParentVout.toULongLong(&voutOk);
    if (!voutOk || QString::number(parentVout) != expectedParentVout
        || parentVout > std::numeric_limits<quint32>::max()
        || parentPrevout.value("bestblock") != QJsonValue(expectedBestBlock)
        || parentPrevout.value("txid") != QJsonValue(expectedParentTxid)
        || parentPrevout.value("vout") != QJsonValue(static_cast<double>(parentVout))
        || parentPrevout.value("coinbase") != QJsonValue(false)
        || !exactInteger(parentPrevout.value("confirmations"),
                         std::numeric_limits<quint32>::max(), confirmations)
        || confirmations < static_cast<quint64>(minimumConfirmations)
        || !amount(parentPrevout.value("value"), chainAmount))
        return rejected("Unconfirmed or mismatched Bitcoin settlement parent");

    const QJsonObject witness = input.value("witness_utxo").toObject();
    if (!amount(witness.value("amount"), witnessAmount)
        || witnessAmount != chainAmount || !witness.value("scriptPubKey").isObject()
        || !parentPrevout.value("scriptPubKey").isObject())
        return rejected("Malformed Bitcoin settlement witness UTXO");
    QString witnessScript, chainScript;
    if (!script(witness.value("scriptPubKey").toObject(), witnessScript)
        || !script(parentPrevout.value("scriptPubKey").toObject(), chainScript)
        || witnessScript != expectedParentScript || chainScript != expectedParentScript)
        return rejected("Bitcoin settlement parent script mismatch");

    quint64 expectedAmountAtomic = 0, reportedFee = 0;
    bool amountOk = false;
    expectedAmountAtomic = expectedParentAmount.toULongLong(&amountOk);
    if (!amountOk || QString::number(expectedAmountAtomic) != expectedParentAmount
        || expectedAmountAtomic != chainAmount || !amount(psbt.value("fee"), reportedFee))
        return rejected("Bitcoin settlement parent amount or fee mismatch");

    const QVariantMap spend = BitcoinSettlementTemplate::validateSpend(
        psbt.value("tx").toObject(), expectedCommitment, expectedParentTxid,
        expectedParentVout, expectedParentAmount, expectedRecipient,
        expectedRecipientScript, expectedSequence, maximumFee);
    if (!spend.value("valid").toBool()) return rejected(spend.value("error").toString());
    if (spend.value("feeAtomic").toString() != QString::number(reportedFee))
        return rejected("Bitcoin settlement PSBT reported fee mismatch");

    const QVariantMap sighash = BitcoinSegwitV0Sighash::computeAll(
        psbt.value("tx").toObject(), 0, scriptEvidence.value("witnessScriptHex").toString(),
        expectedParentAmount);
    if (!sighash.value("valid").toBool())
        return rejected(sighash.value("error").toString());

    return {{"valid", true}, {"provenanceVerified", true},
            {"scriptVerified", true},
            {"sighashVerified", true},
            {"sighashHex", sighash.value("digestHex")},
            {"templateCommitment", spend.value("commitment")},
            {"feeAtomic", QString::number(reportedFee)},
            {"parentConfirmations", QString::number(confirmations)},
            {"executable", false}};
}
}
