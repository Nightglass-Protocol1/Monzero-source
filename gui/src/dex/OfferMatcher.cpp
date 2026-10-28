#include "OfferMatcher.h"

#include <QCryptographicHash>
#include <QDataStream>

namespace {
QByteArray field(const QByteArray &value)
{
    QByteArray encoded;
    QDataStream stream(&encoded, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    stream << quint32(value.size());
    encoded += value;
    return encoded;
}

bool atomic(const QString &text, quint64 &value)
{
    bool ok = false;
    value = text.toULongLong(&ok);
    return ok && value > 0 && text == QString::number(value);
}
}

namespace OfferMatcher {
Evidence cross(const SignedOffer::Evidence &buyCandidate,
               const SignedOffer::Evidence &sellCandidate,
               const QString &expectedProtocol,
               const QString &expectedNetwork, qint64 now)
{
    Evidence evidence;
    evidence.m_details = {{"valid", false}, {"crossed", false},
                          {"executable", false}};
    if (!buyCandidate.accepted() || !sellCandidate.accepted()) {
        evidence.m_details["error"] = "Missing authenticated offer evidence";
        return evidence;
    }
    const auto buy = SignedOffer::verify(buyCandidate.terms(),
        buyCandidate.makerPublicKey(), buyCandidate.signature(), expectedProtocol,
        expectedNetwork, now);
    const auto sell = SignedOffer::verify(sellCandidate.terms(),
        sellCandidate.makerPublicKey(), sellCandidate.signature(), expectedProtocol,
        expectedNetwork, now);
    if (!buy.accepted() || !sell.accepted()
        || buy.commitment() != buyCandidate.commitment()
        || sell.commitment() != sellCandidate.commitment()) {
        evidence.m_details["error"] = "Offer authentication or deployment mismatch";
        return evidence;
    }
    quint64 buyAmount = 0, sellAmount = 0, buyPrice = 0, sellPrice = 0;
    if (buy.side() != "buy" || sell.side() != "sell"
        || buy.pair() != sell.pair() || buy.offerId() == sell.offerId()
        || buy.makerPublicKey() == sell.makerPublicKey()
        || !atomic(buy.amountAtomic(), buyAmount)
        || !atomic(sell.amountAtomic(), sellAmount)
        || !atomic(buy.priceAtomic(), buyPrice)
        || !atomic(sell.priceAtomic(), sellPrice)
        || buyPrice < sellPrice) {
        evidence.m_details["error"] =
            "Offers do not form an authenticated crossed market";
        return evidence;
    }

    evidence.m_buyOfferCommitment = buy.commitment();
    evidence.m_sellOfferCommitment = sell.commitment();
    evidence.m_buyOfferId = buy.offerId();
    evidence.m_sellOfferId = sell.offerId();
    evidence.m_pair = buy.pair();
    evidence.m_buyBaseAtomic = QString::number(buyAmount);
    evidence.m_sellBaseAtomic = QString::number(sellAmount);
    evidence.m_maximumBaseAtomic = QString::number(qMin(buyAmount, sellAmount));
    evidence.m_minimumPriceAtomic = QString::number(sellPrice);
    evidence.m_maximumPriceAtomic = QString::number(buyPrice);
    evidence.m_validUntil = qMin(buy.expiresAt(), sell.expiresAt());
    QByteArray input("MONZERO-DEX-OFFER-CROSS-V1\n");
    for (const QByteArray &value : {evidence.m_buyOfferCommitment,
             evidence.m_sellOfferCommitment, evidence.m_buyOfferId.toUtf8(),
             evidence.m_sellOfferId.toUtf8(), evidence.m_pair.toUtf8(),
             evidence.m_buyBaseAtomic.toLatin1(),
             evidence.m_sellBaseAtomic.toLatin1(),
             evidence.m_maximumBaseAtomic.toLatin1(),
             evidence.m_minimumPriceAtomic.toLatin1(),
             evidence.m_maximumPriceAtomic.toLatin1(),
             QByteArray::number(evidence.m_validUntil)})
        input += field(value);
    evidence.m_commitment =
        QCryptographicHash::hash(input, QCryptographicHash::Sha256);
    evidence.m_details = {{"valid", true}, {"crossed", true},
        {"pair", evidence.m_pair},
        {"buyOfferId", evidence.m_buyOfferId},
        {"sellOfferId", evidence.m_sellOfferId},
        {"buyBaseAtomic", evidence.m_buyBaseAtomic},
        {"sellBaseAtomic", evidence.m_sellBaseAtomic},
        {"maximumBaseAtomic", evidence.m_maximumBaseAtomic},
        {"minimumPriceAtomic", evidence.m_minimumPriceAtomic},
        {"maximumPriceAtomic", evidence.m_maximumPriceAtomic},
        {"validUntil", QString::number(evidence.m_validUntil)},
        {"requiresSignedQuote", true}, {"executable", false}};
    return evidence;
}
}
