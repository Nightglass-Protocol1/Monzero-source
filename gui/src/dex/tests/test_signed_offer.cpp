#include "../SignedOffer.h"

#include <QtTest>

namespace {
SignedOffer::Terms terms()
{
    return {"monzero-dex-offer-v1", "isolated-btc-xmz",
        "12345678-1234-4abc-8def-1234567890ab", "BTC/XMZ", "sell",
        "100000", "117250000000000000", 100, 3700};
}
}

class SignedOfferTest : public QObject
{
    Q_OBJECT
private slots:
    void authenticatesCanonicalOffer()
    {
        OfferIdentity maker;
        const auto created = SignedOffer::create(terms(), maker);
        QVERIFY(created.accepted());
        QVERIFY(created.details().value("authenticated").toBool());
        QVERIFY(!created.details().value("executable").toBool());
        const auto verified = SignedOffer::verify(terms(), created.makerPublicKey(),
            created.signature(), "monzero-dex-offer-v1", "isolated-btc-xmz", 101);
        QVERIFY(verified.accepted());
        QCOMPARE(verified.commitment(), created.commitment());
        QCOMPARE(verified.amountAtomic(), QString("100000"));
    }

    void rejectsMutationDomainImpostorAndExpiry()
    {
        OfferIdentity maker, impostor;
        const auto created = SignedOffer::create(terms(), maker);
        QVERIFY(created.accepted());
        auto changed = terms(); changed.priceAtomic = "117250000000000001";
        QVERIFY(!SignedOffer::verify(changed, created.makerPublicKey(),
            created.signature(), changed.protocolId, changed.network, 101).accepted());
        QVERIFY(!SignedOffer::verify(terms(), created.makerPublicKey(),
            created.signature(), terms().protocolId, "other-network", 101).accepted());
        QVERIFY(!SignedOffer::verify(terms(), impostor.publicKey(),
            created.signature(), terms().protocolId, terms().network, 101).accepted());
        QVERIFY(!SignedOffer::verify(terms(), created.makerPublicKey(),
            created.signature(), terms().protocolId, terms().network, 3700).accepted());
        QVERIFY(!SignedOffer::verify(terms(), created.makerPublicKey(),
            created.signature().left(63), terms().protocolId, terms().network, 101)
                     .accepted());
    }

    void rejectsAmbiguousAndUnboundedTerms()
    {
        OfferIdentity maker;
        auto changed = terms(); changed.amountAtomic = "01";
        QVERIFY(!SignedOffer::create(changed, maker).accepted());
        changed = terms(); changed.side = "SELL";
        QVERIFY(!SignedOffer::create(changed, maker).accepted());
        changed = terms(); changed.pair = "ETH/XMZ";
        QVERIFY(!SignedOffer::create(changed, maker).accepted());
        changed = terms(); changed.offerId = "not-a-uuid";
        QVERIFY(!SignedOffer::create(changed, maker).accepted());
        changed = terms(); changed.expiresAt = changed.createdAt + 86401;
        QVERIFY(!SignedOffer::create(changed, maker).accepted());
        changed = terms(); changed.expiresAt = changed.createdAt;
        QVERIFY(!SignedOffer::create(changed, maker).accepted());
    }
};

QTEST_GUILESS_MAIN(SignedOfferTest)
#include "test_signed_offer.moc"
