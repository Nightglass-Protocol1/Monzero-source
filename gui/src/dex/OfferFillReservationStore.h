#ifndef MONZERO_OFFER_FILL_RESERVATION_STORE_H
#define MONZERO_OFFER_FILL_RESERVATION_STORE_H

#include "OfferBookStore.h"
#include "SignedQuoteAcceptor.h"
#include "SettlementJournal.h"

#include <QJsonArray>
#include <QLockFile>
#include <memory>

class OfferFillReservationStore;

namespace OfferFillReservation {
class Evidence final {
public:
    bool accepted() const { return !m_commitment.isEmpty(); }
    QByteArray commitment() const { return m_commitment; }
    QString reservationId() const { return m_reservationId; }
    QString amountAtomic() const { return m_amountAtomic; }
    qint64 expiresAt() const { return m_expiresAt; }
    QVariantMap details() const { return m_details; }

private:
    friend class ::OfferFillReservationStore;
    QByteArray m_commitment;
    QString m_reservationId, m_amountAtomic;
    qint64 m_expiresAt = 0;
    QVariantMap m_details;
};
}

class OfferFillReservationStore final
{
public:
    OfferFillReservationStore(QString path, QString expectedProtocol,
        QString expectedNetwork, const OfferBookStore &offerBook);
    bool ready() const;
    QString error() const;
    OfferFillReservation::Evidence reserve(const QString &reservationId,
        const SignedOffer::Evidence &buy, const SignedOffer::Evidence &sell,
        const QString &amountAtomic, qint64 expiresAt, qint64 now);
    bool commit(const QString &reservationId, const AcceptedQuote &quote, qint64 now);
    // Read-only: cannot commit a missing/reserved fill or refresh quote expiry.
    OfferFillReservation::Evidence recoverCommitted(const QString &reservationId,
        const RecoveredMutualQuote &quote, qint64 now) const;
    bool finalizeSettled(const QString &reservationId, const QString &swapId,
        const AcceptedQuote &quote, const SettlementJournal &journal,
        const MonzeroSuccessTransactionEvidence::Evidence &xmzEvidence,
        const ConfirmationTracker &xmzTracker,
        const BitcoinRecoveryConfirmationTracker::Tracker &bitcoinTracker,
        const QByteArray &bitcoinBroadcastCommitment);
    bool finalizeRefunded(const QString &reservationId, const QString &swapId,
        const AcceptedQuote &quote, const SettlementJournal &journal,
        const MonzeroRefundTransactionEvidence::Evidence &principalRefund,
        const MonzeroRefundTransactionEvidence::Evidence &feeRefund,
        const ConfirmationTracker &xmzTracker,
        const BitcoinRecoveryConfirmationTracker::Tracker &bitcoinTracker,
        const QByteArray &bitcoinBroadcastCommitment);
    bool release(const QString &reservationId);
    QJsonArray records() const;

private:
    bool load();
    bool save(const QJsonArray &next);
    bool decodeOffer(const QJsonObject &object, SignedOffer::Evidence &offer) const;
    QJsonObject encodeOffer(const SignedOffer::Evidence &offer) const;
    OfferFillReservation::Evidence evidence(const QJsonObject &record) const;
    bool validRecord(const QJsonObject &record) const;
    bool capacityAvailable(const QByteArray &offerCommitment, quint64 capacity,
                           quint64 requested, qint64 now) const;

    QString m_path, m_expectedProtocol, m_expectedNetwork, m_error;
    const OfferBookStore &m_offerBook;
    QJsonArray m_records;
    std::unique_ptr<QLockFile> m_lock;
};

#endif
