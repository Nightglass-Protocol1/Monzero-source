#include "OfferFillReservationStore.h"
#include "MonzeroRefundTransactionEvidence.h"
#include "MonzeroSuccessTransactionEvidence.h"

#include <QJsonObject>

namespace {
bool apply(OfferFillReservationStore *store, QJsonArray &records,
           const QString &reservationId,
           const SettlementOutcome::Evidence &outcome,
           const QString &terminalState)
{
    Q_UNUSED(store)
    for (int i = 0; i < records.size(); ++i) {
        QJsonObject record = records.at(i).toObject();
        if (record.value("reservationId") != reservationId) continue;
        if (record.value("state") == terminalState)
            return record.value("outcomeCommitment")
                == QString::fromLatin1(outcome.commitment().toHex());
        if (record.value("state") != "committed"
            || record.value("commitment")
                != QString::fromLatin1(outcome.fillReservationCommitment().toHex())
            || record.value("quoteCommitment")
                != QString::fromLatin1(outcome.quoteCommitment().toHex())) return false;
        record["state"] = terminalState;
        record["outcomeCommitment"] = QString::fromLatin1(outcome.commitment().toHex());
        records[i] = record;
        return true;
    }
    return false;
}
}

bool OfferFillReservationStore::finalizeSettled(
    const QString &reservationId, const QString &swapId,
    const AcceptedQuote &quote, const SettlementJournal &journal,
    const MonzeroSuccessTransactionEvidence::Evidence &xmzEvidence,
    const ConfirmationTracker &xmzTracker,
    const BitcoinRecoveryConfirmationTracker::Tracker &bitcoinTracker,
    const QByteArray &bitcoinBroadcastCommitment)
{
    if (!journal.xmzEvidenceCurrent(xmzTracker, xmzEvidence.transactionId(),
                                    xmzEvidence.blockHash())) return false;
    const auto outcome = journal.settledEvidence(swapId, quote, xmzEvidence,
        bitcoinTracker, bitcoinBroadcastCommitment);
    if (!ready() || !outcome.accepted() || outcome.outcome() != "settled") return false;
    QJsonArray next = m_records;
    return apply(this, next, reservationId, outcome, "settled") && save(next);
}

bool OfferFillReservationStore::finalizeRefunded(
    const QString &reservationId, const QString &swapId,
    const AcceptedQuote &quote, const SettlementJournal &journal,
    const MonzeroRefundTransactionEvidence::Evidence &principalRefund,
    const MonzeroRefundTransactionEvidence::Evidence &feeRefund,
    const ConfirmationTracker &xmzTracker,
    const BitcoinRecoveryConfirmationTracker::Tracker &bitcoinTracker,
    const QByteArray &bitcoinBroadcastCommitment)
{
    if (!journal.xmzEvidenceCurrent(xmzTracker, principalRefund.transactionId(),
                                    principalRefund.details().value("blockHash").toString())
        || !journal.xmzEvidenceCurrent(xmzTracker, feeRefund.transactionId(),
                                       feeRefund.details().value("blockHash").toString()))
        return false;
    const auto outcome = journal.refundedEvidence(swapId, quote, principalRefund,
        feeRefund, bitcoinTracker, bitcoinBroadcastCommitment);
    if (!ready() || !outcome.accepted() || outcome.outcome() != "refunded") return false;
    QJsonArray next = m_records;
    return apply(this, next, reservationId, outcome, "refunded") && save(next);
}
