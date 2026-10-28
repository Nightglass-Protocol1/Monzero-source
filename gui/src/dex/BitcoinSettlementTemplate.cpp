#include "BitcoinSettlementTemplate.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonValue>
#include <QRegularExpression>
#include <QSet>
#include <cmath>
#include <limits>

namespace {
const quint64 satoshisPerBitcoin = 100000000ULL;
const quint64 maximumBitcoinSatoshis = 21000000ULL * satoshisPerBitcoin;
const QRegularExpression hash64("^[0-9a-f]{64}$");
const QRegularExpression lowercaseHex("^[0-9a-f]+$");
const QRegularExpression p2wshHex("^0020[0-9a-f]{64}$");

QVariantMap rejected(const QString &error)
{
    return {{"valid", false}, {"committed", false}, {"executable", false}, {"error", error}};
}

bool unsignedInteger(const QString &text, quint64 &value, quint64 maximum)
{
    static const QRegularExpression integer("^(0|[1-9][0-9]{0,19})$");
    bool ok = false;
    value = text.toULongLong(&ok);
    return ok && integer.match(text).hasMatch() && value <= maximum;
}

bool exactJsonInteger(const QJsonValue &input, quint64 &value, quint64 maximum)
{
    if (!input.isDouble()) return false;
    const double number = input.toDouble();
    if (!std::isfinite(number) || number < 0 || number != std::floor(number)
        || number > static_cast<double>(maximum)) return false;
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
    if (std::fabs(scaled - rounded) > 0.000001 || rounded > maximumBitcoinSatoshis)
        return false;
    satoshis = static_cast<quint64>(rounded);
    return true;
}

bool validScriptHex(const QString &value)
{
    return !value.isEmpty() && value.size() <= 10000 && value.size() % 2 == 0
        && lowercaseHex.match(value).hasMatch();
}

void append(QByteArray &payload, const QByteArray &value)
{
    payload += QByteArray::number(value.size()) + ':' + value;
}

bool unsignedInput(const QJsonObject &input)
{
    if (input.contains("coinbase") || !input.value("scriptSig").isObject()) return false;
    const QJsonObject script = input.value("scriptSig").toObject();
    if (script.value("hex") != QJsonValue("") || (!script.value("asm").isUndefined()
        && script.value("asm") != QJsonValue(""))) return false;
    return !input.contains("txinwitness")
        || (input.value("txinwitness").isArray()
            && input.value("txinwitness").toArray().isEmpty());
}

struct Output {
    quint64 index = 0;
    quint64 amount = 0;
    QString script;
    QString address;
    QString type;
};

bool output(const QJsonValue &value, Output &parsed)
{
    if (!value.isObject()) return false;
    const QJsonObject object = value.toObject();
    if (!exactJsonInteger(object.value("n"), parsed.index,
                          std::numeric_limits<quint32>::max())
        || !bitcoinAmount(object.value("value"), parsed.amount)
        || !object.value("scriptPubKey").isObject()) return false;
    const QJsonObject script = object.value("scriptPubKey").toObject();
    parsed.script = script.value("hex").toString();
    parsed.address = script.value("address").toString();
    parsed.type = script.value("type").toString();
    return validScriptHex(parsed.script);
}
}

