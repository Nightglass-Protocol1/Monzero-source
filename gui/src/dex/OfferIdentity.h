#ifndef MONZERO_OFFER_IDENTITY_H
#define MONZERO_OFFER_IDENTITY_H

#include <QByteArray>
#include <sodium.h>
#include <memory>

class OfferIdentity {
public:
    OfferIdentity();
    ~OfferIdentity();
    OfferIdentity(const OfferIdentity &) = delete;
    OfferIdentity &operator=(const OfferIdentity &) = delete;
    QByteArray publicKey() const;
    QByteArray encrypt(const QByteArray &passphrase) const;
    static std::unique_ptr<OfferIdentity> decrypt(const QByteArray &blob,
                                                const QByteArray &passphrase);
    QByteArray sign(const QByteArray &payload, const QByteArray &network) const;
    // Private participant message; never publish through the offer relay.
    QByteArray signLitecoinNegotiation(const QByteArray &frame) const;
    QByteArray signLitecoinRecovery(const QByteArray &frame) const;
    QByteArray signLitecoinRedeem(const QByteArray &frame) const;
    // Sensitive BCH adaptor: only a chain-gated private session may supply it.
    QByteArray signBchSuccess(const QByteArray &frame) const;
    static bool verify(const QByteArray &payload, const QByteArray &network,
                       const QByteArray &publicKey, const QByteArray &signature);
private:
    QByteArray signFramed(const QByteArray &message) const;
    explicit OfferIdentity(const unsigned char *seed);
    unsigned char m_public[crypto_sign_PUBLICKEYBYTES];
    unsigned char m_secret[crypto_sign_SECRETKEYBYTES];
    mutable bool m_initialized;
};
#endif
