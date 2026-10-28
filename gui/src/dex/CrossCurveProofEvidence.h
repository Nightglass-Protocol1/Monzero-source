#ifndef MONZERO_CROSS_CURVE_PROOF_EVIDENCE_H
#define MONZERO_CROSS_CURVE_PROOF_EVIDENCE_H

#include "CryptoVerifierProtocol.h"
#include "SettlementTerms.h"

#include <QVariantMap>

class CryptoVerifierClient;
class RecoveredMutualQuote;

namespace CrossCurveProofEvidence {
enum class Party { Maker, Taker };

struct Prepared {
    bool valid = false;
    CryptoVerifierProtocol::Request request;
    QByteArray settlementTermsCommitment;
    QByteArray proofCommitment;
    QString party;
    QString error;
};

class Verification final {
public:
    bool accepted() const { return !m_journalCommitment.isEmpty(); }
    QByteArray journalCommitment() const { return m_journalCommitment; }
    QByteArray settlementTermsCommitment() const { return m_settlementTermsCommitment; }
    QString party() const { return m_party; }
    QVariantMap details() const { return m_details; }

private:
    friend Verification verifyEvidence(const SettlementTerms::Terms &, Party,
        const QByteArray &, const QByteArray &, qint64, const CryptoVerifierClient &);
    friend Verification verifyRecoveredEvidence(const RecoveredMutualQuote &, Party,
        const QByteArray &, const QByteArray &, qint64, const CryptoVerifierClient &);
    QByteArray m_journalCommitment;
    QByteArray m_settlementTermsCommitment;
    QString m_party;
    QVariantMap m_details;
};

Prepared prepare(const SettlementTerms::Terms &terms, Party party,
                 const QByteArray &proof, const QByteArray &requestNonce,
                 qint64 now);
Verification verifyEvidence(const SettlementTerms::Terms &terms, Party party,
                            const QByteArray &proof, const QByteArray &requestNonce,
                            qint64 now, const CryptoVerifierClient &client);
// Rechecks proof bytes against previously accepted, mutually signed terms.
// Does not refresh quote acceptance or authorize signing/broadcast.
Verification verifyRecoveredEvidence(const RecoveredMutualQuote &quote, Party party,
                            const QByteArray &proof, const QByteArray &requestNonce,
                            qint64 now, const CryptoVerifierClient &client);
QVariantMap verify(const SettlementTerms::Terms &terms, Party party,
                   const QByteArray &proof, const QByteArray &requestNonce,
                   qint64 now, const CryptoVerifierClient &client);
}

#endif
