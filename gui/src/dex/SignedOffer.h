#ifndef MONZERO_SIGNED_OFFER_H
#define MONZERO_SIGNED_OFFER_H

#include "OfferIdentity.h"

#include <QVariantMap>

namespace SignedOffer {
struct Terms {
    QString protocolId;
    QString network;
    QString offerId;
    QString pair;
    QString side;
    QString amountAtomic;
    QString priceAtomic;
    qint64 createdAt = 0;
    qint64 expiresAt = 0;
};

class Evidence final {
public:
    bool accepted() const { return !m_commitment.isEmpty(); }
    QByteArray commitment() const { return m_commitment; }
    QByteArray payload() const { return m_payload; }
    QByteArray makerPublicKey() const { return m_makerPublicKey; }
    QByteArray signature() const { return m_signature; }
    Terms terms() const { return m_terms; }
    QString protocolId() const { return m_terms.protocolId; }
    QString network() const { return m_terms.network; }
    QString offerId() const { return m_terms.offerId; }
    QString pair() const { return m_terms.pair; }
    QString side() const { return m_terms.side; }
    QString amountAtomic() const { return m_terms.amountAtomic; }
    QString priceAtomic() const { return m_terms.priceAtomic; }
    qint64 createdAt() const { return m_terms.createdAt; }
    qint64 expiresAt() const { return m_terms.expiresAt; }
    QVariantMap details() const { return m_details; }

private:
    friend Evidence create(const Terms &, const OfferIdentity &);
    friend Evidence verify(const Terms &, const QByteArray &, const QByteArray &,
                           const QString &, const QString &, qint64);
    Terms m_terms;
    QByteArray m_payload, m_makerPublicKey, m_signature, m_commitment;
    QVariantMap m_details;
};

Evidence create(const Terms &terms, const OfferIdentity &identity);
Evidence verify(const Terms &terms, const QByteArray &makerPublicKey,
                const QByteArray &signature, const QString &expectedProtocol,
                const QString &expectedNetwork, qint64 now);
}

#endif
