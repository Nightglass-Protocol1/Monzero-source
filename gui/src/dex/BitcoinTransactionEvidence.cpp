#include "BitcoinTransactionEvidence.h"
#include <QJsonArray>
#include <QRegularExpression>
#include <QSet>
#include <cmath>
#include <limits>

namespace {
const quint64 satoshisPerBitcoin = 100000000;
const quint64 maximumBitcoinSatoshis = 21000000ULL * satoshisPerBitcoin;
const QRegularExpression hash64("^[0-9a-f]{64}$");

QVariantMap rejected(const QString &error)
{
    return {{"valid", false}, {"found", false}, {"confirmed", false},
            {"executable", false}, {"error", error}};
}

bool atomic(const QString &text, quint64 &value)
{
    static const QRegularExpression pattern("^(0|[1-9][0-9]{0,15})$");
    bool ok = false;
    value = text.toULongLong(&ok);
    return ok && pattern.match(text).hasMatch() && value <= maximumBitcoinSatoshis;
}

bool exactJsonInteger(const QJsonValue &input, quint64 &value)
{
    if (!input.isDouble()) return false;
    const double number = input.toDouble();
    if (!std::isfinite(number) || number < 0 || number != std::floor(number)
        || number > 9007199254740991.0) return false;
    value = static_cast<quint64>(number);
    return true;
}

bool bitcoinAmount(const QJsonValue &input, quint64 &satoshis)
{
    if (!input.isDouble()) return false;
    const double coins = input.toDouble();
    if (!std::isfinite(coins) || coins < 0 || coins > 21000000.0) return false;
    const double scaled = coins * static_cast<double>(satoshisPerBitcoin);
    const double rounded = std::round(scaled);
    // Bitcoin Core emits no more than eight decimal places. This strict distance
    // check rejects values that cannot represent a whole satoshi after JSON parsing.
    if (std::fabs(scaled - rounded) > 0.000001 || rounded > maximumBitcoinSatoshis) return false;
    satoshis = static_cast<quint64>(rounded);
    return true;
}
}

namespace BitcoinTransactionEvidence {
QVariantMap validate(const QJsonObject &transaction, const QJsonObject &header,
                     const QString &expectedTxid, const QString &expectedAddress,
                     const QString &expectedAmountText, const QString &nodeHeightText,
                     int requiredConfirmations)
{
    quint64 expectedAmount = 0, nodeHeight = 0;
    if (!hash64.match(expectedTxid).hasMatch() || expectedAddress.isEmpty()
        || expectedAddress.toUtf8().size() > 128 || !atomic(expectedAmountText, expectedAmount)
        || expectedAmount == 0 || !atomic(nodeHeightText, nodeHeight) || requiredConfirmations < 1
        || requiredConfirmations > 1000)
        return rejected("Invalid expected Bitcoin transaction policy");
    if (transaction.value("txid") != QJsonValue(expectedTxid)
        || !transaction.value("vout").isArray())
        return rejected("Bitcoin transaction identity or outputs mismatch");

    quint64 paid = 0;
    QSet<quint64> indexes;
    for (const auto value : transaction.value("vout").toArray()) {
        if (!value.isObject()) return rejected("Malformed Bitcoin output");
        const QJsonObject output = value.toObject();
        quint64 amount = 0, index = 0;
        if (!bitcoinAmount(output.value("value"), amount)
            || !exactJsonInteger(output.value("n"), index) || indexes.contains(index)
            || !output.value("scriptPubKey").isObject())
            return rejected("Malformed Bitcoin output");
        indexes.insert(index);
        if (output.value("scriptPubKey").toObject().value("address") == QJsonValue(expectedAddress)) {
            if (amount > std::numeric_limits<quint64>::max() - paid)
                return rejected("Bitcoin destination amount overflow");
            paid += amount;
        }
    }
    if (paid != expectedAmount) return rejected("Bitcoin destination amount mismatch");

    const QString blockHash = transaction.value("blockhash").toString();
    quint64 confirmations = 0;
    if (blockHash.isEmpty()) {
        if (!header.isEmpty() || (!transaction.value("confirmations").isUndefined()
            && (!exactJsonInteger(transaction.value("confirmations"), confirmations)
                || confirmations != 0)))
            return rejected("Inconsistent Bitcoin mempool evidence");
        return {{"valid", true}, {"found", true}, {"transactionId", expectedTxid},
                {"blockHash", ""}, {"blockHeight", "0"}, {"confirmations", "0"},
                {"nodeHeight", nodeHeightText}, {"requiredConfirmations", requiredConfirmations},
                {"confirmed", false}, {"executable", false}};
    }

    quint64 blockHeight = 0, headerConfirmations = 0, transactionConfirmations = 0;
    if (!hash64.match(blockHash).hasMatch() || header.value("hash") != QJsonValue(blockHash)
        || header.value("confirmations").toDouble(-1) < 1
        || !exactJsonInteger(header.value("height"), blockHeight)
        || !exactJsonInteger(header.value("confirmations"), headerConfirmations)
        || !exactJsonInteger(transaction.value("confirmations"), transactionConfirmations)
        || blockHeight > nodeHeight || transactionConfirmations != headerConfirmations
        || transactionConfirmations != nodeHeight - blockHeight + 1
        || (transaction.contains("in_active_chain")
            && transaction.value("in_active_chain") != QJsonValue(true)))
        return rejected("Inconsistent Bitcoin block evidence");
    return {{"valid", true}, {"found", true}, {"transactionId", expectedTxid},
            {"blockHash", blockHash}, {"blockHeight", QString::number(blockHeight)},
            {"confirmations", QString::number(transactionConfirmations)},
            {"nodeHeight", nodeHeightText}, {"requiredConfirmations", requiredConfirmations},
            {"confirmed", transactionConfirmations >= static_cast<quint64>(requiredConfirmations)},
            {"executable", false}};
}
}
