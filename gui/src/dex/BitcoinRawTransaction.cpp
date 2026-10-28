#include "BitcoinRawTransaction.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QRegularExpression>
#include <algorithm>
#include <limits>

namespace {
const QRegularExpression evenHex("^([0-9a-f][0-9a-f])+$");

quint32 little32(const QByteArray &bytes, int offset)
{
    const auto *p = reinterpret_cast<const unsigned char *>(bytes.constData() + offset);
    return quint32(p[0]) | (quint32(p[1]) << 8) | (quint32(p[2]) << 16)
        | (quint32(p[3]) << 24);
}

quint64 little64(const QByteArray &bytes, int offset)
{
    quint64 value = 0;
    for (int i = 7; i >= 0; --i)
        value = (value << 8) | static_cast<unsigned char>(bytes[offset + i]);
    return value;
}

bool take(const QByteArray &bytes, int &offset, int count, QByteArray &value)
{
    if (count < 0 || offset < 0 || count > bytes.size() - offset) return false;
    value = bytes.mid(offset, count);
    offset += count;
    return true;
}

bool compactSize(const QByteArray &bytes, int &offset, quint64 &value,
                 QByteArray *encoding = nullptr)
{
    if (offset >= bytes.size()) return false;
    const int start = offset;
    const quint8 first = static_cast<quint8>(bytes[offset++]);
    int width = 0;
    if (first < 0xfd) value = first;
    else {
        width = first == 0xfd ? 2 : first == 0xfe ? 4 : 8;
        if (width > bytes.size() - offset) return false;
        value = 0;
        for (int i = width - 1; i >= 0; --i)
            value = (value << 8) | static_cast<quint8>(bytes[offset + i]);
        offset += width;
        if ((width == 2 && value < 0xfd)
            || (width == 4 && value <= std::numeric_limits<quint16>::max())
            || (width == 8 && value <= std::numeric_limits<quint32>::max())) return false;
    }
    if (encoding) *encoding = bytes.mid(start, offset - start);
    return true;
}

QByteArray doubleSha256(const QByteArray &bytes)
{
    return QCryptographicHash::hash(
        QCryptographicHash::hash(bytes, QCryptographicHash::Sha256),
        QCryptographicHash::Sha256);
}

QString displayedHash(QByteArray digest)
{
    std::reverse(digest.begin(), digest.end());
    return QString::fromLatin1(digest.toHex());
}

BitcoinRawTransaction::Result rejected(const QString &error)
{
    BitcoinRawTransaction::Result result;
    result.error = error;
    return result;
}
}

namespace BitcoinRawTransaction {
Result parseRecovery(const QByteArray &rawHex)
{
    if (rawHex.isEmpty() || rawHex.size() > 8 * 1024 * 1024
        || !evenHex.match(QString::fromLatin1(rawHex)).hasMatch())
        return rejected("Invalid canonical raw Bitcoin transaction hex");
    const QByteArray raw = QByteArray::fromHex(rawHex);
    if (raw.size() < 64) return rejected("Truncated Bitcoin recovery transaction");

    int offset = 0;
    QByteArray versionBytes;
    if (!take(raw, offset, 4, versionBytes) || little32(versionBytes, 0) != 2
        || offset + 2 > raw.size() || raw[offset++] != '\0' || raw[offset++] != '\1')
        return rejected("Recovery transaction must use version 2 and SegWit marker/flag");

    QByteArray inputCountBytes;
    quint64 inputCount = 0;
    if (!compactSize(raw, offset, inputCount, &inputCountBytes) || inputCount != 1)
        return rejected("Recovery transaction must contain exactly one input");
    const int inputStart = offset;
    QByteArray previousWire, voutBytes, scriptSig, sequenceBytes;
    quint64 scriptLength = 0;
    if (!take(raw, offset, 32, previousWire) || !take(raw, offset, 4, voutBytes)
        || !compactSize(raw, offset, scriptLength) || scriptLength != 0
        || !take(raw, offset, 0, scriptSig) || !take(raw, offset, 4, sequenceBytes))
        return rejected("Malformed Bitcoin recovery input");
    const QByteArray serializedInputs = raw.mid(inputStart, offset - inputStart);

    QByteArray outputCountBytes;
    quint64 outputCount = 0;
    if (!compactSize(raw, offset, outputCount, &outputCountBytes) || outputCount != 1)
        return rejected("Recovery transaction must contain exactly one output");
    const int outputStart = offset;
    QByteArray amountBytes, outputScript;
    quint64 outputScriptLength = 0;
    if (!take(raw, offset, 8, amountBytes)
        || !compactSize(raw, offset, outputScriptLength)
        || outputScriptLength == 0 || outputScriptLength > 10000
        || !take(raw, offset, static_cast<int>(outputScriptLength), outputScript))
        return rejected("Malformed Bitcoin recovery output");
    const quint64 amount = little64(amountBytes, 0);
    if (amount > 21000000ULL * 100000000ULL)
        return rejected("Bitcoin recovery output exceeds monetary range");
    const QByteArray serializedOutputs = raw.mid(outputStart, offset - outputStart);

    quint64 witnessItems = 0;
    if (!compactSize(raw, offset, witnessItems) || witnessItems != 3)
        return rejected("Recovery witness must contain exactly three items");
    QStringList witness;
    for (quint64 i = 0; i < witnessItems; ++i) {
        quint64 length = 0;
        QByteArray item;
        if (!compactSize(raw, offset, length) || length == 0 || length > 10000
            || !take(raw, offset, static_cast<int>(length), item))
            return rejected("Malformed Bitcoin recovery witness item");
        witness.append(QString::fromLatin1(item.toHex()));
    }
    QByteArray locktimeBytes;
    if (!take(raw, offset, 4, locktimeBytes) || offset != raw.size()
        || little32(locktimeBytes, 0) != 0)
        return rejected("Invalid recovery locktime or trailing transaction bytes");

    QByteArray previousDisplay = previousWire;
    std::reverse(previousDisplay.begin(), previousDisplay.end());
    const quint32 vout = little32(voutBytes, 0);
    const quint32 sequence = little32(sequenceBytes, 0);
    const QJsonObject input{{"txid", QString::fromLatin1(previousDisplay.toHex())},
        {"vout", static_cast<double>(vout)},
        {"scriptSig", QJsonObject{{"asm", ""}, {"hex", ""}}},
        {"sequence", static_cast<double>(sequence)},
        {"txinwitness", QJsonArray::fromStringList(witness)}};
    const QJsonObject output{{"value", static_cast<double>(amount) / 100000000.0},
        {"n", 0}, {"scriptPubKey", QJsonObject{
            {"hex", QString::fromLatin1(outputScript.toHex())}}}};
    QJsonObject transaction{{"version", 2}, {"locktime", 0},
        {"vin", QJsonArray{input}}, {"vout", QJsonArray{output}}};

    const QByteArray base = versionBytes + inputCountBytes + serializedInputs
        + outputCountBytes + serializedOutputs + locktimeBytes;
    Result result;
    result.valid = true;
    result.transaction = transaction;
    result.witness = witness;
    result.txid = displayedHash(doubleSha256(base));
    result.wtxid = displayedHash(doubleSha256(raw));
    const quint64 weight = static_cast<quint64>(base.size()) * 3
        + static_cast<quint64>(raw.size());
    result.virtualSize = (weight + 3) / 4;
    result.transaction["txid"] = result.txid;
    result.transaction["hash"] = result.wtxid;
    return result;
}
}
