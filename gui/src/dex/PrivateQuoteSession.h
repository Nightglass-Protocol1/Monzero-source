#ifndef MONZERO_PRIVATE_QUOTE_SESSION_H
#define MONZERO_PRIVATE_QUOTE_SESSION_H
#include "PrivateQuoteChannel.h"
#include "SignedQuoteAcceptor.h"
class SettlementJournal;

// Attach before connecting. Dependencies share this thread and outlive session.
// Pins and expected participant keys must be provisioned together out of band.
class PrivateQuoteSession final : public QObject
{
    Q_OBJECT
public:
    PrivateQuoteSession(PrivateQuoteChannel &channel, SignedQuoteAcceptor &acceptor,
        QByteArray makerKey, QByteArray takerKey, QObject *parent = nullptr);
    PrivateQuoteSession(PrivateQuoteChannel &channel, SignedQuoteAcceptor &acceptor,
        SettlementJournal &journal, QByteArray makerKey, QByteArray takerKey,
        QObject *parent = nullptr);
    bool send(const SettlementTerms::Terms &terms, const QByteArray &makerSignature,
              const QByteArray &takerSignature);
    bool waitingForAcknowledgement() const { return !m_pending.isEmpty(); }
signals:
    void quoteAccepted(const QVariantMap &details);
    void rejected();
    void peerAcknowledged(const QByteArray &quoteCommitment);
    void deliveryUncertain(const QByteArray &quoteCommitment);
private:
    bool configured() const;
    PrivateQuoteChannel &m_channel;
    SignedQuoteAcceptor &m_acceptor;
    SettlementJournal *m_journal = nullptr;
    QByteArray m_maker, m_taker;
    QByteArray m_pending;
    QTimer m_ackDeadline;
};
#endif
