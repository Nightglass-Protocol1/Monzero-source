#include "RelayIngress.h"
#include "RelayWireCodec.h"
#include <QJsonDocument>

#include <QCryptographicHash>
#include <QDataStream>

namespace {
QByteArray field(const QByteArray &value)
{
    QByteArray encoded;
    QDataStream stream(&encoded, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    stream << quint32(value.size());
    encoded += value;
    return encoded;
}
}

namespace RelayIngress {
Coordinator::Coordinator(RelayReplayStore &replayStore,
                         OfferBookStore &offerBook, QString relayProtocol,
                         QString offerProtocol, QString network)
    : m_replayStore(replayStore), m_offerBook(offerBook),
      m_relayProtocol(std::move(relayProtocol)),
      m_offerProtocol(std::move(offerProtocol)), m_network(std::move(network))
{
}

namespace {
QByteArray messageBytes(const RelayEnvelope::Evidence &relay, const QJsonObject &artifact)
{
    const auto frame = RelayWireCodec::encode(relay);
    if (frame.isEmpty()) return {};
    const auto bytes = QJsonDocument(QJsonObject{{"version", 1},
        {"relay", QString::fromLatin1(frame.toHex())}, {"artifact", artifact}})
        .toJson(QJsonDocument::Compact);
    return bytes.size() <= Coordinator::maximumMessageSize ? bytes : QByteArray{};
}
}

QByteArray Coordinator::encodeMessage(const RelayEnvelope::Evidence &relay,
    const SignedOffer::Evidence &offer, qint64 now) const
{
    RelayEnvelope::Evidence verified;
    if (!offer.accepted() || !validateRelay(relay, "offer", offer.commitment(), now, verified))
        return {};
    if (now >= verified.terms().expiresAt || !SignedOffer::verify(offer.terms(),
        offer.makerPublicKey(), offer.signature(), m_offerProtocol, m_network, now).accepted()) return {};
    return messageBytes(verified, m_offerBook.encode(offer));
}

QByteArray Coordinator::encodeMessage(const RelayEnvelope::Evidence &relay,
    const SignedOfferCancellation::Evidence &cancellation, qint64 now) const
{
    RelayEnvelope::Evidence verified;
    if (!cancellation.accepted() || !validateRelay(relay, "cancellation",
            cancellation.commitment(), now, verified)) return {};
    if (now >= verified.terms().expiresAt || !SignedOfferCancellation::verify(cancellation.terms(),
        cancellation.makerPublicKey(), cancellation.signature(), m_offerProtocol, m_network, now).accepted()) return {};
    return messageBytes(verified, m_offerBook.encodeCancellation(cancellation));
}

RelayEnvelope::Evidence Coordinator::inspectMessage(const QByteArray &message, qint64 now) const
{
    if (message.isEmpty() || message.size() > maximumMessageSize) return {};
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(message, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()
        || document.toJson(QJsonDocument::Compact) != message) return {};
    const auto object = document.object();
    if (object.size() != 3 || object.value("version").toInt(-1) != 1
        || !object.value("relay").isString() || !object.value("artifact").isObject()) return {};
    const auto hex = object.value("relay").toString().toLatin1();
    const auto frame = QByteArray::fromHex(hex);
    if (frame.toHex() != hex) return {};
    const auto relay = RelayWireCodec::decode(frame, m_relayProtocol, m_network, now);
    if (!relay.accepted()) return {};
    const auto artifact = object.value("artifact").toObject();
    if (relay.terms().type == "offer") {
        SignedOffer::Evidence offer;
        if (!m_offerBook.decode(artifact, offer) || m_offerBook.encode(offer) != artifact) return {};
        return encodeMessage(relay, offer, now) == message ? relay : RelayEnvelope::Evidence{};
    }
    SignedOfferCancellation::Evidence cancellation;
    if (!m_offerBook.decodeCancellation(artifact, cancellation)
        || m_offerBook.encodeCancellation(cancellation) != artifact) return {};
    return encodeMessage(relay, cancellation, now) == message ? relay : RelayEnvelope::Evidence{};
}

Evidence Coordinator::ingestMessage(const QByteArray &message, qint64 now)
{
    const auto relay = inspectMessage(message, now);
    if (!relay.accepted()) return {};
    const auto artifact = QJsonDocument::fromJson(message).object().value("artifact").toObject();
    if (relay.terms().type == "offer") {
        SignedOffer::Evidence offer;
        if (!m_offerBook.decode(artifact, offer)) return {};
        return ingestOffer(relay, offer, now);
    }
    SignedOfferCancellation::Evidence cancellation;
    if (!m_offerBook.decodeCancellation(artifact, cancellation)) return {};
    return ingestCancellation(relay, cancellation, now);
}

Evidence Coordinator::recover(const QByteArray &senderPublicKey, qint64 now)
{
    const auto relay = m_replayStore.pendingEnvelope(senderPublicKey);
    if (!relay.accepted()) return {};
    const auto payload = m_replayStore.pendingArtifact(senderPublicKey);
    if (relay.terms().type == "offer") {
        SignedOffer::Evidence offer;
        if (!m_offerBook.decode(payload, offer)) return {};
        return ingestOffer(relay, offer, now);
    }
    SignedOfferCancellation::Evidence cancellation;
    if (!m_offerBook.decodeCancellation(payload, cancellation)) return {};
    return ingestCancellation(relay, cancellation, now);
}

bool Coordinator::validateRelay(const RelayEnvelope::Evidence &candidate,
                                const QString &type,
                                const QByteArray &artifactCommitment,
                                qint64 now,
                                RelayEnvelope::Evidence &verified) const
{
    if (!candidate.accepted() || artifactCommitment.size() != 32) return false;
    verified = RelayEnvelope::verify(candidate.terms(),
        candidate.senderPublicKey(), candidate.signature(), m_relayProtocol,
        m_network, now);
    if (!verified.accepted() && m_replayStore.pendingRecovery(candidate, now))
        verified = RelayEnvelope::verify(candidate.terms(),
            candidate.senderPublicKey(), candidate.signature(), m_relayProtocol,
            m_network, candidate.terms().createdAt);
    return verified.accepted()
        && verified.commitment() == candidate.commitment()
        && verified.terms().type == type
        && verified.terms().artifact == artifactCommitment;
}

Evidence Coordinator::result(const QString &action,
                             const RelayEnvelope::Evidence &relay,
                             const QByteArray &artifactCommitment) const
{
    Evidence evidence;
    QByteArray input("MONZERO-DEX-RELAY-INGRESS-V1\n");
    for (const QByteArray &value : {action.toUtf8(), relay.commitment(),
             artifactCommitment})
        input += field(value);
    evidence.m_commitment = QCryptographicHash::hash(
        input, QCryptographicHash::Sha256);
    evidence.m_relayCommitment = relay.commitment();
    evidence.m_artifactCommitment = artifactCommitment;
    evidence.m_action = action;
    evidence.m_details = {{"valid", true}, {"authenticated", true},
        {"replayProtected", true}, {"persisted", true},
        {"action", action}, {"executable", false}};
    return evidence;
}

Evidence Coordinator::ingestOffer(const RelayEnvelope::Evidence &relay,
                                  const SignedOffer::Evidence &offer,
                                  qint64 now)
{
    if (now < 0 || !m_replayStore.ready() || !m_offerBook.ready()
        || !offer.accepted())
        return {};
    const auto verifiedOffer = SignedOffer::verify(offer.terms(),
        offer.makerPublicKey(), offer.signature(), m_offerProtocol, m_network,
        now);
    RelayEnvelope::Evidence verifiedRelay;
    if (!verifiedOffer.accepted()
        || verifiedOffer.commitment() != offer.commitment()
        || !validateRelay(relay, "offer", verifiedOffer.commitment(), now,
                          verifiedRelay))
        return {};

    // Persist the authenticated sequence first. An exact retry remains valid,
    // allowing recovery if the process stops before the idempotent book write.
    if (!m_replayStore.prepare(verifiedRelay, now, m_offerBook.encode(verifiedOffer))
        || !m_offerBook.insert(verifiedOffer, now)
        || !m_replayStore.complete(verifiedRelay))
        return {};
    return result("offer", verifiedRelay, verifiedOffer.commitment());
}

Evidence Coordinator::ingestCancellation(
    const RelayEnvelope::Evidence &relay,
    const SignedOfferCancellation::Evidence &cancellation, qint64 now)
{
    if (now < 0 || !m_replayStore.ready() || !m_offerBook.ready()
        || !cancellation.accepted())
        return {};
    const auto verifiedCancellation = SignedOfferCancellation::verify(
        cancellation.terms(), cancellation.makerPublicKey(),
        cancellation.signature(), m_offerProtocol, m_network, now);
    RelayEnvelope::Evidence verifiedRelay;
    if (!verifiedCancellation.accepted()
        || verifiedCancellation.commitment() != cancellation.commitment()
        || !validateRelay(relay, "cancellation",
                          verifiedCancellation.commitment(), now,
                          verifiedRelay))
        return {};

    if (!m_replayStore.prepare(verifiedRelay, now,
            m_offerBook.encodeCancellation(verifiedCancellation))
        || !m_offerBook.cancel(verifiedCancellation, now)
        || !m_replayStore.complete(verifiedRelay))
        return {};
    return result("cancellation", verifiedRelay,
                  verifiedCancellation.commitment());
}
}
