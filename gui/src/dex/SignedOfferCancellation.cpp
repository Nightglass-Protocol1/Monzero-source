#include "SignedOfferCancellation.h"

#include <QCryptographicHash>
#include <QDataStream>
#include <QRegularExpression>

namespace {
const QRegularExpression identifier("^[a-z0-9][a-z0-9._-]{0,63}$");
const QRegularExpression uuid(
    "^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$");

QByteArray field(const QByteArray &value)
{
    QByteArray encoded;
    QDataStream stream(&encoded, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    stream << quint32(value.size());
    encoded += value;
    return encoded;
}

QByteArray payload(const SignedOfferCancellation::Terms &terms, QString &error)
{
    if (!identifier.match(terms.protocolId).hasMatch()
        || !identifier.match(terms.network).hasMatch()
        || !uuid.match(terms.offerId).hasMatch()
        || terms.offerCommitment.size() != 32 || terms.cancelledAt < 0) {
        error = "Invalid canonical offer-cancellation terms";
        return {};
    }
    QByteArray result("MONZERO-DEX-OFFER-CANCELLATION-V1\n");
    for (const QByteArray &value : {terms.protocolId.toUtf8(), terms.network.toUtf8(),
             terms.offerId.toUtf8(), terms.offerCommitment,
             QByteArray::number(terms.cancelledAt)})
        result += field(value);
    return result;
}
}

namespace SignedOfferCancellation {
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
        identity.sign(canonical, terms.network.toUtf8()), terms.protocolId,
        terms.network, terms.cancelledAt);
}

Evidence verify(const Terms &terms, const QByteArray &makerPublicKey,
                const QByteArray &signature, const QString &expectedProtocol,
                const QString &expectedNetwork, qint64 now)
{
    Evidence evidence;
    evidence.m_details = {{"valid", false}, {"authenticated", false},
                          {"executable", false}};
    QString error;
    const QByteArray canonical = payload(terms, error);
    if (canonical.isEmpty() || now < terms.cancelledAt
        || terms.protocolId != expectedProtocol || terms.network != expectedNetwork
        || !OfferIdentity::verify(canonical, expectedNetwork.toUtf8(),
                                  makerPublicKey, signature)) {
        evidence.m_details["error"] = error.isEmpty()
            ? "Offer cancellation authentication, deployment, or time mismatch" : error;
        return evidence;
    }
    evidence.m_terms = terms;
    evidence.m_payload = canonical;
    evidence.m_makerPublicKey = makerPublicKey;
    evidence.m_signature = signature;
    QByteArray commitmentInput("MONZERO-DEX-OFFER-CANCELLATION-EVIDENCE-V1\n");
    for (const QByteArray &value : {canonical, makerPublicKey, signature})
        commitmentInput += field(value);
    evidence.m_commitment =
        QCryptographicHash::hash(commitmentInput, QCryptographicHash::Sha256);
    evidence.m_details = {{"valid", true}, {"authenticated", true},
        {"offerId", terms.offerId},
        {"offerCommitment", QString::fromLatin1(terms.offerCommitment.toHex())},
        {"cancelledAt", QString::number(terms.cancelledAt)},
        {"makerPublicKey", QString::fromLatin1(makerPublicKey.toHex())},
        {"signature", QString::fromLatin1(signature.toHex())},
        {"executable", false}};
    return evidence;
}
}
