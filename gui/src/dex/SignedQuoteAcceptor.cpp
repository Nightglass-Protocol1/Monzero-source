#include "SignedQuoteAcceptor.h"
#include "OfferIdentity.h"
#include <QJsonDocument>
#include <QJsonObject>

namespace {
QVariantMap rejected(const QString &error)
{
    return {{"valid", false}, {"accepted", false}, {"executable", false}, {"error", error}};
}
}

SignedQuoteAcceptor::SignedQuoteAcceptor(const QString &replayPath, QString protocolId,
                                         QString payNetwork, QString receiveNetwork,
                                         QString xmzNetwork, QByteArray signatureDomain)
    : m_store(replayPath), m_protocolId(std::move(protocolId)),
      m_payNetwork(std::move(payNetwork)), m_receiveNetwork(std::move(receiveNetwork)),
      m_xmzNetwork(std::move(xmzNetwork)), m_signatureDomain(std::move(signatureDomain)) {}

QuoteReplayStore &SignedQuoteAcceptor::store() { return m_store; }

RecoveredMutualQuote SignedQuoteAcceptor::recoverMutualQuote(const QByteArray &makerKey,
    const QByteArray &nonce, qint64 now) const
{
    RecoveredMutualQuote result;
    if (!m_store.ready() || now < 0) return result;
    const auto id = QuoteReplayStore::replayId(makerKey, nonce);
    if (id.isEmpty()) return result;
    for (const auto value : m_store.records()) {
        const auto record = value.toObject();
        if (record.value("id") != id) continue;
        return recoverRecord(record, now);
    }
    return result;
}

bool SignedQuoteAcceptor::recoverMutualHistory(qint64 now,
    QVector<RecoveredMutualQuote> &quotes) const
{
    if (!m_store.ready() || now < 0) return false;
    QVector<RecoveredMutualQuote> recovered;
    for (const auto value : m_store.records()) {
        const auto quote = recoverRecord(value.toObject(), now);
        if (!quote.valid()) return false;
        recovered.append(quote);
    }
    quotes = std::move(recovered);
    return true;
}

RecoveredMutualQuote SignedQuoteAcceptor::recoverRecord(const QJsonObject &record, qint64 now) const
{
    RecoveredMutualQuote result;
    bool timeOk = false;
    const auto acceptedAt = record.value("acceptedAt").toString().toLongLong(&timeOk);
    const auto encoded = record.value("signedArtifact").toString().toLatin1();
    const auto wire = QByteArray::fromBase64(encoded);
    if (!timeOk || acceptedAt < 0 || acceptedAt > now || wire.isEmpty()
        || wire.size() > 98304 || wire.toBase64() != encoded) return result;
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(wire, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) return result;
    const auto root = document.object();
    SettlementTerms::Terms terms;
    const auto payload = QByteArray::fromBase64(root.value("terms").toString().toLatin1());
    if (!SettlementTerms::decode(payload, acceptedAt, terms)
        || record.value("id") != QuoteReplayStore::replayId(terms.makerKey, terms.nonce)
        || record.value("quoteExpiry") != QString::number(terms.quoteExpiry)) return result;
    const auto maker = QByteArray::fromHex(root.value("makerSignature").toString().toLatin1());
    const auto taker = QByteArray::fromHex(root.value("takerSignature").toString().toLatin1());
    // Exact re-encoding checks schema, canonical encodings, both signatures
    // and configured deployment without modifying the replay store.
    if (encodeMutualMessage(terms, maker, taker, acceptedAt) != wire) return result;
    const auto built = SettlementTerms::build(terms, acceptedAt);
    if (record.value("commitment") != QString::fromLatin1(built.commitment.toHex())) return result;
    result.m_terms = terms;
    result.m_commitment = built.commitment;
    result.m_acceptedAt = acceptedAt;
    return result;
}

QByteArray SignedQuoteAcceptor::encodeMutualMessage(const SettlementTerms::Terms &terms,
    const QByteArray &makerSignature, const QByteArray &takerSignature, qint64 now) const
{
    const auto built = SettlementTerms::build(terms, now);
    if (!built.valid || built.payload.size() > 65536 || m_signatureDomain.isEmpty()
        || terms.protocolId != m_protocolId || terms.payNetwork != m_payNetwork
        || terms.receiveNetwork != m_receiveNetwork || terms.xmzNetwork != m_xmzNetwork
        || terms.makerKey == terms.takerKey
        || !OfferIdentity::verify(built.payload, m_signatureDomain, terms.makerKey, makerSignature)
        || !OfferIdentity::verify(takerAcceptancePayload(terms, now), m_signatureDomain,
                                  terms.takerKey, takerSignature)) return {};
    return QJsonDocument(QJsonObject{{"version", 1},
        {"terms", QString::fromLatin1(built.payload.toBase64())},
        {"makerSignature", QString::fromLatin1(makerSignature.toHex())},
        {"takerSignature", QString::fromLatin1(takerSignature.toHex())}})
        .toJson(QJsonDocument::Compact);
}

