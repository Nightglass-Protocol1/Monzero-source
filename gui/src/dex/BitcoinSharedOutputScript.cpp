#include "BitcoinSharedOutputScript.h"

#include <QCryptographicHash>
#include <QRegularExpression>
#include <openssl/ec.h>
#include <openssl/obj_mac.h>
#include <memory>

namespace {
const QRegularExpression canonicalHex("^[0-9a-f]+$");

QVariantMap rejected(const QString &error)
{
    return {{"valid", false}, {"scriptVerified", false},
            {"executable", false}, {"error", error}};
}

bool compressedSecp256k1Key(const QString &hex, QByteArray &bytes)
{
    if (hex.size() != 66 || !canonicalHex.match(hex).hasMatch()
        || (!hex.startsWith("02") && !hex.startsWith("03"))) return false;
    bytes = QByteArray::fromHex(hex.toLatin1());
    if (bytes.size() != 33) return false;

    using Group = std::unique_ptr<EC_GROUP, decltype(&EC_GROUP_free)>;
    using Point = std::unique_ptr<EC_POINT, decltype(&EC_POINT_free)>;
    Group group(EC_GROUP_new_by_curve_name(NID_secp256k1), EC_GROUP_free);
    if (!group) return false;
    Point point(EC_POINT_new(group.get()), EC_POINT_free);
    if (!point
        || EC_POINT_oct2point(group.get(), point.get(),
                              reinterpret_cast<const unsigned char *>(bytes.constData()),
                              static_cast<size_t>(bytes.size()), nullptr) != 1
        || EC_POINT_is_at_infinity(group.get(), point.get()) == 1
        || EC_POINT_is_on_curve(group.get(), point.get(), nullptr) != 1) return false;
    return true;
}
}

namespace BitcoinSharedOutputScript {
QVariantMap validateProgram(const QString &scriptPubKeyHex,
                            const QString &firstPublicKeyHex,
                            const QString &secondPublicKeyHex)
{
    QByteArray first, second;
    if (!compressedSecp256k1Key(firstPublicKeyHex, first)
        || !compressedSecp256k1Key(secondPublicKeyHex, second) || first == second)
        return rejected("Invalid Bitcoin shared-output public keys");

    // COMIT reference descriptor c:and_v(v:pk(A),pk_k(B)):
    // PUSH33 A CHECKSIGVERIFY PUSH33 B CHECKSIG.
    QByteArray expected;
    expected.reserve(70);
    expected.append(char(0x21)); expected.append(first); expected.append(char(0xad));
    expected.append(char(0x21)); expected.append(second); expected.append(char(0xac));
    const QByteArray witnessHash = QCryptographicHash::hash(expected, QCryptographicHash::Sha256);
    const QString expectedScriptPubKey = QString::fromLatin1(
        (QByteArray("\x00\x20", 2) + witnessHash).toHex());
    if (scriptPubKeyHex != expectedScriptPubKey)
        return rejected("Bitcoin P2WSH output does not commit to the shared witness script");

    return {{"valid", true}, {"scriptVerified", true},
            {"witnessScriptHex", QString::fromLatin1(expected.toHex())},
            {"scriptPubKeyHex", expectedScriptPubKey},
            {"witnessScriptHash", QString::fromLatin1(witnessHash.toHex())},
            {"executable", false}};
}

QVariantMap validate(const QString &witnessScriptHex, const QString &scriptPubKeyHex,
                     const QString &firstPublicKeyHex, const QString &secondPublicKeyHex)
{
    const QVariantMap program = validateProgram(
        scriptPubKeyHex, firstPublicKeyHex, secondPublicKeyHex);
    if (!program.value("valid").toBool()) return program;
    if (witnessScriptHex.size() != 140 || !canonicalHex.match(witnessScriptHex).hasMatch()
        || witnessScriptHex != program.value("witnessScriptHex").toString())
        return rejected("Bitcoin shared witness script does not match ordered keys");
    return program;
}
}
