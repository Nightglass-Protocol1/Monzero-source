#include "RelayEnvelope.h"

#include <QCryptographicHash>
#include <QDataStream>
#include <QRegularExpression>
#include <limits>

namespace {
const QRegularExpression identifier("^[a-z0-9][a-z0-9._:-]{0,63}$");
constexpr qint64 maximumLifetime = 3600;

QByteArray field(const QByteArray &value)
{
    QByteArray encoded;
    QDataStream stream(&encoded, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    stream << quint32(value.size());
    encoded += value;
    return encoded;
}

QByteArray payload(const RelayEnvelope::Terms &terms, QString &error)
{
    if (!identifier.match(terms.protocol).hasMatch()
        || !identifier.match(terms.network).hasMatch()
        || (terms.type != "offer" && terms.type != "cancellation")
        || terms.sequence == 0 || terms.createdAt < 0
        || terms.expiresAt <= terms.createdAt
        || terms.createdAt > std::numeric_limits<qint64>::max() - maximumLifetime
        || terms.expiresAt - terms.createdAt > maximumLifetime
        || terms.artifact.size() != QCryptographicHash::hashLength(
               QCryptographicHash::Sha256)
        || terms.artifact == QByteArray(32, '\0')) {
        error = "Invalid canonical relay-envelope terms";
        return {};
    }

    QByteArray result("MONZERO-DEX-RELAY-V1\n");
    for (const QByteArray &value : {terms.protocol.toUtf8(),
             terms.network.toUtf8(), terms.type.toUtf8(),
             QByteArray::number(terms.sequence),
             QByteArray::number(terms.createdAt),
             QByteArray::number(terms.expiresAt), terms.artifact})
        result += field(value);
    return result;
}
}

namespace RelayEnvelope {
Evidence create(const Terms &terms, const OfferIdentity &identity)
{
    QString error;
    const QByteArray canonical = payload(terms, error);
    if (canonical.isEmpty()) {
        Evidence evidence;
        evidence.m_details = {{"valid", false}, {"authenticated", false},
                              {"executable", false}, {"error", error}};
        return evidence;
    }
    return verify(terms, identity.publicKey(),
                  identity.sign(canonical, terms.network.toUtf8()),
                  terms.protocol, terms.network, terms.createdAt);
}

Evidence verify(const Terms &terms, const QByteArray &senderPublicKey,
                const QByteArray &signature, const QString &expectedProtocol,
                const QString &expectedNetwork, qint64 now)
{
    Evidence evidence;
    evidence.m_details = {{"valid", false}, {"authenticated", false},
                          {"executable", false}};
    QString error;
    const QByteArray canonical = payload(terms, error);
    if (canonical.isEmpty() || now < 0 || now < terms.createdAt
        || now >= terms.expiresAt || terms.protocol != expectedProtocol
        || terms.network != expectedNetwork
        || !OfferIdentity::verify(canonical, expectedNetwork.toUtf8(),
                                  senderPublicKey, signature)) {
        evidence.m_details["error"] = error.isEmpty()
            ? "Relay authentication, deployment, or lifetime mismatch" : error;
        return evidence;
    }

    evidence.m_terms = terms;
    evidence.m_payload = canonical;
    evidence.m_senderPublicKey = senderPublicKey;
    evidence.m_signature = signature;
    QByteArray commitmentInput("MONZERO-DEX-RELAY-EVIDENCE-V1\n");
    for (const QByteArray &value : {canonical, senderPublicKey, signature})
        commitmentInput += field(value);
    evidence.m_commitment = QCryptographicHash::hash(
        commitmentInput, QCryptographicHash::Sha256);
    evidence.m_details = {{"valid", true}, {"authenticated", true},
        {"protocol", terms.protocol}, {"network", terms.network},
        {"type", terms.type}, {"sequence", QString::number(terms.sequence)},
        {"createdAt", QString::number(terms.createdAt)},
        {"expiresAt", QString::number(terms.expiresAt)},
        {"artifact", QString::fromLatin1(terms.artifact.toHex())},
        {"senderPublicKey", QString::fromLatin1(senderPublicKey.toHex())},
        {"signature", QString::fromLatin1(signature.toHex())},
        {"executable", false}};
    return evidence;
}
}
