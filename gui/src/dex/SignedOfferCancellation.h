#ifndef MONZERO_SIGNED_OFFER_CANCELLATION_H
#define MONZERO_SIGNED_OFFER_CANCELLATION_H

#include "OfferIdentity.h"

#include <QVariantMap>

namespace SignedOfferCancellation {
struct Terms {
    QString protocolId;
    QString network;
    QString offerId;
    QByteArray offerCommitment;
    qint64 cancelledAt = 0;
};

class Evidence final {
public:
    bool accepted() const { return !m_commitment.isEmpty(); }
    QByteArray commitment() const { return m_commitment; }
    QByteArray payload() const { return m_payload; }
    QByteArray makerPublicKey() const { return m_makerPublicKey; }
    QByteArray signature() const { return m_signature; }
    Terms terms() const { return m_terms; }
    QString offerId() const { return m_terms.offerId; }
    QByteArray offerCommitment() const { return m_terms.offerCommitment; }
    qint64 cancelledAt() const { return m_terms.cancelledAt; }
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
