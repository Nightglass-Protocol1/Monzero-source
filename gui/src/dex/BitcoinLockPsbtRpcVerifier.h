#ifndef MONZERO_BITCOIN_LOCK_PSBT_RPC_VERIFIER_H
#define MONZERO_BITCOIN_LOCK_PSBT_RPC_VERIFIER_H

#include "ReadOnlyRpcClient.h"
#include "SettlementTerms.h"

namespace BitcoinLockPsbtRpcVerifier {
enum class Leg { Pay, Receive };

class Evidence final {
public:
    bool accepted() const { return !m_journalCommitment.isEmpty(); }
    QByteArray journalCommitment() const { return m_journalCommitment; }
    QByteArray settlementTermsCommitment() const { return m_termsCommitment; }
    QString leg() const { return m_leg; }
    QString chain() const { return m_chain; }
    QVariantMap details() const { return m_details; }

private:
    friend Evidence validate(const ReadOnlyRpcClient &, const SettlementTerms::Terms &,
        Leg, qint64, const QByteArray &, const QByteArray &, const QString &,
        const QString &, int, int);
    QByteArray m_journalCommitment, m_termsCommitment;
    QString m_leg, m_chain;
    QVariantMap m_details;
};

Evidence validate(const ReadOnlyRpcClient &client,
                  const SettlementTerms::Terms &terms,
                  Leg leg, qint64 now,
                  const QByteArray &psbtBase64,
                  const QByteArray &authorization,
                  const QString &expectedAddress,
                  const QString &expectedScriptHex,
                  int feeConfirmationTarget = 6,
                  int maximumFeeHeadroomBasisPoints = 20000);
}

#endif
