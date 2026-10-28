#include "../OfferMatcher.h"

#include <QtTest>

namespace {
SignedOffer::Terms offer(const QString &id, const QString &side,
                         const QString &amount, const QString &price,
                         const QString &pair = "BTC/XMZ")
{
    return {"monzero-dex-offer-v1", "isolated-btc-xmz", id, pair, side,
        amount, price, 100, 3700};
}
}

class OfferMatcherTest : public QObject
{
    Q_OBJECT
private slots:
    void producesDeterministicCrossingEvidence()
    {
        OfferIdentity buyer, seller;
        const auto buy = SignedOffer::create(offer(
            "11111111-1111-4111-8111-111111111111", "buy", "200000", "120"), buyer);
        const auto sell = SignedOffer::create(offer(
            "22222222-2222-4222-8222-222222222222", "sell", "150000", "110"), seller);
        const auto first = OfferMatcher::cross(buy, sell,
            "monzero-dex-offer-v1", "isolated-btc-xmz", 101);
        const auto second = OfferMatcher::cross(buy, sell,
            "monzero-dex-offer-v1", "isolated-btc-xmz", 102);
        QVERIFY(first.accepted());
        QCOMPARE(first.commitment(), second.commitment());
        QCOMPARE(first.maximumBaseAtomic(), QString("150000"));
        QCOMPARE(first.buyBaseAtomic(), QString("200000"));
        QCOMPARE(first.sellBaseAtomic(), QString("150000"));
        QCOMPARE(first.minimumPriceAtomic(), QString("110"));
        QCOMPARE(first.maximumPriceAtomic(), QString("120"));
        QCOMPARE(first.validUntil(), qint64(3700));
        QVERIFY(first.details().value("requiresSignedQuote").toBool());
        QVERIFY(!first.details().value("executable").toBool());
    }

    void rejectsUncrossedReversedDifferentAndExpiredOffers()
    {
        OfferIdentity buyer, seller;
        const auto buy = SignedOffer::create(offer(
            "11111111-1111-4111-8111-111111111111", "buy", "200000", "100"), buyer);
        const auto sell = SignedOffer::create(offer(
            "22222222-2222-4222-8222-222222222222", "sell", "150000", "110"), seller);
        QVERIFY(!OfferMatcher::cross(buy, sell, "monzero-dex-offer-v1",
            "isolated-btc-xmz", 101).accepted());
        QVERIFY(!OfferMatcher::cross(sell, buy, "monzero-dex-offer-v1",
            "isolated-btc-xmz", 101).accepted());
        const auto otherPair = SignedOffer::create(offer(
            "33333333-3333-4333-8333-333333333333", "sell", "150000", "90",
            "LTC/XMZ"), seller);
        QVERIFY(!OfferMatcher::cross(buy, otherPair, "monzero-dex-offer-v1",
            "isolated-btc-xmz", 101).accepted());
        QVERIFY(!OfferMatcher::cross(buy, sell, "monzero-dex-offer-v1",
            "isolated-btc-xmz", 3700).accepted());
    }

    void rejectsSelfMatchAndWrongDeployment()
    {
        OfferIdentity maker;
        const auto buy = SignedOffer::create(offer(
            "11111111-1111-4111-8111-111111111111", "buy", "200000", "120"), maker);
        const auto sell = SignedOffer::create(offer(
            "22222222-2222-4222-8222-222222222222", "sell", "150000", "110"), maker);
        QVERIFY(!OfferMatcher::cross(buy, sell, "monzero-dex-offer-v1",
            "isolated-btc-xmz", 101).accepted());
        QVERIFY(!OfferMatcher::cross(buy, sell, "monzero-dex-offer-v1",
            "other-network", 101).accepted());
        SignedOffer::Evidence empty;
        QVERIFY(!OfferMatcher::cross(empty, sell, "monzero-dex-offer-v1",
            "isolated-btc-xmz", 101).accepted());
    }
};

QTEST_GUILESS_MAIN(OfferMatcherTest)
#include "test_offer_matcher.moc"
