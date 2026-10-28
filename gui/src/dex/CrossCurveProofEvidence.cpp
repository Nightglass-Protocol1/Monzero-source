#include "CrossCurveProofEvidence.h"

#include "CryptoVerifierClient.h"
#include "SignedQuoteAcceptor.h"

#include <QCryptographicHash>
#include <QDataStream>

namespace {
QVariantMap rejected(const QString &error)
{
    return {{"valid", false}, {"termsBound", false},
            {"responseAuthenticated", false}, {"cryptographicallyVerified", false},
            {"executable", false}, {"error", error}};
}

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

namespace CrossCurveProofEvidence {
Prepared prepare(const SettlementTerms::Terms &terms, Party party,
                 const QByteArray &proof, const QByteArray &requestNonce,
                 qint64 now)
{
    Prepared prepared;
    if (party != Party::Maker && party != Party::Taker) {
        prepared.error = "Invalid settlement party";
        return prepared;
    }
    const SettlementTerms::Result settlement = SettlementTerms::build(terms, now);
    if (!settlement.valid) {
        prepared.error = "Invalid signed settlement terms: " + settlement.error;
        return prepared;
    }
    if (requestNonce.size() != 32 || requestNonce == QByteArray(32, '\0')
        || requestNonce == terms.nonce) {
        prepared.error = "Invalid or reused crypto request nonce";
        return prepared;
    }

    const bool maker = party == Party::Maker;
    const QByteArray secpKey = maker ? terms.makerAdaptorSecpKey
                                    : terms.takerAdaptorSecpKey;
    const QByteArray edKey = maker ? terms.makerSwapEd25519Key
                                  : terms.takerSwapEd25519Key;
    const QByteArray expectedProofCommitment = maker
        ? terms.makerDleqProofCommitment : terms.takerDleqProofCommitment;
    const QByteArray actualProofCommitment = CryptoVerifierProtocol::dleqProofCommitment(proof);
    if (actualProofCommitment.isEmpty()
        || actualProofCommitment != expectedProofCommitment) {
        prepared.error = "DLEQ proof does not match signed settlement terms";
        return prepared;
    }

    prepared.request = CryptoVerifierProtocol::crossCurveDleqRequest(
        QString::fromLatin1(requestNonce.toHex()), QString::fromLatin1(secpKey.toHex()),
        QString::fromLatin1(edKey.toHex()), proof, expectedProofCommitment);
    if (!prepared.request.valid) {
        prepared.error = prepared.request.error;
        return prepared;
    }
    prepared.valid = true;
    prepared.settlementTermsCommitment = settlement.commitment;
    prepared.proofCommitment = actualProofCommitment;
    prepared.party = maker ? "maker" : "taker";
    return prepared;
}

Verification verifyEvidence(const SettlementTerms::Terms &terms, Party party,
                            const QByteArray &proof, const QByteArray &requestNonce,
                            qint64 now, const CryptoVerifierClient &client)
{
    Verification verification;
    const Prepared prepared = prepare(terms, party, proof, requestNonce, now);
    if (!prepared.valid) {
        verification.m_details = rejected(prepared.error);
        return verification;
    }
    QVariantMap result = client.verify(prepared.request);
    result["termsBound"] = true;
    result["party"] = prepared.party;
    result["settlementTermsCommitment"] =
        QString::fromLatin1(prepared.settlementTermsCommitment.toHex());
    result["proofCommitment"] = QString::fromLatin1(prepared.proofCommitment.toHex());
    result["cryptoRequestCommitment"] =
        QString::fromLatin1(prepared.request.commitment.toHex());
    result["executable"] = false;
    verification.m_settlementTermsCommitment = prepared.settlementTermsCommitment;
    verification.m_party = prepared.party;
    verification.m_details = result;
    if (result.value("valid").toBool() && result.value("termsBound").toBool()
        && result.value("responseAuthenticated").toBool()
        && result.value("cryptographicallyVerified").toBool()
        && !result.value("executable").toBool()) {
        QByteArray preimage("MONZERO-DEX-DLEQ-EVIDENCE-V1\n");
        preimage += field(prepared.settlementTermsCommitment);
        preimage += field(prepared.party.toUtf8());
        preimage += field(prepared.proofCommitment);
        preimage += field(prepared.request.commitment);
        preimage += field(result.value("implementation").toString().toUtf8());
        preimage += field(result.value("verifierSha256").toString().toLatin1());
        verification.m_journalCommitment =
            QCryptographicHash::hash(preimage, QCryptographicHash::Sha256);
    }
    return verification;
}

Verification verifyRecoveredEvidence(const RecoveredMutualQuote &quote, Party party,
                            const QByteArray &proof, const QByteArray &requestNonce,
                            qint64 now, const CryptoVerifierClient &client)
{
    Verification verification;
    if (!quote.valid() || now < quote.acceptedAt()) {
        verification.m_details = rejected("Missing recovered mutual quote or invalid recovery time");
        return verification;
    }
    const auto terms = quote.terms();
    const auto historical = SettlementTerms::build(terms, quote.acceptedAt());
    if (!historical.valid || historical.commitment != quote.commitment()) {
        verification.m_details = rejected("Recovered quote commitment mismatch");
        return verification;
    }
    // Fresh verification still enforces current expiry. Only the opaque recovered
    // quote permits evaluating immutable terms at their recorded acceptance
    // time; the proof itself is verified again by the pinned verifier now.
    verification = verifyEvidence(terms, party, proof, requestNonce,
                                  quote.acceptedAt(), client);
    verification.m_details["recoveryOnly"] = true;
    return verification;
}

QVariantMap verify(const SettlementTerms::Terms &terms, Party party,
                   const QByteArray &proof, const QByteArray &requestNonce,
                   qint64 now, const CryptoVerifierClient &client)
{
    return verifyEvidence(terms, party, proof, requestNonce, now, client).details();
}
}
