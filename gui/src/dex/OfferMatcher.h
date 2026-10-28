#ifndef MONZERO_OFFER_MATCHER_H
#define MONZERO_OFFER_MATCHER_H

#include "SignedOffer.h"

#include <QVariantMap>

namespace OfferMatcher {
class Evidence final {
public:
    bool accepted() const { return !m_commitment.isEmpty(); }
    QByteArray commitment() const { return m_commitment; }
    QByteArray buyOfferCommitment() const { return m_buyOfferCommitment; }
    QByteArray sellOfferCommitment() const { return m_sellOfferCommitment; }
    QString buyOfferId() const { return m_buyOfferId; }
    QString sellOfferId() const { return m_sellOfferId; }
    QString pair() const { return m_pair; }
    QString buyBaseAtomic() const { return m_buyBaseAtomic; }
    QString sellBaseAtomic() const { return m_sellBaseAtomic; }
    QString maximumBaseAtomic() const { return m_maximumBaseAtomic; }
    QString minimumPriceAtomic() const { return m_minimumPriceAtomic; }
    QString maximumPriceAtomic() const { return m_maximumPriceAtomic; }
    qint64 validUntil() const { return m_validUntil; }
    QVariantMap details() const { return m_details; }

private:
    friend Evidence cross(const SignedOffer::Evidence &,
                          const SignedOffer::Evidence &, const QString &,
                          const QString &, qint64);
    QByteArray m_commitment, m_buyOfferCommitment, m_sellOfferCommitment;
    QString m_buyOfferId, m_sellOfferId, m_pair, m_buyBaseAtomic,
        m_sellBaseAtomic, m_maximumBaseAtomic, m_minimumPriceAtomic,
        m_maximumPriceAtomic;
    qint64 m_validUntil = 0;
    QVariantMap m_details;
};

Evidence cross(const SignedOffer::Evidence &buy,
               const SignedOffer::Evidence &sell,
               const QString &expectedProtocol,
               const QString &expectedNetwork, qint64 now);
}

#endif
