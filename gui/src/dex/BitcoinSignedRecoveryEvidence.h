#ifndef MONZERO_BITCOIN_SIGNED_RECOVERY_EVIDENCE_H
#define MONZERO_BITCOIN_SIGNED_RECOVERY_EVIDENCE_H

#include "BitcoinRecoveryPsbtRpcVerifier.h"

namespace BitcoinSignedRecoveryEvidence {
class Evidence final {
public:
    bool accepted() const { return !m_journalCommitment.isEmpty(); }
    QByteArray journalCommitment() const { return m_journalCommitment; }
    QByteArray preparedCommitment() const { return m_preparedCommitment; }
    QByteArray settlementTermsCommitment() const { return m_settlementTermsCommitment; }
    QString leg() const { return m_leg; }
    QString branch() const { return m_branch; }
    QString chain() const { return m_chain; }
    QString txid() const { return m_txid; }
    QString wtxid() const { return m_wtxid; }
    QByteArray rawTransactionCommitment() const { return m_rawTransactionCommitment; }
    QString feeAtomic() const { return m_feeAtomic; }
    quint64 virtualSize() const { return m_virtualSize; }
    int requiredConfirmations() const { return m_requiredConfirmations; }
    QString expectedGenesis() const { return m_expectedGenesis; }
    QVariantMap details() const { return m_details; }

private:
    friend Evidence validate(const BitcoinRecoveryPsbtRpcVerifier::Evidence &,
                             const QByteArray &);
    QByteArray m_journalCommitment, m_preparedCommitment, m_settlementTermsCommitment;
    QByteArray m_rawTransactionCommitment;
    QString m_leg, m_branch, m_chain, m_txid, m_wtxid, m_feeAtomic;
    QString m_expectedGenesis;
    quint64 m_virtualSize = 0;
    int m_requiredConfirmations = 0;
    QVariantMap m_details;
};

// Parses and validates raw bytes for the exact RPC-prepared recovery template.
// This is evidence only: it never signs or broadcasts a transaction.
Evidence validate(const BitcoinRecoveryPsbtRpcVerifier::Evidence &prepared,
                  const QByteArray &rawTransactionHex);
}

#endif
