#ifndef MONZERO_BITCOIN_RECOVERY_MEMPOOL_VERIFIER_H
#define MONZERO_BITCOIN_RECOVERY_MEMPOOL_VERIFIER_H

#include "BitcoinSignedRecoveryEvidence.h"
#include "ReadOnlyRpcClient.h"

namespace BitcoinRecoveryMempoolVerifier {
class Approval final {
public:
    bool accepted() const { return !m_commitment.isEmpty(); }
    QByteArray commitment() const { return m_commitment; }
    QByteArray signedCommitment() const { return m_signedCommitment; }
    QString txid() const { return m_txid; }
    QString wtxid() const { return m_wtxid; }
    QString bestBlock() const { return m_bestBlock; }
    QVariantMap details() const { return m_details; }

private:
    friend Approval validate(const ReadOnlyRpcClient &,
                             const BitcoinSignedRecoveryEvidence::Evidence &,
                             const QByteArray &, const QByteArray &);
    QByteArray m_commitment, m_signedCommitment;
    QString m_txid, m_wtxid, m_bestBlock;
    QVariantMap m_details;
};

// Runs Bitcoin Core's non-mutating testmempoolaccept against the exact locally
// verified signed artifact while pinning the active-chain tip. Never broadcasts.
Approval validate(const ReadOnlyRpcClient &client,
                  const BitcoinSignedRecoveryEvidence::Evidence &signedEvidence,
                  const QByteArray &rawTransactionHex,
                  const QByteArray &authorization = {});
}

#endif
