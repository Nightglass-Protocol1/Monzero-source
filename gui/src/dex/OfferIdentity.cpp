#include "OfferIdentity.h"
#include <stdexcept>

namespace {
// Version pins algorithm and costs: Argon2id v1.3, 3 passes, 64 MiB.
const QByteArray identityMagic("MONZERO-DEX-IDENTITY-ARGON2ID13-3-64M-XCHACHA20-V1\n");
template<size_t N> struct Secret {
    unsigned char bytes[N]{};
    ~Secret() { sodium_memzero(bytes, N); }
};
bool derive(unsigned char *key, const QByteArray &passphrase, const unsigned char *salt)
{
    return !passphrase.isEmpty() && passphrase.size() <= 1024
        && crypto_pwhash(key, crypto_aead_xchacha20poly1305_ietf_KEYBYTES,
            passphrase.constData(), passphrase.size(), salt, 3, 64 * 1024 * 1024,
            crypto_pwhash_ALG_ARGON2ID13) == 0;
}

QByteArray message(const QByteArray &payload, const QByteArray &network)
{
    if (payload.isEmpty() || payload.size() > 16384 || network.isEmpty() || network.size() > 128)
        return {};
    return QByteArray("MONZERO-DEX-OFFER-V1\n") + QByteArray::number(network.size()) + ":" + network
        + QByteArray::number(payload.size()) + ":" + payload;
}
}

OfferIdentity::OfferIdentity() : m_initialized(false)
{
    if (sodium_init() < 0) throw std::runtime_error("Offer signing initialization failed");
    crypto_sign_keypair(m_public, m_secret);
    m_initialized = true;
}

OfferIdentity::~OfferIdentity()
{
    if (m_initialized) {
        sodium_memzero(m_secret, sizeof(m_secret));
        m_initialized = false;
    }
}

OfferIdentity::OfferIdentity(const unsigned char *seed) : m_initialized(false)
{
    if (sodium_init() < 0 || crypto_sign_seed_keypair(m_public, m_secret, seed) != 0)
        throw std::runtime_error("Offer identity restoration failed");
    m_initialized = true;
}

QByteArray OfferIdentity::encrypt(const QByteArray &passphrase) const
{
    if (!m_initialized || passphrase.isEmpty() || passphrase.size() > 1024) return {};
    unsigned char salt[crypto_pwhash_SALTBYTES];
    unsigned char nonce[crypto_aead_xchacha20poly1305_ietf_NPUBBYTES];
    randombytes_buf(salt, sizeof salt);
    randombytes_buf(nonce, sizeof nonce);
    Secret<crypto_aead_xchacha20poly1305_ietf_KEYBYTES> key;
    Secret<crypto_sign_SEEDBYTES> seed;
    if (!derive(key.bytes, passphrase, salt)
        || crypto_sign_ed25519_sk_to_seed(seed.bytes, m_secret) != 0) return {};
    QByteArray header = identityMagic;
    header.append(reinterpret_cast<const char *>(salt), sizeof salt);
    header.append(reinterpret_cast<const char *>(nonce), sizeof nonce);
    QByteArray ciphertext(crypto_sign_SEEDBYTES + crypto_aead_xchacha20poly1305_ietf_ABYTES, '\0');
    unsigned long long size = 0;
    if (crypto_aead_xchacha20poly1305_ietf_encrypt(
            reinterpret_cast<unsigned char *>(ciphertext.data()), &size,
            seed.bytes, sizeof seed.bytes,
            reinterpret_cast<const unsigned char *>(header.constData()), header.size(),
            nullptr, nonce, key.bytes) != 0 || size != static_cast<unsigned>(ciphertext.size())) return {};
    return header + ciphertext;
}

std::unique_ptr<OfferIdentity> OfferIdentity::decrypt(const QByteArray &blob,
                                                    const QByteArray &passphrase)
{
    const int headerSize = identityMagic.size() + crypto_pwhash_SALTBYTES
        + crypto_aead_xchacha20poly1305_ietf_NPUBBYTES;
    const int cipherSize = crypto_sign_SEEDBYTES + crypto_aead_xchacha20poly1305_ietf_ABYTES;
    if (blob.size() != headerSize + cipherSize || !blob.startsWith(identityMagic)
        || passphrase.isEmpty() || passphrase.size() > 1024 || sodium_init() < 0) return {};
    const auto *bytes = reinterpret_cast<const unsigned char *>(blob.constData());
    const auto *salt = bytes + identityMagic.size();
    const auto *nonce = salt + crypto_pwhash_SALTBYTES;
    Secret<crypto_aead_xchacha20poly1305_ietf_KEYBYTES> key;
    Secret<crypto_sign_SEEDBYTES> seed;
    unsigned long long size = 0;
    if (!derive(key.bytes, passphrase, salt)
        || crypto_aead_xchacha20poly1305_ietf_decrypt(seed.bytes, &size, nullptr,
            bytes + headerSize, cipherSize, bytes, headerSize, nonce, key.bytes) != 0
        || size != sizeof seed.bytes) return {};
    return std::unique_ptr<OfferIdentity>(new OfferIdentity(seed.bytes));
}

QByteArray OfferIdentity::publicKey() const
{
    return QByteArray(reinterpret_cast<const char *>(m_public), sizeof(m_public));
}

QByteArray OfferIdentity::sign(const QByteArray &payload, const QByteArray &network) const
{
    const auto bytes = message(payload, network);
    if (bytes.isEmpty() || !m_initialized) return {};
    QByteArray signature(crypto_sign_BYTES, '\0');
    if (crypto_sign_detached(reinterpret_cast<unsigned char *>(signature.data()), nullptr,
        reinterpret_cast<const unsigned char *>(bytes.constData()), bytes.size(), m_secret) != 0) return {};
    return signature;
}

bool OfferIdentity::verify(const QByteArray &payload, const QByteArray &network,
                           const QByteArray &publicKey, const QByteArray &signature)
{
    const auto bytes = message(payload, network);
    if (sodium_init() < 0 || bytes.isEmpty() || publicKey.size() != crypto_sign_PUBLICKEYBYTES
        || signature.size() != crypto_sign_BYTES) return false;
    return crypto_sign_verify_detached(reinterpret_cast<const unsigned char *>(signature.constData()),
        reinterpret_cast<const unsigned char *>(bytes.constData()), bytes.size(),
        reinterpret_cast<const unsigned char *>(publicKey.constData())) == 0;
}
