#include "BitcoinEcdsaSignatureVerifier.h"

#include <QByteArray>
#include <QRegularExpression>
#include <openssl/bn.h>
#include <openssl/ec.h>
#include <openssl/ecdsa.h>
#include <openssl/evp.h>
#include <openssl/core_names.h>
#include <openssl/obj_mac.h>
#include <openssl/params.h>
#include <memory>

namespace {
const QRegularExpression digestPattern("^[0-9a-f]{64}$");
const QRegularExpression keyPattern("^0[23][0-9a-f]{64}$");
const QRegularExpression hexPattern("^([0-9a-f][0-9a-f])+$");

QVariantMap rejected(const QString &error)
{
    return {{"valid", false}, {"signatureVerified", false},
            {"executable", false}, {"error", error}};
}
}

namespace BitcoinEcdsaSignatureVerifier {
QVariantMap verifyAll(const QString &digestHex,
                      const QString &compressedPublicKeyHex,
                      const QString &signatureWithHashTypeHex)
{
    if (!digestPattern.match(digestHex).hasMatch()
        || !keyPattern.match(compressedPublicKeyHex).hasMatch()
        || signatureWithHashTypeHex.size() < 18
        || signatureWithHashTypeHex.size() > 146
        || !hexPattern.match(signatureWithHashTypeHex).hasMatch()
        || !signatureWithHashTypeHex.endsWith("01"))
        return rejected("Invalid Bitcoin signature encoding or sighash type");

    const QByteArray digest = QByteArray::fromHex(digestHex.toLatin1());
    const QByteArray keyBytes = QByteArray::fromHex(compressedPublicKeyHex.toLatin1());
    QByteArray der = QByteArray::fromHex(signatureWithHashTypeHex.chopped(2).toLatin1());

    using Group = std::unique_ptr<EC_GROUP, decltype(&EC_GROUP_free)>;
    using Point = std::unique_ptr<EC_POINT, decltype(&EC_POINT_free)>;
    using Signature = std::unique_ptr<ECDSA_SIG, decltype(&ECDSA_SIG_free)>;
    using Number = std::unique_ptr<BIGNUM, decltype(&BN_free)>;
    using Pkey = std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)>;
    using PkeyContext = std::unique_ptr<EVP_PKEY_CTX, decltype(&EVP_PKEY_CTX_free)>;

    Group group(EC_GROUP_new_by_curve_name(NID_secp256k1), EC_GROUP_free);
    if (!group) return rejected("Unable to initialize secp256k1");
    Point point(EC_POINT_new(group.get()), EC_POINT_free);
    if (!point || EC_POINT_oct2point(group.get(), point.get(),
            reinterpret_cast<const unsigned char *>(keyBytes.constData()),
            static_cast<size_t>(keyBytes.size()), nullptr) != 1
        || EC_POINT_is_at_infinity(group.get(), point.get()) == 1
        || EC_POINT_is_on_curve(group.get(), point.get(), nullptr) != 1)
        return rejected("Invalid Bitcoin signature public key");

    const unsigned char *cursor = reinterpret_cast<const unsigned char *>(der.constData());
    const unsigned char *const end = cursor + der.size();
    Signature signature(d2i_ECDSA_SIG(nullptr, &cursor, der.size()), ECDSA_SIG_free);
    if (!signature || cursor != end) return rejected("Non-canonical Bitcoin DER signature");
    const int encodedLength = i2d_ECDSA_SIG(signature.get(), nullptr);
    QByteArray canonical(encodedLength, Qt::Uninitialized);
    unsigned char *encoded = reinterpret_cast<unsigned char *>(canonical.data());
    if (encodedLength <= 0 || i2d_ECDSA_SIG(signature.get(), &encoded) != encodedLength
        || canonical != der)
        return rejected("Non-canonical Bitcoin DER signature");

    const BIGNUM *r = nullptr, *s = nullptr;
    ECDSA_SIG_get0(signature.get(), &r, &s);
    Number order(BN_new(), BN_free), halfOrder(BN_new(), BN_free);
    if (!r || !s || !order || !halfOrder
        || EC_GROUP_get_order(group.get(), order.get(), nullptr) != 1
        || BN_copy(halfOrder.get(), order.get()) == nullptr
        || BN_rshift1(halfOrder.get(), halfOrder.get()) != 1
        || BN_is_zero(r) || BN_is_negative(r) || BN_cmp(r, order.get()) >= 0
        || BN_is_zero(s) || BN_is_negative(s) || BN_cmp(s, halfOrder.get()) > 0)
        return rejected("Bitcoin signature is out of range or not low-S");

    PkeyContext importContext(EVP_PKEY_CTX_new_from_name(nullptr, "EC", nullptr),
                              EVP_PKEY_CTX_free);
    EVP_PKEY *rawKey = nullptr;
    char groupName[] = "secp256k1";
    OSSL_PARAM parameters[] = {
        OSSL_PARAM_construct_utf8_string(OSSL_PKEY_PARAM_GROUP_NAME, groupName, 0),
        OSSL_PARAM_construct_octet_string(OSSL_PKEY_PARAM_PUB_KEY,
                                          const_cast<char *>(keyBytes.constData()),
                                          static_cast<size_t>(keyBytes.size())),
        OSSL_PARAM_construct_end()};
    if (!importContext || EVP_PKEY_fromdata_init(importContext.get()) != 1
        || EVP_PKEY_fromdata(importContext.get(), &rawKey, EVP_PKEY_PUBLIC_KEY,
                             parameters) != 1)
        return rejected("Unable to initialize Bitcoin verification key");
    Pkey key(rawKey, EVP_PKEY_free);
    PkeyContext verifyContext(EVP_PKEY_CTX_new(key.get(), nullptr), EVP_PKEY_CTX_free);
    if (!verifyContext || EVP_PKEY_verify_init(verifyContext.get()) != 1
        || EVP_PKEY_CTX_set_signature_md(verifyContext.get(), EVP_sha256()) != 1)
        return rejected("Unable to initialize Bitcoin signature verification");
    const int verified = EVP_PKEY_verify(
        verifyContext.get(),
        reinterpret_cast<const unsigned char *>(der.constData()), der.size(),
        reinterpret_cast<const unsigned char *>(digest.constData()), digest.size());
    if (verified != 1) return rejected("Bitcoin signature does not match digest and key");

    return {{"valid", true}, {"signatureVerified", true},
            {"sighashType", QString("ALL")}, {"executable", false}};
}
}
