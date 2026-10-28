#ifndef MONZERO_RELAY_ENVELOPE_H
#define MONZERO_RELAY_ENVELOPE_H

#include "OfferIdentity.h"

#include <QVariantMap>

namespace RelayEnvelope {
struct Terms {
    QString protocol;
    QString network;
    QString type;
    quint64 sequence = 0;
    qint64 createdAt = 0;
    qint64 expiresAt = 0;
    QByteArray artifact;
};

class Evidence final {
public:
    bool accepted() const { return !m_commitment.isEmpty(); }
    QByteArray commitment() const { return m_commitment; }
    QByteArray payload() const { return m_payload; }
    QByteArray senderPublicKey() const { return m_senderPublicKey; }
    QByteArray signature() const { return m_signature; }
    Terms terms() const { return m_terms; }
    QVariantMap details() const { return m_details; }

private:
    friend Evidence create(const Terms &, const OfferIdentity &);
    friend Evidence verify(const Terms &, const QByteArray &, const QByteArray &,
                           const QString &, const QString &, qint64);
    Terms m_terms;
    QByteArray m_payload;
    QByteArray m_senderPublicKey;
    QByteArray m_signature;
    QByteArray m_commitment;
    QVariantMap m_details;
};

Evidence create(const Terms &terms, const OfferIdentity &identity);
Evidence verify(const Terms &terms, const QByteArray &senderPublicKey,
                const QByteArray &signature, const QString &expectedProtocol,
                const QString &expectedNetwork, qint64 now);
}

#endif