namespace BitcoinSettlementTemplate {
namespace {
QByteArray calculateCommitment(const QJsonObject &transaction, bool requireUnsigned)
{
    quint64 version = 0, locktime = 0;
    if (!exactJsonInteger(transaction.value("version"), version,
                          std::numeric_limits<qint32>::max())
        || version != 2
        || !exactJsonInteger(transaction.value("locktime"), locktime,
                             std::numeric_limits<quint32>::max())
        || locktime != 0 || !transaction.value("vin").isArray()
        || !transaction.value("vout").isArray()) return {};
    const QJsonArray inputs = transaction.value("vin").toArray();
    const QJsonArray outputs = transaction.value("vout").toArray();
    if (inputs.isEmpty() || outputs.isEmpty() || inputs.size() > 1000 || outputs.size() > 1000)
        return {};

    QByteArray payload("MONZERO-DEX-BTC-UNSIGNED-TEMPLATE-V1\n");
    append(payload, QByteArray::number(version));
    append(payload, QByteArray::number(locktime));
    append(payload, QByteArray::number(inputs.size()));
    QSet<QString> outpoints;
    for (const QJsonValue &value : inputs) {
        if (!value.isObject()) return {};
        const QJsonObject input = value.toObject();
        const QString txid = input.value("txid").toString();
        quint64 vout = 0, sequence = 0;
        if (!hash64.match(txid).hasMatch() || input.contains("coinbase")
            || (requireUnsigned && !unsignedInput(input))
            || !exactJsonInteger(input.value("vout"), vout,
                                 std::numeric_limits<quint32>::max())
            || !exactJsonInteger(input.value("sequence"), sequence,
                                 std::numeric_limits<quint32>::max())) return {};
        const QString outpoint = txid + ':' + QString::number(vout);
        if (outpoints.contains(outpoint)) return {};
        outpoints.insert(outpoint);
        append(payload, txid.toLatin1());
        append(payload, QByteArray::number(vout));
        append(payload, QByteArray::number(sequence));
    }
    append(payload, QByteArray::number(outputs.size()));
    QSet<quint64> indexes;
    quint64 expectedIndex = 0;
    for (const QJsonValue &value : outputs) {
        Output parsed;
        if (!output(value, parsed) || parsed.index != expectedIndex
            || indexes.contains(parsed.index)) return {};
        indexes.insert(parsed.index);
        append(payload, QByteArray::number(parsed.index));
        append(payload, QByteArray::number(parsed.amount));
        append(payload, parsed.script.toLatin1());
        ++expectedIndex;
    }
    return QCryptographicHash::hash(payload, QCryptographicHash::Sha256);
}
}

QByteArray commitment(const QJsonObject &transaction)
{
    return calculateCommitment(transaction, true);
}

QByteArray observedCommitment(const QJsonObject &transaction)
{
    return calculateCommitment(transaction, false);
}

QVariantMap validateLock(const QJsonObject &transaction,
                         const QByteArray &expectedCommitment,
                         const QString &expectedAddress, const QString &expectedScriptHex,
                         const QString &expectedAmountText, const QString &actualFeeText,
                         const QString &maximumFeeText)
{
    quint64 expectedAmount = 0, actualFee = 0, maximumFee = 0;
    const QByteArray actualCommitment = commitment(transaction);
    if (expectedCommitment.size() != 32 || expectedCommitment == QByteArray(32, '\0')
        || actualCommitment.isEmpty() || actualCommitment != expectedCommitment
        || expectedAddress.isEmpty() || expectedAddress.toUtf8().size() > 128
        || !p2wshHex.match(expectedScriptHex).hasMatch()
        || !unsignedInteger(expectedAmountText, expectedAmount, maximumBitcoinSatoshis)
        || expectedAmount == 0
        || !unsignedInteger(actualFeeText, actualFee, maximumBitcoinSatoshis)
        || !unsignedInteger(maximumFeeText, maximumFee, maximumBitcoinSatoshis)
        || actualFee == 0 || maximumFee == 0 || actualFee > maximumFee)
        return rejected("Invalid Bitcoin lock template policy or commitment");

    const QJsonArray outputs = transaction.value("vout").toArray();
    if (outputs.size() < 1 || outputs.size() > 2)
        return rejected("Bitcoin lock must have one settlement output and optional change");
    int matches = 0;
    for (const QJsonValue &value : outputs) {
        Output parsed;
        if (!output(value, parsed)) return rejected("Malformed Bitcoin lock output");
        if (parsed.script == expectedScriptHex || parsed.address == expectedAddress) {
            if (parsed.script != expectedScriptHex || parsed.address != expectedAddress
                || parsed.type != "witness_v0_scripthash" || parsed.amount != expectedAmount)
                return rejected("Bitcoin lock destination mismatch");
            ++matches;
        }
    }
    if (matches != 1) return rejected("Missing or ambiguous Bitcoin lock output");
    return {{"valid", true}, {"committed", true},
            {"commitment", QString::fromLatin1(actualCommitment.toHex())},
            {"feeAtomic", actualFeeText}, {"executable", false}};
}

QVariantMap validateSpend(const QJsonObject &transaction,
                          const QByteArray &expectedCommitment,
                          const QString &expectedParentTxid,
                          const QString &expectedParentVoutText,
                          const QString &expectedParentAmountText,
                          const QString &expectedRecipient,
                          const QString &expectedRecipientScriptHex,
                          const QString &expectedSequenceText,
                          const QString &maximumFeeText)
{
    quint64 parentVout = 0, parentAmount = 0, sequence = 0, maximumFee = 0;
    const QByteArray actualCommitment = commitment(transaction);
    if (expectedCommitment.size() != 32 || expectedCommitment == QByteArray(32, '\0')
        || actualCommitment.isEmpty() || actualCommitment != expectedCommitment
        || !hash64.match(expectedParentTxid).hasMatch()
        || !unsignedInteger(expectedParentVoutText, parentVout,
                            std::numeric_limits<quint32>::max())
        || !unsignedInteger(expectedParentAmountText, parentAmount, maximumBitcoinSatoshis)
        || parentAmount == 0 || expectedRecipient.isEmpty()
        || expectedRecipient.toUtf8().size() > 128
        || !validScriptHex(expectedRecipientScriptHex)
        || !unsignedInteger(expectedSequenceText, sequence,
                            std::numeric_limits<quint32>::max())
        || !unsignedInteger(maximumFeeText, maximumFee, maximumBitcoinSatoshis)
        || maximumFee == 0)
        return rejected("Invalid Bitcoin spend template policy or commitment");
    const QJsonArray inputs = transaction.value("vin").toArray();
    const QJsonArray outputs = transaction.value("vout").toArray();
    if (inputs.size() != 1 || outputs.size() != 1)
        return rejected("Bitcoin settlement spend must have one input and one output");
    const QJsonObject input = inputs[0].toObject();
    quint64 actualVout = 0, actualSequence = 0;
    if (input.value("txid") != QJsonValue(expectedParentTxid)
        || !exactJsonInteger(input.value("vout"), actualVout,
                             std::numeric_limits<quint32>::max())
        || !exactJsonInteger(input.value("sequence"), actualSequence,
                             std::numeric_limits<quint32>::max())
        || actualVout != parentVout || actualSequence != sequence)
        return rejected("Bitcoin settlement outpoint or sequence mismatch");
    Output destination;
    if (!output(outputs[0], destination) || destination.index != 0
        || destination.address != expectedRecipient
        || destination.script != expectedRecipientScriptHex)
        return rejected("Bitcoin settlement destination mismatch");
    if (destination.amount >= parentAmount)
        return rejected("Bitcoin settlement fee must be positive");
    const quint64 fee = parentAmount - destination.amount;
    if (fee > maximumFee) return rejected("Bitcoin settlement fee exceeds signed ceiling");
    return {{"valid", true}, {"committed", true},
            {"commitment", QString::fromLatin1(actualCommitment.toHex())},
            {"feeAtomic", QString::number(fee)}, {"executable", false}};
}
}
