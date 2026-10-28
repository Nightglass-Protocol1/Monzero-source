#include "BitcoinPsbtEvidence.h"
#include "BitcoinSettlementTemplate.h"
#include "BitcoinSharedOutputScript.h"

#include <QJsonValue>
#include <QRegularExpression>
#include <cmath>
#include <limits>

namespace {
const quint64 satoshisPerBitcoin = 100000000ULL;
const quint64 maximumBitcoinSatoshis = 21000000ULL * satoshisPerBitcoin;
const QRegularExpression hash64("^[0-9a-f]{64}$");
const QRegularExpression lowercaseHex("^[0-9a-f]+$");

QVariantMap rejected(const QString &error)
{
    return {{"valid", false}, {"provenanceVerified", false},
            {"executable", false}, {"error", error}};
}

bool exactInteger(const QJsonValue &input, quint64 &value, quint64 maximum)
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

bool atomic(const QString &text, quint64 &value)
{
    static const QRegularExpression integer("^(0|[1-9][0-9]{0,15})$");
    bool ok = false;
    value = text.toULongLong(&ok);
    return ok && integer.match(text).hasMatch() && value <= maximumBitcoinSatoshis;
}

bool script(const QJsonObject &object, QString &hex)
{
    hex = object.value("hex").toString();
    return !hex.isEmpty() && hex.size() <= 10000 && hex.size() % 2 == 0
        && lowercaseHex.match(hex).hasMatch();
}

bool populated(const QJsonObject &object, const QString &key)
{
    if (!object.contains(key)) return false;
    const QJsonValue value = object.value(key);
    if (value.isArray()) return !value.toArray().isEmpty();
    if (value.isObject()) return !value.toObject().isEmpty();
    return !value.isNull() && !value.isUndefined() && value != QJsonValue("");
}

bool hasSigningOrAmbiguousMaterial(const QJsonObject &input)
{
    static const QStringList forbidden{
        "partial_signatures", "final_scriptSig", "final_scriptwitness",
        "ripemd160_preimages", "sha256_preimages", "hash160_preimages",
        "hash256_preimages", "taproot_key_path_sig", "taproot_script_path_sigs",
        "musig2_pubnonces", "musig2_partial_sigs", "unknown", "proprietary"};
    for (const QString &key : forbidden)
        if (populated(input, key)) return true;
    return input.contains("sighash") && input.value("sighash") != QJsonValue("ALL");
}
}

