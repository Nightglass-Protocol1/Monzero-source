#ifndef MONZERO_BITCOIN_RECOVERY_MATURITY_VERIFIER_H
#define MONZERO_BITCOIN_RECOVERY_MATURITY_VERIFIER_H

#include "BitcoinRecoveryPsbtRpcVerifier.h"
#include "ReadOnlyRpcClient.h"

namespace BitcoinRecoveryMaturityVerifier {
class Evidence final {
public:
    bool accepted() const { return !m_commitment.isEmpty(); }
    QByteArray commitment() const { return m_commitment; }
    QByteArray preparedCommitment() const { return m_preparedCommitment; }
    QString leg() const { return m_leg; }
    QString branch() const { return m_branch; }
    QString chain() const { return m_chain; }
    QString earliestBlockHeight() const { return m_earliestBlockHeight; }
    QVariantMap details() const { return m_details; }

private:
    friend Evidence validate(const ReadOnlyRpcClient &,
        const BitcoinRecoveryPsbtRpcVerifier::Evidence &, const QByteArray &);
    QByteArray m_commitment, m_preparedCommitment;
    QString m_leg, m_branch, m_chain, m_earliestBlockHeight;
    QVariantMap m_details;
};

// Rechecks the exact unspent parent and BIP68 block maturity at one pinned
// Bitcoin tip. It validates cancel/punish readiness only and never broadcasts.
Evidence validate(const ReadOnlyRpcClient &client,
                  const BitcoinRecoveryPsbtRpcVerifier::Evidence &prepared,
                  const QByteArray &authorization = {});
}

#endif
