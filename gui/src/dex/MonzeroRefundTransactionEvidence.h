#ifndef MONZERO_REFUND_TRANSACTION_EVIDENCE_H
#define MONZERO_REFUND_TRANSACTION_EVIDENCE_H

#include "SettlementTerms.h"
#include <QJsonObject>
#include <QVariantMap>

namespace MonzeroRefundTransactionEvidence {
enum class Leg { ReceivePrincipal, ProtocolFee };

class Evidence final {
public:
    bool accepted() const { return !m_commitment.isEmpty(); }
    QByteArray commitment() const { return m_commitment; }
    QByteArray settlementTermsCommitment() const { return m_termsCommitment; }
    QByteArray intentCommitment() const { return m_intentCommitment; }
    QString transactionId() const { return m_transactionId; }
    QString chain() const { return m_chain; }
    QString leg() const { return m_leg; }
    QVariantMap details() const { return m_details; }

private:
    friend Evidence validateEvidence(const SettlementTerms::Terms &, Leg, qint64,
        const QString &, const QString &, const QJsonObject &, const QJsonObject &,
        const QJsonObject &, const QString &);
    QByteArray m_commitment, m_termsCommitment, m_intentCommitment;
    QString m_transactionId, m_chain, m_leg;
    QVariantMap m_details;
};

QByteArray intentCommitment(Leg leg, const QString &network,
    const QString &genesis, const QString &recipient, const QString &amountAtomic,
    const QString &maximumNativeFeeAtomic, const QString &refundHeight);

// Validates a confirmed outgoing refund. It never creates or relays one.
Evidence validateEvidence(const SettlementTerms::Terms &terms, Leg leg, qint64 now,
    const QString &observedGenesis, const QString &expectedTransactionId,
    const QJsonObject &walletResult, const QJsonObject &blockResult,
    const QJsonObject &nodeInfo, const QString &expectedNetwork = "xmz-fakechain");
}

#endif