namespace BitcoinPsbtEvidence {
QVariantMap validateLock(const QJsonObject &psbt, const QJsonArray &prevouts,
                         const QByteArray &expectedCommitment,
                         const QString &expectedAddress, const QString &expectedScriptHex,
                         const QString &firstContractKeyHex,
                         const QString &secondContractKeyHex,
                         const QString &expectedAmountText, const QString &maximumFeeText,
                         const QString &expectedBestBlock, int minimumConfirmations)
{
    const QVariantMap scriptEvidence = BitcoinSharedOutputScript::validateProgram(
        expectedScriptHex, firstContractKeyHex, secondContractKeyHex);
    if (!scriptEvidence.value("valid").toBool())
        return rejected(scriptEvidence.value("error").toString());
    quint64 psbtVersion = 0, maximumFee = 0;
    if (!psbt.value("tx").isObject() || !psbt.value("inputs").isArray()
        || !psbt.value("outputs").isArray()
        || !exactInteger(psbt.value("psbt_version"), psbtVersion, 2)
        || (psbtVersion != 0 && psbtVersion != 2)
        || populated(psbt, "unknown") || populated(psbt, "proprietary")
        || psbt.contains("tx_modifiable")
        || !hash64.match(expectedBestBlock).hasMatch()
        || minimumConfirmations < 1 || minimumConfirmations > 1000
        || !atomic(maximumFeeText, maximumFee) || maximumFee == 0)
        return rejected("Invalid or modifiable Bitcoin PSBT policy");

    const QJsonObject transaction = psbt.value("tx").toObject();
    const QJsonArray transactionInputs = transaction.value("vin").toArray();
    const QJsonArray transactionOutputs = transaction.value("vout").toArray();
    const QJsonArray inputs = psbt.value("inputs").toArray();
    const QJsonArray outputs = psbt.value("outputs").toArray();
    if (inputs.size() != transactionInputs.size() || prevouts.size() != inputs.size()
        || outputs.size() != transactionOutputs.size())
        return rejected("Bitcoin PSBT maps do not match unsigned transaction");

    quint64 inputTotal = 0;
    for (int i = 0; i < inputs.size(); ++i) {
        if (!inputs[i].isObject() || !prevouts[i].isObject()
            || !transactionInputs[i].isObject())
            return rejected("Malformed Bitcoin PSBT input evidence");
        const QJsonObject input = inputs[i].toObject();
        const QJsonObject prevout = prevouts[i].toObject();
        const QJsonObject txInput = transactionInputs[i].toObject();
        if (hasSigningOrAmbiguousMaterial(input) || input.contains("non_witness_utxo")
            || !input.value("witness_utxo").isObject()
            || prevout.value("bestblock") != QJsonValue(expectedBestBlock)
            || prevout.value("coinbase") != QJsonValue(false)
            || prevout.value("txid") != txInput.value("txid")
            || prevout.value("vout") != txInput.value("vout"))
            return rejected("Unverified Bitcoin PSBT input provenance");
        quint64 confirmations = 0, witnessAmount = 0, chainAmount = 0;
        if (!exactInteger(prevout.value("confirmations"), confirmations,
                          std::numeric_limits<quint32>::max())
            || confirmations < static_cast<quint64>(minimumConfirmations)
            || !amount(prevout.value("value"), chainAmount))
            return rejected("Unconfirmed or malformed Bitcoin prevout");
        const QJsonObject witness = input.value("witness_utxo").toObject();
        if (!amount(witness.value("amount"), witnessAmount)
            || !witness.value("scriptPubKey").isObject()
            || !prevout.value("scriptPubKey").isObject())
            return rejected("Malformed Bitcoin witness UTXO");
        QString witnessScript, chainScript;
        if (!script(witness.value("scriptPubKey").toObject(), witnessScript)
            || !script(prevout.value("scriptPubKey").toObject(), chainScript)
            || witnessAmount != chainAmount || witnessScript != chainScript)
            return rejected("Bitcoin PSBT UTXO differs from active chain");
        if (chainAmount > std::numeric_limits<quint64>::max() - inputTotal)
            return rejected("Bitcoin PSBT input amount overflow");
        inputTotal += chainAmount;
    }
    for (const QJsonValue &value : outputs) {
        if (!value.isObject() || populated(value.toObject(), "unknown")
            || populated(value.toObject(), "proprietary"))
            return rejected("Ambiguous Bitcoin PSBT output metadata");
    }

    quint64 outputTotal = 0;
    for (const QJsonValue &value : transactionOutputs) {
        if (!value.isObject()) return rejected("Malformed Bitcoin PSBT output");
        quint64 outputAmount = 0;
        if (!amount(value.toObject().value("value"), outputAmount)
            || outputAmount > std::numeric_limits<quint64>::max() - outputTotal)
            return rejected("Bitcoin PSBT output amount overflow");
        outputTotal += outputAmount;
    }
    quint64 reportedFee = 0;
    if (inputTotal <= outputTotal || !amount(psbt.value("fee"), reportedFee)
        || reportedFee != inputTotal - outputTotal || reportedFee > maximumFee)
        return rejected("Bitcoin PSBT fee or balance mismatch");

    const QVariantMap lock = BitcoinSettlementTemplate::validateLock(
        transaction, expectedCommitment, expectedAddress, expectedScriptHex,
        expectedAmountText, QString::number(reportedFee), maximumFeeText);
    if (!lock.value("valid").toBool()) return rejected(lock.value("error").toString());
    return {{"valid", true}, {"provenanceVerified", true},
            {"scriptVerified", true},
            {"templateCommitment", lock.value("commitment")},
            {"feeAtomic", QString::number(reportedFee)},
            {"inputTotalAtomic", QString::number(inputTotal)}, {"executable", false}};
}
}
