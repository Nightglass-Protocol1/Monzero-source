#ifndef MONZERO_SUCCESS_TRANSACTION_EVIDENCE_H
#define MONZERO_SUCCESS_TRANSACTION_EVIDENCE_H

#include <QJsonObject>
#include <QVariantMap>
#include "SettlementTerms.h"

namespace MonzeroSuccessTransactionEvidence {
class Evidence final {
public:
    bool accepted() const { return !m_commitment.isEmpty(); }
    QByteArray commitment() const { return m_commitment; }
    QByteArray settlementTermsCommitment() const { return m_termsCommitment; }
    QString transactionId() const { return m_transactionId; }
    QString blockHash() const { return m_blockHash; }
    QString chain() const { return m_chain; }
    QVariantMap details() const { return m_details; }

private:
    friend Evidence validateEvidence(const SettlementTerms::Terms &, qint64,
        const QString &, const QString &, const QJsonObject &, const QJsonObject &,
        const QJsonObject &, const QString &);
    QByteArray m_commitment, m_termsCommitment;
    QString m_transactionId, m_blockHash, m_chain;
    QVariantMap m_details;
};

// Checks delivery/fee metadata from a trusted local sender wallet against node
// inclusion evidence. This does NOT independently prove confidential destinations
// or amounts from raw chain data. Never accept peer-supplied wallet JSON as a
// payment proof. Evidence only: cannot construct/relay or enforce fee collection.
QVariantMap validate(const QJsonObject &walletResult,
                     const QJsonObject &blockResult,
                     const QJsonObject &nodeInfo,
                     const QString &expectedTransactionId,
                     const QString &deliveryAddress,
                     const QString &deliveryAmountAtomic,
                     const QString &feeAddress,
                     const QString &feeAmountAtomic,
                     const QString &maximumNativeFeeAtomic,
                     int requiredConfirmations);
Evidence validateEvidence(const SettlementTerms::Terms &terms,
                          qint64 now,
                          const QString &observedGenesis,
                          const QString &expectedTransactionId,
                          const QJsonObject &walletResult,
                          const QJsonObject &blockResult,
                          const QJsonObject &nodeInfo,
                          const QString &expectedNetwork = "xmz-fakechain");
}

#endif
