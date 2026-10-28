#include "SignedOffer.h"

#include <QCryptographicHash>
#include <QDataStream>
#include <QRegularExpression>
#include <QStringList>
#include <limits>

namespace {
const QRegularExpression identifier("^[a-z0-9][a-z0-9._-]{0,63}$");
const QRegularExpression uuid(
    "^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$");
const QRegularExpression integer("^[1-9][0-9]{0,19}$");
constexpr qint64 maximumLifetime = 86400;

QByteArray field(const QByteArray &value)
{
    QByteArray encoded;
    QDataStream stream(&encoded, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    stream << quint32(value.size());
    encoded += value;
    return encoded;
}

QByteArray payload(const SignedOffer::Terms &terms, QString &error)
{
    bool amountOk = false, priceOk = false;
    terms.amountAtomic.toULongLong(&amountOk);
    terms.priceAtomic.toULongLong(&priceOk);
    const QStringList pairs{"BTC/XMZ", "LTC/XMZ", "BCH/XMZ"};
    if (!identifier.match(terms.protocolId).hasMatch()
        || !identifier.match(terms.network).hasMatch()
        || !uuid.match(terms.offerId).hasMatch()
        || !pairs.contains(terms.pair)
        || (terms.side != "buy" && terms.side != "sell")
        || !amountOk || !priceOk || !integer.match(terms.amountAtomic).hasMatch()
        || !integer.match(terms.priceAtomic).hasMatch()
        || terms.createdAt < 0 || terms.expiresAt <= terms.createdAt
        || terms.createdAt > std::numeric_limits<qint64>::max() - maximumLifetime
        || terms.expiresAt - terms.createdAt > maximumLifetime) {
        error = "Invalid canonical signed-offer terms";
        return {};
    }
    QByteArray result("MONZERO-DEX-SIGNED-OFFER-V1\n");
    for (const QByteArray &value : {terms.protocolId.toUtf8(), terms.network.toUtf8(),
             terms.offerId.toUtf8(), terms.pair.toUtf8(), terms.side.toUtf8(),
             terms.amountAtomic.toLatin1(), terms.priceAtomic.toLatin1(),
             QByteArray::number(terms.createdAt), QByteArray::number(terms.expiresAt)})
        result += field(value);
    return result;
}

}

namespace SignedOffer {
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
    const QByteArray signature = identity.sign(canonical, terms.network.toUtf8());
    return verify(terms, identity.publicKey(), signature, terms.protocolId,
                  terms.network, terms.createdAt);
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
    if (canonical.isEmpty() || now < 0 || now < terms.createdAt
        || now >= terms.expiresAt || terms.protocolId != expectedProtocol
        || terms.network != expectedNetwork
        || !OfferIdentity::verify(canonical, expectedNetwork.toUtf8(),
                                  makerPublicKey, signature)) {
        evidence.m_details["error"] = error.isEmpty()
            ? "Signed offer authentication, deployment, or lifetime mismatch" : error;
        return evidence;
    }
    evidence.m_terms = terms;
    evidence.m_payload = canonical;
    evidence.m_makerPublicKey = makerPublicKey;
    evidence.m_signature = signature;
    QByteArray commitmentInput("MONZERO-DEX-SIGNED-OFFER-EVIDENCE-V1\n");
    for (const QByteArray &value : {canonical, makerPublicKey, signature})
        commitmentInput += field(value);
    evidence.m_commitment =
        QCryptographicHash::hash(commitmentInput, QCryptographicHash::Sha256);
    evidence.m_details = {{"valid", true}, {"authenticated", true},
        {"offerId", terms.offerId}, {"pair", terms.pair}, {"side", terms.side},
        {"amountAtomic", terms.amountAtomic}, {"priceAtomic", terms.priceAtomic},
        {"createdAt", QString::number(terms.createdAt)},
        {"expiresAt", QString::number(terms.expiresAt)},
        {"makerPublicKey", QString::fromLatin1(makerPublicKey.toHex())},
        {"signature", QString::fromLatin1(signature.toHex())},
        {"executable", false}};
    return evidence;
}
}
