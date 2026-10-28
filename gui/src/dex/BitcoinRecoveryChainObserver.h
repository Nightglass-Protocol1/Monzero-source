#ifndef MONZERO_BITCOIN_RECOVERY_CHAIN_OBSERVER_H
#define MONZERO_BITCOIN_RECOVERY_CHAIN_OBSERVER_H

#include "BitcoinSignedRecoveryEvidence.h"
#include "ReadOnlyRpcClient.h"

namespace BitcoinRecoveryChainObserver {
class Observation final {
public:
    bool accepted() const { return !m_commitment.isEmpty(); }
    bool confirmed() const { return m_confirmed; }
    bool found() const { return m_found; }
    QByteArray commitment() const { return m_commitment; }
    QByteArray broadcastCommitment() const { return m_broadcastCommitment; }
    QByteArray signedCommitment() const { return m_signedCommitment; }
    QByteArray settlementTermsCommitment() const { return m_settlementTermsCommitment; }
    QString txid() const { return m_txid; }
    QString chain() const { return m_chain; }
    QString leg() const { return m_leg; }
    QString branch() const { return m_branch; }
    QString blockHash() const { return m_blockHash; }
    QString blockHeight() const { return m_blockHeight; }
    QString confirmations() const { return m_confirmations; }
    QString nodeHeight() const { return m_nodeHeight; }
    int requiredConfirmations() const { return m_requiredConfirmations; }
    QVariantMap details() const { return m_details; }

private:
    friend Observation observe(const ReadOnlyRpcClient &,
        const BitcoinSignedRecoveryEvidence::Evidence &, const QByteArray &,
        const QByteArray &);
    QByteArray m_commitment, m_broadcastCommitment, m_signedCommitment;
    QByteArray m_settlementTermsCommitment;
    QString m_txid, m_chain, m_leg, m_branch;
    QString m_blockHash, m_blockHeight, m_confirmations, m_nodeHeight;
    int m_requiredConfirmations = 0;
    bool m_confirmed = false, m_found = false;
    QVariantMap m_details;
};

// Observes an already-submitted transaction through a pinned, identity-checked
// local Bitcoin Core node. It never submits or modifies node state.
Observation observe(const ReadOnlyRpcClient &client,
                    const BitcoinSignedRecoveryEvidence::Evidence &signedEvidence,
                    const QByteArray &rawTransactionHex,
                    const QByteArray &authorization = {});
}

#endif
