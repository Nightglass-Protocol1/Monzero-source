#include "NativeTradeQuote.h"
#include "OfferIdentity.h"
#include "DexUtils.h"
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>
#include <QUuid>

namespace NativeTradeQuote {
QVariantMap displayAmounts(const QJsonObject &terms, qint64 now)
{
    if (encode(terms, now).isEmpty()) return {};
    const auto number = [&](const char *key) { return terms.value(key).toString().toULongLong(); };
    // BTC/XMZ = (satoshis / 10^8) / (atoms / 10^11).
    // Validated Bitcoin range makes multiplication by 1000 fit in quint64.
    const quint64 numerator = number("btcSatoshis") * 1000;
    const quint64 denominator = number("xmzAtoms");
    QString rate = QString::number(numerator / denominator);
    quint64 remainder = numerator % denominator;
    QString fraction;
    for (int place = 0; place < 12 && remainder; ++place) {
        quint64 next = 0;
        int digit = 0;
        // Compute remainder * 10 / denominator without overflowing uint64.
        for (int i = 0; i < 10; ++i) {
            if (next >= denominator - remainder) { next -= denominator - remainder; ++digit; }
            else next += remainder;
        }
        fraction += QChar('0' + digit);
        remainder = next;
    }
    while (fraction.endsWith('0')) fraction.chop(1);
    if (!fraction.isEmpty()) rate += '.' + fraction;
    if (remainder) rate = rate == "0" ? "< 0.000000000001" : QString::fromUtf8("≈ ") + rate;
    return {{"btcAmount", DexUtils::formatAtomic(number("btcSatoshis"), 8)},
        {"xmzAmount", DexUtils::formatAtomic(number("xmzAtoms"), 11)}, {"btcPerXmz", rate},
        {"btcFeeLimit", DexUtils::formatAtomic(number("btcNativeFeeMaxSatoshis"), 8)},
        {"xmzFeeLimit", DexUtils::formatAtomic(number("xmzNativeFeeMaxAtoms"), 11)}};
}
QByteArray signatureDomain() { return QByteArrayLiteral("monzero-native-btc-xmz-quote-v1"); }
QByteArray btcSignatureDomain() { return signatureDomain() + "-btc-funder"; }
QByteArray xmzSignatureDomain() { return signatureDomain() + "-xmz-funder"; }

bool verifyForDeployment(const QByteArray &payload, qint64 now,
    const QByteArray &btcSignature, const QByteArray &xmzSignature,
    const QByteArray &expectedBtcFunder, const QByteArray &expectedXmzFunder,
    const DeploymentPolicy &p, QJsonObject &output)
{
    QJsonObject terms;
    if (!verifySignatures(payload, now, btcSignature, xmzSignature,
        expectedBtcFunder, expectedXmzFunder, terms)) return false;
    const auto number = [&](const char *key) { return terms.value(key).toString().toULongLong(); };
    if (terms.value("btcGenesis") != p.btcGenesis || terms.value("xmzGenesis") != p.xmzGenesis
        || terms.value("btcNetwork") != p.btcNetwork || terms.value("xmzNetwork") != p.xmzNetwork
        || number("btcConfirmations") != p.btcConfirmations
        || number("xmzConfirmations") != p.xmzConfirmations
        || number("cancelBlocks") != p.cancelBlocks || number("punishBlocks") != p.punishBlocks
        || p.maximumBtcPrincipal == 0 || p.maximumXmzPrincipal == 0
        || p.maximumBtcNativeFee == 0 || p.maximumXmzNativeFee == 0
        || number("btcSatoshis") > p.maximumBtcPrincipal || number("xmzAtoms") > p.maximumXmzPrincipal
        || number("btcNativeFeeMaxSatoshis") > p.maximumBtcNativeFee
        || number("xmzNativeFeeMaxAtoms") > p.maximumXmzNativeFee) return false;
    output = terms;
    return true;
}

bool verifySignatures(const QByteArray &payload, qint64 now,
    const QByteArray &btcSignature, const QByteArray &xmzSignature,
    const QByteArray &expectedBtcFunder, const QByteArray &expectedXmzFunder,
    QJsonObject &output)
{
    QJsonObject terms;
    if (expectedBtcFunder.size() != 32 || expectedXmzFunder.size() != 32
        || expectedBtcFunder == expectedXmzFunder || !decode(payload, now, terms)) return false;
    if (terms.value("btcFunder").toString().toLatin1() != expectedBtcFunder.toHex()
        || terms.value("xmzFunder").toString().toLatin1() != expectedXmzFunder.toHex()
        || !OfferIdentity::verify(payload, btcSignatureDomain(), expectedBtcFunder, btcSignature)
        || !OfferIdentity::verify(payload, xmzSignatureDomain(), expectedXmzFunder, xmzSignature)) return false;
    output = terms;
    return true;
}

QByteArray encode(const QJsonObject &t, qint64 now)
{
    const QSet<QString> fields{
        "schema", "quoteId", "swapId", "btcFunder", "xmzFunder",
        "btcGenesis", "xmzGenesis", "btcNetwork", "xmzNetwork",
        "btcSatoshis", "xmzAtoms", "appFeeXmzAtoms", "btcNativeFeeMaxSatoshis",
        "xmzNativeFeeMaxAtoms", "btcPayout", "btcRefund", "xmzPayout", "xmzRefund",
        "btcConfirmations", "xmzConfirmations", "cancelBlocks", "punishBlocks", "expiresAt"
    };
    if (now < 0 || t.size() != fields.size()) return {};
    for (auto it = t.begin(); it != t.end(); ++it)
        if (!fields.contains(it.key()) || !it.value().isString()) return {};
    const auto value = [&](const char *name) { return t.value(QLatin1String(name)).toString(); };
    if (value("schema") != QString::fromLatin1(signatureDomain())
        || value("appFeeXmzAtoms") != "0") return {};
    for (const auto name : {"quoteId", "swapId"}) {
        const QUuid id(value(name));
        if (id.isNull() || id.toString(QUuid::WithoutBraces) != value(name)) return {};
    }
    const QRegularExpression hash("^[0-9a-f]{64}\\z");
    for (const auto name : {"btcFunder", "xmzFunder", "btcGenesis", "xmzGenesis"})
        if (!hash.match(value(name)).hasMatch() || value(name) == QString(64, '0')) return {};
    if (value("btcFunder") == value("xmzFunder") || value("btcGenesis") == value("xmzGenesis")) return {};
    const QRegularExpression network("^[a-z0-9][a-z0-9._-]{0,63}\\z");
    for (const auto name : {"btcNetwork", "xmzNetwork"})
        if (!network.match(value(name)).hasMatch()) return {};
    // Only bounded textual destinations here. Chain-specific decoding and
    // network matching must occur before quote acceptance or engine entry.
    const QRegularExpression address("^[!-~]{1,256}\\z");
    for (const auto name : {"btcPayout", "btcRefund", "xmzPayout", "xmzRefund"})
        if (!address.match(value(name)).hasMatch()) return {};
    const QRegularExpression integer("^[1-9][0-9]{0,19}\\z");
    for (const auto name : {"btcSatoshis", "xmzAtoms", "btcNativeFeeMaxSatoshis",
         "xmzNativeFeeMaxAtoms", "btcConfirmations", "xmzConfirmations",
         "cancelBlocks", "punishBlocks", "expiresAt"}) {
        bool ok = false;
        value(name).toULongLong(&ok);
        if (!ok || !integer.match(value(name)).hasMatch()) return {};
    }
    const auto number = [&](const char *name) { return value(name).toULongLong(); };
    if (number("btcSatoshis") > 2100000000000000ULL
        || number("btcNativeFeeMaxSatoshis") > 2100000000000000ULL
        || number("btcConfirmations") > 1000 || number("xmzConfirmations") > 1000
        || number("cancelBlocks") > 65535 || number("punishBlocks") > 65535
        || number("cancelBlocks") <= number("btcConfirmations")
        || number("punishBlocks") <= number("btcConfirmations")
        || number("expiresAt") <= static_cast<quint64>(now)
        || number("expiresAt") - static_cast<quint64>(now) > 3600) return {};
    const auto bytes = QJsonDocument(t).toJson(QJsonDocument::Compact);
    return bytes.size() <= 8192 ? bytes : QByteArray{};
}

bool decode(const QByteArray &payload, qint64 now, QJsonObject &output)
{
    if (payload.isEmpty() || payload.size() > 8192) return false;
    const auto doc = QJsonDocument::fromJson(payload);
    if (!doc.isObject() || encode(doc.object(), now) != payload) return false;
    output = doc.object();
    return true;
}

QByteArray commitment(const QJsonObject &terms, qint64 now)
{
    const auto bytes = encode(terms, now);
    if (bytes.isEmpty()) return {};
    return QCryptographicHash::hash(signatureDomain() + '\0' + bytes, QCryptographicHash::Sha256);
}
}
