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
    static bool verify(const QByteArray &payload, const QByteArray &network,
                       const QByteArray &publicKey, const QByteArray &signature);
private:
    explicit OfferIdentity(const unsigned char *seed);
    unsigned char m_public[crypto_sign_PUBLICKEYBYTES];
    unsigned char m_secret[crypto_sign_SECRETKEYBYTES];
    mutable bool m_initialized;
};
#endif
