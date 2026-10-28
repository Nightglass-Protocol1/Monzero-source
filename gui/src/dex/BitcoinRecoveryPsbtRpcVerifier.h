#ifndef MONZERO_BITCOIN_RECOVERY_PSBT_RPC_VERIFIER_H
#define MONZERO_BITCOIN_RECOVERY_PSBT_RPC_VERIFIER_H

#include "BitcoinRecoveryPsbtEvidence.h"
#include "ReadOnlyRpcClient.h"

namespace BitcoinRecoveryPsbtRpcVerifier {
class Evidence final {
public:
    bool accepted() const { return !m_journalCommitment.isEmpty(); }
    QByteArray journalCommitment() const { return m_journalCommitment; }
    QByteArray settlementTermsCommitment() const { return m_settlementTermsCommitment; }
    QString leg() const { return m_leg; }
    QString branch() const { return m_branch; }
    QString chain() const { return m_chain; }
    QString firstContractKeyHex() const { return m_firstContractKeyHex; }
    QString secondContractKeyHex() const { return m_secondContractKeyHex; }
    QString witnessScriptHex() const { return m_witnessScriptHex; }
    int requiredConfirmations() const { return m_requiredConfirmations; }
    QString expectedGenesis() const { return m_expectedGenesis; }
    QString parentTxid() const { return m_parentTxid; }
    QString parentVoutAtomic() const { return m_parentVoutAtomic; }
    QString parentBlockHeight() const { return m_parentBlockHeight; }
    quint32 relativeBlocks() const { return m_relativeBlocks; }
    QVariantMap details() const { return m_details; }

private:
    friend Evidence validateEvidence(const ReadOnlyRpcClient &,
        const SettlementTerms::Terms &, BitcoinRecoveryPsbtEvidence::Leg,
        BitcoinRecoveryPsbtEvidence::Branch, qint64, const QByteArray &,
        const QByteArray &, const QString &, const QString &, const QString &,
        const QString &, const QString &, const QString &, const QString &,
        const QString &, int, int);
    QByteArray m_journalCommitment;
    QByteArray m_settlementTermsCommitment;
    QString m_leg, m_branch, m_chain;
    QString m_firstContractKeyHex, m_secondContractKeyHex, m_witnessScriptHex;
    QString m_expectedGenesis;
    QString m_parentTxid, m_parentVoutAtomic, m_parentBlockHeight;
    quint32 m_relativeBlocks = 0;
    int m_requiredConfirmations = 0;
    QVariantMap m_details;
};

QVariantMap validate(const ReadOnlyRpcClient &client,
                     const SettlementTerms::Terms &terms,
                     BitcoinRecoveryPsbtEvidence::Leg leg,
                     BitcoinRecoveryPsbtEvidence::Branch branch,
                     qint64 now, const QByteArray &psbtBase64,
                     const QByteArray &authorization,
                     const QString &expectedParentTxid,
                     const QString &expectedParentVoutAtomic,
                     const QString &expectedParentAmountAtomic,
                     const QString &expectedParentScriptHex,
                     const QString &firstContractKeyHex,
                     const QString &secondContractKeyHex,
                     const QString &expectedRecipient,
                     const QString &expectedRecipientScriptHex,
                     int feeConfirmationTarget = 6,
                     int maximumFeeHeadroomBasisPoints = 20000);
Evidence validateEvidence(const ReadOnlyRpcClient &client,
                     const SettlementTerms::Terms &terms,
                     BitcoinRecoveryPsbtEvidence::Leg leg,
                     BitcoinRecoveryPsbtEvidence::Branch branch,
                     qint64 now, const QByteArray &psbtBase64,
                     const QByteArray &authorization,
                     const QString &expectedParentTxid,
                     const QString &expectedParentVoutAtomic,
                     const QString &expectedParentAmountAtomic,
                     const QString &expectedParentScriptHex,
                     const QString &firstContractKeyHex,
                     const QString &secondContractKeyHex,
                     const QString &expectedRecipient,
                     const QString &expectedRecipientScriptHex,
                     int feeConfirmationTarget = 6,
                     int maximumFeeHeadroomBasisPoints = 20000);
}

#endif