AcceptedQuote SignedQuoteAcceptor::acceptMutualMessage(const QByteArray &message, qint64 now,
    const QByteArray &expectedMaker, const QByteArray &expectedTaker)
{
    AcceptedQuote invalid;
    invalid.m_details = rejected("Invalid signed quote wire message");
    if (message.isEmpty() || message.size() > 98304) return invalid;
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(message, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()
        || document.toJson(QJsonDocument::Compact) != message) return invalid;
    const auto root = document.object();
    if (root.size() != 4 || root.value("version").toInt(-1) != 1) return invalid;
    const auto encoded = root.value("terms").toString().toLatin1();
    const auto payload = QByteArray::fromBase64(encoded);
    if (payload.toBase64() != encoded) return invalid;
    SettlementTerms::Terms terms;
    if (!SettlementTerms::decode(payload, now, terms)) return invalid;
    if ((!expectedMaker.isEmpty() && terms.makerKey != expectedMaker)
        || (!expectedTaker.isEmpty() && terms.takerKey != expectedTaker)) return invalid;
    const auto makerHex = root.value("makerSignature").toString().toLatin1();
    const auto takerHex = root.value("takerSignature").toString().toLatin1();
    const auto maker = QByteArray::fromHex(makerHex), taker = QByteArray::fromHex(takerHex);
    if (maker.toHex() != makerHex || taker.toHex() != takerHex
        || encodeMutualMessage(terms, maker, taker, now) != message) return invalid;
    return acceptMutualEvidence(terms, maker, taker, now);
}

QByteArray SignedQuoteAcceptor::takerAcceptancePayload(const SettlementTerms::Terms &terms, qint64 now)
{
    const auto built = SettlementTerms::build(terms, now);
    return built.valid ? QByteArray("MONZERO-DEX-TAKER-ACCEPT-V1\n") + built.payload : QByteArray();
}

AcceptedQuote SignedQuoteAcceptor::acceptMutualEvidence(const SettlementTerms::Terms &terms,
    const QByteArray &makerSignature, const QByteArray &takerSignature, qint64 now)
{
    const auto payload = takerAcceptancePayload(terms, now);
    if (payload.isEmpty() || terms.takerKey == terms.makerKey
        || m_signatureDomain.isEmpty()
        || !OfferIdentity::verify(payload, m_signatureDomain, terms.takerKey, takerSignature)) {
        AcceptedQuote rejectedEvidence;
        rejectedEvidence.m_details = rejected("Invalid taker quote acceptance signature");
        return rejectedEvidence;
    }
    // Both signatures must validate before acceptance can consume a nonce.
    const auto artifact = encodeMutualMessage(terms, makerSignature, takerSignature, now);
    if (artifact.isEmpty()) {
        AcceptedQuote invalid;
        invalid.m_details = rejected("Invalid mutual quote artifact");
        return invalid;
    }
    auto evidence = acceptVerifiedEvidence(terms, makerSignature, now, artifact);
    if (evidence.accepted()) {
        evidence.m_mutual = true;
        evidence.m_details["mutuallyAuthorized"] = true;
    }
    return evidence;
}

QVariantMap SignedQuoteAcceptor::accept(const SettlementTerms::Terms &terms,
                                        const QByteArray &makerSignature, qint64 now)
{
    return acceptEvidence(terms, makerSignature, now).details();
}

AcceptedQuote SignedQuoteAcceptor::acceptEvidence(
    const SettlementTerms::Terms &terms, const QByteArray &makerSignature, qint64 now)
{
    return acceptVerifiedEvidence(terms, makerSignature, now, {});
}

AcceptedQuote SignedQuoteAcceptor::acceptVerifiedEvidence(
    const SettlementTerms::Terms &terms, const QByteArray &makerSignature, qint64 now,
    const QByteArray &signedArtifact)
{
    AcceptedQuote evidence;
    if (!m_store.ready()) { evidence.m_details = rejected(m_store.error()); return evidence; }
    if (m_protocolId.isEmpty() || m_payNetwork.isEmpty() || m_receiveNetwork.isEmpty()
        || m_xmzNetwork.isEmpty() || m_signatureDomain.isEmpty())
        { evidence.m_details = rejected("Signed quote acceptor policy is incomplete"); return evidence; }
    if (terms.protocolId != m_protocolId || terms.payNetwork != m_payNetwork
        || terms.receiveNetwork != m_receiveNetwork || terms.xmzNetwork != m_xmzNetwork)
        { evidence.m_details = rejected("Quote protocol or deployment does not match configured policy"); return evidence; }
    const SettlementTerms::Result built = SettlementTerms::build(terms, now);
    if (!built.valid) { evidence.m_details = rejected(built.error); return evidence; }
    if (!OfferIdentity::verify(built.payload, m_signatureDomain, terms.makerKey, makerSignature))
        { evidence.m_details = rejected("Invalid maker quote signature"); return evidence; }
    if (!m_store.accept(terms.makerKey, terms.nonce, built.commitment, terms.quoteExpiry, now, signedArtifact))
        { evidence.m_details = rejected("Quote nonce was replayed with different terms or cannot be persisted"); return evidence; }
    evidence.m_commitment = built.commitment;
    evidence.m_terms = terms;
    evidence.m_details = {{"valid", true}, {"accepted", true}, {"executable", false},
            {"mutuallyAuthorized", false},
            {"commitment", QString::fromLatin1(built.commitment.toHex())},
            {"executionBlocker", "Settlement and refund protocol is not verified"}};
    return evidence;
}
