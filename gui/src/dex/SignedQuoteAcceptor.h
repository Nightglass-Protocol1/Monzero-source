#ifndef MONZERO_SIGNED_QUOTE_ACCEPTOR_H
#define MONZERO_SIGNED_QUOTE_ACCEPTOR_H

#include "QuoteReplayStore.h"
#include "SettlementTerms.h"
#include <QVariantMap>
#include <QVector>

class AcceptedQuote final
{
public:
    bool accepted() const { return !m_commitment.isEmpty(); }
    bool mutuallyAuthorized() const { return accepted() && m_mutual; }
    QByteArray commitment() const { return m_commitment; }
    SettlementTerms::Terms terms() const { return m_terms; }
    QByteArray fillReservationCommitment() const
        { return m_terms.offerFillReservationCommitment; }
    QVariantMap details() const { return m_details; }

private:
    friend class SignedQuoteAcceptor;
    QByteArray m_commitment;
    SettlementTerms::Terms m_terms;
    QVariantMap m_details;
    bool m_mutual = false;
};

// Not convertible to fresh quote acceptance or transaction authority.
class RecoveredMutualQuote final
{
public:
    bool valid() const { return !m_commitment.isEmpty(); }
    QByteArray commitment() const { return m_commitment; }
    SettlementTerms::Terms terms() const { return m_terms; }
    qint64 acceptedAt() const { return m_acceptedAt; }
private:
    friend class SignedQuoteAcceptor;
    QByteArray m_commitment;
    SettlementTerms::Terms m_terms;
    qint64 m_acceptedAt = 0;
};

class SignedQuoteAcceptor final
{
public:
    SignedQuoteAcceptor(const QString &replayPath, QString protocolId,
                        QString payNetwork, QString receiveNetwork, QString xmzNetwork,
                        QByteArray signatureDomain);
    QVariantMap accept(const SettlementTerms::Terms &terms,
                       const QByteArray &makerSignature, qint64 now);
    AcceptedQuote acceptEvidence(const SettlementTerms::Terms &terms,
                                 const QByteArray &makerSignature, qint64 now);
    static QByteArray takerAcceptancePayload(const SettlementTerms::Terms &terms, qint64 now);
    AcceptedQuote acceptMutualEvidence(const SettlementTerms::Terms &terms,
        const QByteArray &makerSignature, const QByteArray &takerSignature, qint64 now);
    QByteArray encodeMutualMessage(const SettlementTerms::Terms &terms,
        const QByteArray &makerSignature, const QByteArray &takerSignature, qint64 now) const;
    AcceptedQuote acceptMutualMessage(const QByteArray &message, qint64 now,
        const QByteArray &expectedMaker = {}, const QByteArray &expectedTaker = {});
    RecoveredMutualQuote recoverMutualQuote(const QByteArray &makerKey,
        const QByteArray &nonce, qint64 now) const;
    // All-or-nothing read-only inventory. Output is unchanged on failure.
    bool recoverMutualHistory(qint64 now, QVector<RecoveredMutualQuote> &quotes) const;
    QuoteReplayStore &store();

private:
    RecoveredMutualQuote recoverRecord(const QJsonObject &record, qint64 now) const;
    AcceptedQuote acceptVerifiedEvidence(const SettlementTerms::Terms &terms,
        const QByteArray &makerSignature, qint64 now, const QByteArray &signedArtifact);
    QuoteReplayStore m_store;
    QString m_protocolId, m_payNetwork, m_receiveNetwork, m_xmzNetwork;
    QByteArray m_signatureDomain;
};

#endif
