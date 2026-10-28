#include "BitcoinSegwitV0Sighash.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QRegularExpression>
#include <algorithm>
#include <cmath>
#include <limits>

namespace {
constexpr quint64 satsPerBitcoin = 100000000ULL;
const QRegularExpression hash64("^[0-9a-f]{64}$");
const QRegularExpression hexPattern("^([0-9a-f][0-9a-f])+$");

QVariantMap rejected(const QString &error)
{
    return {{"valid", false}, {"sighashVerified", false},
            {"executable", false}, {"error", error}};
}

void le32(QByteArray &out, quint32 n)
{
    for (int i = 0; i < 4; ++i) out.append(char((n >> (8 * i)) & 0xff));
}
void le64(QByteArray &out, quint64 n)
{
    for (int i = 0; i < 8; ++i) out.append(char((n >> (8 * i)) & 0xff));
}
void compact(QByteArray &out, quint64 n)
{
    if (n < 253) out.append(char(n));
    else if (n <= 0xffff) { out.append(char(253)); out.append(char(n)); out.append(char(n >> 8)); }
    else { out.append(char(254)); le32(out, static_cast<quint32>(n)); }
}
QByteArray dsha(const QByteArray &bytes)
{
    return QCryptographicHash::hash(
        QCryptographicHash::hash(bytes, QCryptographicHash::Sha256),
        QCryptographicHash::Sha256);
}
bool u32(const QJsonValue &v, quint32 &out)
{
    if (!v.isDouble()) return false;
    const double n = v.toDouble();
    if (!std::isfinite(n) || n < 0 || n != std::floor(n)
        || n > std::numeric_limits<quint32>::max()) return false;
    out = static_cast<quint32>(n); return true;
}
bool amount(const QJsonValue &v, quint64 &out)
{
    if (!v.isDouble()) return false;
    const double scaled = v.toDouble() * static_cast<double>(satsPerBitcoin);
    const double rounded = std::round(scaled);
    if (!std::isfinite(scaled) || scaled < 0 || std::fabs(scaled - rounded) > 0.000001
        || rounded > 21000000.0 * satsPerBitcoin) return false;
    out = static_cast<quint64>(rounded); return true;
}
bool scriptBytes(const QJsonValue &v, QByteArray &out)
{
    if (!v.isObject()) return false;
    const QJsonValue scriptValue = v.toObject().value("scriptPubKey");
    if (!scriptValue.isObject()) return false;
    const QString hex = scriptValue.toObject().value("hex").toString();
    if (hex.isEmpty() || hex.size() > 20000 || !hexPattern.match(hex).hasMatch()) return false;
    out = QByteArray::fromHex(hex.toLatin1()); return !out.isEmpty();
}
}

namespace BitcoinSegwitV0Sighash {
QVariantMap computeAll(const QJsonObject &tx, int inputIndex,
                       const QString &witnessScriptHex, const QString &amountText)
{
    quint32 version = 0, locktime = 0;
    bool amountOk = false;
    const quint64 parentAmount = amountText.toULongLong(&amountOk);
    if (!u32(tx.value("version"), version) || (version != 1 && version != 2)
        || !u32(tx.value("locktime"), locktime) || !tx.value("vin").isArray()
        || !tx.value("vout").isArray() || !amountOk
        || QString::number(parentAmount) != amountText || parentAmount == 0
        || parentAmount > 21000000ULL * satsPerBitcoin
        || witnessScriptHex.isEmpty() || witnessScriptHex.size() > 20000
        || !hexPattern.match(witnessScriptHex).hasMatch())
        return rejected("Invalid SegWit v0 sighash inputs");
    const QJsonArray inputs = tx.value("vin").toArray(), outputs = tx.value("vout").toArray();
    if (inputIndex < 0 || inputIndex >= inputs.size() || inputs.isEmpty() || outputs.isEmpty()
        || inputs.size() > 1000 || outputs.size() > 1000)
        return rejected("Invalid SegWit v0 sighash input index or count");

    QByteArray prevouts, sequences, serializedOutputs;
    for (const QJsonValue &v : inputs) {
        if (!v.isObject()) return rejected("Malformed sighash transaction input");
        const QJsonObject in = v.toObject();
        const QString txid = in.value("txid").toString(); quint32 n = 0, sequence = 0;
        if (!hash64.match(txid).hasMatch() || !u32(in.value("vout"), n)
            || !u32(in.value("sequence"), sequence)
            || !in.value("scriptSig").isObject()
            || !in.value("scriptSig").toObject().value("hex").toString().isEmpty()
            || in.contains("txinwitness"))
            return rejected("Malformed sighash outpoint or sequence");
        QByteArray id = QByteArray::fromHex(txid.toLatin1()); std::reverse(id.begin(), id.end());
        prevouts += id; le32(prevouts, n); le32(sequences, sequence);
    }
    for (const QJsonValue &v : outputs) {
        quint64 n = 0; QByteArray script;
        if (!v.isObject() || !amount(v.toObject().value("value"), n) || !scriptBytes(v, script))
            return rejected("Malformed sighash transaction output");
        le64(serializedOutputs, n); compact(serializedOutputs, script.size());
        serializedOutputs += script;
    }
    const QJsonObject current = inputs[inputIndex].toObject();
    QByteArray currentId = QByteArray::fromHex(current.value("txid").toString().toLatin1());
    std::reverse(currentId.begin(), currentId.end()); quint32 vout = 0, sequence = 0;
    u32(current.value("vout"), vout); u32(current.value("sequence"), sequence);
    const QByteArray witness = QByteArray::fromHex(witnessScriptHex.toLatin1());
    QByteArray preimage; le32(preimage, version); preimage += dsha(prevouts);
    preimage += dsha(sequences); preimage += currentId; le32(preimage, vout);
    compact(preimage, witness.size()); preimage += witness; le64(preimage, parentAmount);
    le32(preimage, sequence); preimage += dsha(serializedOutputs); le32(preimage, locktime);
    le32(preimage, 1); // SIGHASH_ALL
    return {{"valid", true}, {"sighashVerified", true},
            {"digestHex", QString::fromLatin1(dsha(preimage).toHex())},
            {"preimageHex", QString::fromLatin1(preimage.toHex())},
            {"executable", false}};
}
}
