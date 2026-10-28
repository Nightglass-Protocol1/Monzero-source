#include "MonzeroTransactionEvidence.h"
#include <QJsonArray>
#include <QRegularExpression>
#include <cmath>
#include <limits>

namespace {
const QRegularExpression hash64("^[0-9a-f]{64}$");

QVariantMap rejected(const QString &error)
{
    return {{"valid", false}, {"found", false}, {"confirmed", false},
            {"spendable", false}, {"executable", false}, {"error", error}};
}

bool exactInteger(const QJsonValue &input, quint64 &value)
{
    if (!input.isDouble()) return false;
    const double number = input.toDouble();
    if (!std::isfinite(number) || number < 0 || number != std::floor(number)
        || number > 9007199254740991.0) return false;
    value = static_cast<quint64>(number);
    return true;
}

bool atomic(const QString &text, quint64 &value)
{
    static const QRegularExpression pattern("^(0|[1-9][0-9]{0,15})$");
    bool ok = false;
    value = text.toULongLong(&ok);
    return ok && pattern.match(text).hasMatch() && value <= 9007199254740991ULL;
}
}

namespace MonzeroTransactionEvidence {
QVariantMap validate(const QJsonObject &walletResult, const QJsonObject &blockResult,
                     const QJsonObject &nodeInfo, const QString &expectedTxid,
                     const QString &expectedAddress, const QString &expectedAmountText,
                     int requiredConfirmations)
{
    quint64 expectedAmount = 0, daemonHeight = 0;
    if (!hash64.match(expectedTxid).hasMatch() || expectedAddress.isEmpty()
        || expectedAddress.toUtf8().size() > 256 || !atomic(expectedAmountText, expectedAmount)
        || expectedAmount == 0 || requiredConfirmations < 1 || requiredConfirmations > 1000)
        return rejected("Invalid expected Monzero transaction policy");
    if (nodeInfo.value("status") != QJsonValue("OK")
        || nodeInfo.value("synchronized") != QJsonValue(true)
        || nodeInfo.value("untrusted") != QJsonValue(false)
        || nodeInfo.value("nettype") != QJsonValue("fakechain")
        || !exactInteger(nodeInfo.value("height"), daemonHeight) || daemonHeight == 0)
        return rejected("Untrusted or unsynchronized Monzero node evidence");
    if (!walletResult.value("transfers").isArray())
        return rejected("Missing Monzero wallet transfer evidence");

    quint64 paid = 0, transferHeight = 0, walletConfirmations = 0;
    bool any = false, allUnlocked = true, anyPool = false, anyIncoming = false;
    for (const auto value : walletResult.value("transfers").toArray()) {
        if (!value.isObject()) return rejected("Malformed Monzero wallet transfer");
        const QJsonObject transfer = value.toObject();
        if (transfer.value("txid") != QJsonValue(expectedTxid)
            || transfer.value("address") != QJsonValue(expectedAddress)
            || transfer.value("double_spend_seen") != QJsonValue(false))
            return rejected("Monzero wallet transfer identity or safety mismatch");
        const QString type = transfer.value("type").toString();
        if (type != "in" && type != "pool")
            return rejected("Monzero evidence is not an incoming transfer");
        anyPool = anyPool || type == "pool";
        anyIncoming = anyIncoming || type == "in";
        if (anyPool && anyIncoming) return rejected("Mixed Monzero transfer states");
        quint64 amount = 0, height = 0, confirmations = 0;
        if (!exactInteger(transfer.value("amount"), amount)
            || !exactInteger(transfer.value("height"), height)
            || !exactInteger(transfer.value("confirmations"), confirmations)
            || amount > std::numeric_limits<quint64>::max() - paid)
            return rejected("Malformed Monzero transfer amounts or heights");
        if (!any) { transferHeight = height; walletConfirmations = confirmations; }
        else if (height != transferHeight || confirmations != walletConfirmations)
            return rejected("Inconsistent Monzero transfer entries");
        any = true;
        paid += amount;
        allUnlocked = allUnlocked && transfer.value("locked") == QJsonValue(false);
    }
    if (!any || paid != expectedAmount)
        return rejected("Monzero destination amount mismatch");

    const quint64 tipHeight = daemonHeight - 1;
    if (anyPool) {
        if (!blockResult.isEmpty() || transferHeight != 0 || walletConfirmations != 0)
            return rejected("Inconsistent Monzero pool evidence");
        return {{"valid", true}, {"found", true}, {"transactionId", expectedTxid},
                {"blockHash", ""}, {"blockHeight", "0"}, {"confirmations", "0"},
                {"nodeHeight", QString::number(tipHeight)},
                {"requiredConfirmations", requiredConfirmations}, {"confirmed", false},
                {"spendable", false}, {"executable", false}};
    }
    if (transferHeight > tipHeight || walletConfirmations != daemonHeight - transferHeight)
        return rejected("Inconsistent Monzero confirmation arithmetic");
    if (blockResult.value("status") != QJsonValue("OK")
        || blockResult.value("untrusted") != QJsonValue(false)
        || !blockResult.value("block_header").isObject())
        return rejected("Untrusted Monzero block evidence");
    const QJsonObject header = blockResult.value("block_header").toObject();
    quint64 headerHeight = 0;
    const QString blockHash = header.value("hash").toString();
    if (!hash64.match(blockHash).hasMatch() || !exactInteger(header.value("height"), headerHeight)
        || headerHeight != transferHeight || header.value("orphan_status") != QJsonValue(false))
        return rejected("Inconsistent Monzero block header");
    bool included = blockResult.value("miner_tx_hash") == QJsonValue(expectedTxid);
    if (blockResult.value("tx_hashes").isArray()) {
        for (const auto tx : blockResult.value("tx_hashes").toArray())
            if (tx == QJsonValue(expectedTxid)) included = true;
    }
    if (!included) return rejected("Monzero transaction is absent from the claimed block");

    const bool confirmed = walletConfirmations >= static_cast<quint64>(requiredConfirmations);
    return {{"valid", true}, {"found", true}, {"transactionId", expectedTxid},
            {"blockHash", blockHash}, {"blockHeight", QString::number(transferHeight)},
            {"confirmations", QString::number(walletConfirmations)},
            {"nodeHeight", QString::number(tipHeight)},
            {"requiredConfirmations", requiredConfirmations}, {"confirmed", confirmed},
            {"spendable", confirmed && allUnlocked}, {"executable", false}};
}
}
