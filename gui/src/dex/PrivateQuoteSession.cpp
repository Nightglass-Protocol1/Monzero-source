#include "PrivateQuoteSession.h"
#include "SettlementJournal.h"
#include <QDateTime>
#include <QPointer>
#include <QJsonDocument>
#include <QJsonObject>

namespace {
QByteArray acknowledgement(const QByteArray &commitment)
{
    return QJsonDocument(QJsonObject{{"version", 1}, {"type", "quote-accepted"},
        {"commitment", QString::fromLatin1(commitment.toHex())}}).toJson(QJsonDocument::Compact);
}
}

PrivateQuoteSession::PrivateQuoteSession(PrivateQuoteChannel &channel,
    SignedQuoteAcceptor &acceptor, QByteArray makerKey, QByteArray takerKey, QObject *parent)
    : QObject(parent), m_channel(channel), m_acceptor(acceptor),
      m_maker(std::move(makerKey)), m_taker(std::move(takerKey))
{
    m_ackDeadline.setSingleShot(true);
    m_ackDeadline.setInterval(10000);
    connect(&m_ackDeadline, &QTimer::timeout, &channel, &PrivateQuoteChannel::close);
    connect(&channel, &PrivateQuoteChannel::closed, this, [this] {
        const auto pending = m_pending;
        m_pending.clear(); m_ackDeadline.stop();
        if (!pending.isEmpty()) emit deliveryUncertain(pending);
    });
    connect(&channel, &PrivateQuoteChannel::received, this, [this](const QByteArray &message) {
        const auto object = QJsonDocument::fromJson(message).object();
        if (object.value("type") == "quote-accepted") {
            if (!m_pending.isEmpty() && m_channel.ready() && message == acknowledgement(m_pending)) {
                const auto commitment = m_pending;
                m_pending.clear(); m_ackDeadline.stop();
                emit peerAcknowledged(commitment);
                return;
            }
            QPointer<PrivateQuoteSession> guard(this);
            m_channel.close();
            if (guard) emit rejected();
            return;
        }
        const auto quote = configured() && m_channel.ready()
            ? m_acceptor.acceptMutualMessage(message, QDateTime::currentSecsSinceEpoch(), m_maker, m_taker)
            : AcceptedQuote{};
        if (!quote.mutuallyAuthorized()
            || (m_journal && !m_journal->ensureMutualQuoteAccepted(quote, QDateTime::currentSecsSinceEpoch()))) {
            QPointer<PrivateQuoteSession> guard(this);
            m_channel.close();
            if (guard) emit rejected();
            return;
        }
        // The receipt is protected by this pinned TLS session, not independently
        // signed settlement evidence. Persisted acceptance precedes the reply.
        QPointer<PrivateQuoteSession> guard(this);
        const bool sent = m_channel.send(acknowledgement(quote.commitment()));
        if (!guard) return;
        if (!sent) {
            m_channel.close(); return;
        }
        emit quoteAccepted(quote.details());
    });
}

PrivateQuoteSession::PrivateQuoteSession(PrivateQuoteChannel &channel,
    SignedQuoteAcceptor &acceptor, SettlementJournal &journal,
    QByteArray makerKey, QByteArray takerKey, QObject *parent)
    : PrivateQuoteSession(channel, acceptor, std::move(makerKey), std::move(takerKey), parent)
{
    m_journal = &journal;
}

bool PrivateQuoteSession::configured() const
{
    return m_maker.size() == 32 && m_taker.size() == 32 && m_maker != m_taker
        && m_maker != QByteArray(32, '\0') && m_taker != QByteArray(32, '\0');
}

bool PrivateQuoteSession::send(const SettlementTerms::Terms &terms,
    const QByteArray &makerSignature, const QByteArray &takerSignature)
{
    if (!configured() || !m_channel.ready() || waitingForAcknowledgement()
        || terms.makerKey != m_maker || terms.takerKey != m_taker) return false;
    const auto now = QDateTime::currentSecsSinceEpoch();
    const auto message = m_acceptor.encodeMutualMessage(terms, makerSignature, takerSignature, now);
    if (message.isEmpty()) return false;
    const auto quote = m_acceptor.acceptMutualEvidence(terms, makerSignature, takerSignature, now);
    if (!quote.mutuallyAuthorized()
        || (m_journal && !m_journal->ensureMutualQuoteAccepted(quote, now))) return false;
    m_pending = SettlementTerms::build(terms, now).commitment;
    m_ackDeadline.start();
    QPointer<PrivateQuoteSession> guard(this);
    const bool sent = m_channel.send(message);
    if (!guard) return false;
    if (!sent) {
        m_pending.clear(); m_ackDeadline.stop();
        return false;
    }
    return true;
}
