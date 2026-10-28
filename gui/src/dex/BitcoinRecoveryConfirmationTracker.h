#ifndef MONZERO_BITCOIN_RECOVERY_CONFIRMATION_TRACKER_H
#define MONZERO_BITCOIN_RECOVERY_CONFIRMATION_TRACKER_H

#include "BitcoinRecoveryChainObserver.h"

#include <QJsonArray>
#include <QLockFile>
#include <memory>

namespace BitcoinRecoveryConfirmationTracker {
class Status final {
public:
    bool accepted() const { return !m_commitment.isEmpty(); }
    bool confirmed() const { return m_confirmed; }
    bool reorgDetected() const { return m_reorgDetected; }
    QByteArray commitment() const { return m_commitment; }
    QByteArray broadcastCommitment() const { return m_broadcastCommitment; }
    QByteArray settlementTermsCommitment() const { return m_settlementTermsCommitment; }
    QByteArray confirmationBasisCommitment() const { return m_confirmationBasisCommitment; }
    QString leg() const { return m_leg; }
    QString branch() const { return m_branch; }
    QString chain() const { return m_chain; }
    QVariantMap details() const { return m_details; }

private:
    friend class Tracker;
    QByteArray m_commitment, m_broadcastCommitment, m_settlementTermsCommitment;
    QByteArray m_confirmationBasisCommitment;
    QString m_leg, m_branch, m_chain;
    bool m_confirmed = false, m_reorgDetected = false;
    QVariantMap m_details;
};

class Tracker final {
public:
    explicit Tracker(QString path);
    bool ready() const;
    QString error() const;
    Status observe(const BitcoinRecoveryChainObserver::Observation &observation);
    // Returns the current locked persistent state, never a caller-supplied
    // historical snapshot. An empty status means the identity is unknown.
    Status current(const QByteArray &broadcastCommitment) const;
    QJsonArray records() const;

private:
    bool load();
    bool save(const QJsonArray &next);
    QString m_path, m_error;
    QJsonArray m_records;
    std::unique_ptr<QLockFile> m_lock;
};
}

#endif
