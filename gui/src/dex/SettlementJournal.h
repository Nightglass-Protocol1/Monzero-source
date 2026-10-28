#ifndef MONZERO_SETTLEMENT_JOURNAL_H
#define MONZERO_SETTLEMENT_JOURNAL_H

#include <QJsonArray>
#include <QLockFile>
#include <QString>
#include <memory>

namespace CrossCurveProofEvidence { class Verification; }
namespace BitcoinRecoveryPsbtRpcVerifier { class Evidence; }
namespace BitcoinSignedRecoveryEvidence { class Evidence; }
namespace BitcoinRecoveryChainObserver { class Observation; }
namespace BitcoinRecoveryConfirmationTracker { class Status; }
namespace BitcoinRecoveryConfirmationTracker { class Tracker; }
namespace BitcoinRecoveryMaturityVerifier { class Evidence; }
namespace BitcoinLockPsbtRpcVerifier { class Evidence; }
namespace MonzeroSuccessTransactionEvidence { class Evidence; }
namespace MonzeroRefundTransactionEvidence { class Evidence; }
class AcceptedQuote;
class RecoveredMutualQuote;
class SignedQuoteAcceptor;
class ConfirmationTracker;
class SettlementJournal;

namespace SettlementOutcome {
class Evidence final
{
public:
    bool accepted() const { return !m_commitment.isEmpty(); }
    QByteArray commitment() const { return m_commitment; }
    QByteArray fillReservationCommitment() const { return m_fillReservationCommitment; }
    QByteArray quoteCommitment() const { return m_quoteCommitment; }
    QString swapId() const { return m_swapId; }
    QString outcome() const { return m_outcome; }

private:
    friend class ::SettlementJournal;
    QByteArray m_commitment, m_fillReservationCommitment, m_quoteCommitment;
    QString m_swapId, m_outcome;
};
}

class SettlementJournal final
{
public:
    explicit SettlementJournal(QString path);
    bool ready() const;
    QString error() const;
    QJsonArray records() const;
    QString headHash() const;
    // Read-only bridge: recovered signatures must match this swap's prior
    // acceptance event. Never inserts a missing event or grants fresh authority.
    RecoveredMutualQuote recoverQuote(const QString &swapId,
        const SignedQuoteAcceptor &acceptor, const QByteArray &makerKey,
        const QByteArray &nonce, qint64 now) const;
    bool appendQuoteAccepted(const QString &swapId, const AcceptedQuote &evidence,
                             qint64 timestamp);
    // Private network quotes use their commitment hex as a stable swap ID.
    // Retries retain the first journal timestamp; no missing history is reset.
    bool ensureMutualQuoteAccepted(const AcceptedQuote &quote, qint64 timestamp);
    bool appendDleqVerification(const QString &swapId,
                                const CrossCurveProofEvidence::Verification &evidence,
                                qint64 timestamp);
    bool appendBitcoinLockPrepared(
        const QString &swapId,
        const BitcoinLockPsbtRpcVerifier::Evidence &evidence,
        qint64 timestamp);
    bool appendBitcoinRecoveryPrepared(
        const QString &swapId,
        const BitcoinRecoveryPsbtRpcVerifier::Evidence &evidence,
        qint64 timestamp);
    bool appendBitcoinRecoveryPrepared(const QString &swapId,
        const RecoveredMutualQuote &quote,
        const BitcoinRecoveryPsbtRpcVerifier::Evidence &evidence, qint64 timestamp);
    bool appendBitcoinRecoverySigned(
        const QString &swapId,
        const BitcoinSignedRecoveryEvidence::Evidence &evidence,
        qint64 timestamp);
    bool appendBitcoinRecoveryMature(
        const QString &swapId,
        const BitcoinRecoveryMaturityVerifier::Evidence &evidence,
        qint64 timestamp);
    bool appendBitcoinRecoveryObserved(
        const QString &swapId,
        const BitcoinRecoveryChainObserver::Observation &observation,
        qint64 timestamp);
    bool appendBitcoinRecoveryConfirmed(
        const QString &swapId,
        const BitcoinRecoveryConfirmationTracker::Tracker &bitcoinTracker,
        const QByteArray &bitcoinBroadcastCommitment,
        qint64 timestamp);
    // Records earned XMZ protocol revenue only when the exact accepted quote
    // has both confirmed XMZ delivery/fee evidence and a matching Bitcoin pay
    // redeem that remains confirmed in the locked persistent tracker.
    bool appendProtocolFeeEarned(
        const QString &swapId,
        const MonzeroSuccessTransactionEvidence::Evidence &xmzEvidence,
        const BitcoinRecoveryConfirmationTracker::Tracker &bitcoinTracker,
        const QByteArray &bitcoinBroadcastCommitment,
        qint64 timestamp);
    bool appendProtocolFeeRevoked(
        const QString &swapId,
        const MonzeroSuccessTransactionEvidence::Evidence &xmzEvidence,
        const BitcoinRecoveryConfirmationTracker::Tracker &bitcoinTracker,
        const QByteArray &bitcoinBroadcastCommitment,
        qint64 timestamp);
    bool appendXmzRefundConfirmed(
        const QString &swapId,
        const MonzeroRefundTransactionEvidence::Evidence &principalRefund,
        const MonzeroRefundTransactionEvidence::Evidence &feeRefund,
        qint64 timestamp);
    // Produces opaque terminal evidence only while both chain proofs still
    // validate and the exact compound fee-earned event is present.
    SettlementOutcome::Evidence settledEvidence(
        const QString &swapId,
        const AcceptedQuote &quote,
        const MonzeroSuccessTransactionEvidence::Evidence &xmzEvidence,
        const BitcoinRecoveryConfirmationTracker::Tracker &bitcoinTracker,
        const QByteArray &bitcoinBroadcastCommitment) const;
    SettlementOutcome::Evidence refundedEvidence(
        const QString &swapId,
        const AcceptedQuote &quote,
        const MonzeroRefundTransactionEvidence::Evidence &principalRefund,
        const MonzeroRefundTransactionEvidence::Evidence &feeRefund,
        const BitcoinRecoveryConfirmationTracker::Tracker &bitcoinTracker,
        const QByteArray &bitcoinBroadcastCommitment) const;
    bool xmzEvidenceCurrent(const ConfirmationTracker &tracker,
        const QString &transactionId, const QString &blockHash) const;

private:
    bool append(const QString &swapId, const QString &event, const QString &chain,
                const QByteArray &artifactHash, qint64 timestamp);
    bool load();
    bool save(const QJsonArray &next);
    static QByteArray recordHash(quint64 sequence, const QString &swapId,
                                 const QString &event, const QString &chain,
                                 const QByteArray &artifactHash, qint64 timestamp,
                                 const QByteArray &previousHash);
    static bool validIdentifier(const QString &value, int maximum);

    QString m_path;
    QString m_error;
    QJsonArray m_records;
    std::unique_ptr<QLockFile> m_lock;
};

#endif
