#ifndef MONZERO_RELAY_INGRESS_H
#define MONZERO_RELAY_INGRESS_H

#include "OfferBookStore.h"
#include "RelayReplayStore.h"

#include <QVariantMap>

namespace RelayIngress {
class Evidence final {
public:
    bool accepted() const { return !m_commitment.isEmpty(); }
    QByteArray commitment() const { return m_commitment; }
    QByteArray relayCommitment() const { return m_relayCommitment; }
    QByteArray artifactCommitment() const { return m_artifactCommitment; }
    QString action() const { return m_action; }
    QVariantMap details() const { return m_details; }

private:
    friend class Coordinator;
    QByteArray m_commitment;
    QByteArray m_relayCommitment;
    QByteArray m_artifactCommitment;
    QString m_action;
    QVariantMap m_details;
};

class Coordinator final
{
public:
    Coordinator(RelayReplayStore &replayStore, OfferBookStore &offerBook,
                QString relayProtocol, QString offerProtocol, QString network);
    static constexpr int maximumMessageSize = 8192;
    QByteArray encodeMessage(const RelayEnvelope::Evidence &relay,
                             const SignedOffer::Evidence &offer, qint64 now) const;
    QByteArray encodeMessage(const RelayEnvelope::Evidence &relay,
        const SignedOfferCancellation::Evidence &cancellation, qint64 now) const;
    Evidence ingestMessage(const QByteArray &message, qint64 now);
    RelayEnvelope::Evidence inspectMessage(const QByteArray &message, qint64 now) const;
    Evidence ingestOffer(const RelayEnvelope::Evidence &relay,
                         const SignedOffer::Evidence &offer, qint64 now);
    Evidence recover(const QByteArray &senderPublicKey, qint64 now);
    Evidence ingestCancellation(const RelayEnvelope::Evidence &relay,
        const SignedOfferCancellation::Evidence &cancellation, qint64 now);

private:
    Evidence result(const QString &action,
                    const RelayEnvelope::Evidence &relay,
                    const QByteArray &artifactCommitment) const;
    bool validateRelay(const RelayEnvelope::Evidence &candidate,
                       const QString &type, const QByteArray &artifactCommitment,
                       qint64 now, RelayEnvelope::Evidence &verified) const;

    RelayReplayStore &m_replayStore;
    OfferBookStore &m_offerBook;
    QString m_relayProtocol;
    QString m_offerProtocol;
    QString m_network;
};
}

#endif
