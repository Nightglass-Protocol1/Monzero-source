#include "../SignedOfferCancellation.h"

#include <QtTest>

namespace {
SignedOfferCancellation::Terms terms()
{
    return {"monzero-dex-offer-v1", "isolated-btc-xmz",
        "12345678-1234-4abc-8def-1234567890ab", QByteArray(32, 'o'), 200};
}
}

class OfferCancellationTest : public QObject
{
    Q_OBJECT
private slots:
    void authenticatesExactCancellation()
    {
        OfferIdentity maker;
        const auto created = SignedOfferCancellation::create(terms(), maker);
        QVERIFY(created.accepted());
        QVERIFY(!created.details().value("executable").toBool());
        const auto verified = SignedOfferCancellation::verify(terms(),
            created.makerPublicKey(), created.signature(), "monzero-dex-offer-v1",
            "isolated-btc-xmz", 201);
        QVERIFY(verified.accepted());
        QCOMPARE(verified.commitment(), created.commitment());
    }

    void rejectsMutationImpostorDomainAndFuture()
    {
        OfferIdentity maker, impostor;
        const auto created = SignedOfferCancellation::create(terms(), maker);
        auto changed = terms(); changed.offerCommitment[0] = 'x';
        QVERIFY(!SignedOfferCancellation::verify(changed, created.makerPublicKey(),
            created.signature(), changed.protocolId, changed.network, 201).accepted());
        QVERIFY(!SignedOfferCancellation::verify(terms(), impostor.publicKey(),
            created.signature(), terms().protocolId, terms().network, 201).accepted());
        QVERIFY(!SignedOfferCancellation::verify(terms(), created.makerPublicKey(),
            created.signature(), terms().protocolId, "other-network", 201).accepted());
        QVERIFY(!SignedOfferCancellation::verify(terms(), created.makerPublicKey(),
            created.signature(), terms().protocolId, terms().network, 199).accepted());
    }

    void rejectsMalformedTerms()
    {
        OfferIdentity maker;
        auto changed = terms(); changed.offerCommitment.clear();
        QVERIFY(!SignedOfferCancellation::create(changed, maker).accepted());
        changed = terms(); changed.offerId = "NOT-CANONICAL";
        QVERIFY(!SignedOfferCancellation::create(changed, maker).accepted());
        changed = terms(); changed.cancelledAt = -1;
        QVERIFY(!SignedOfferCancellation::create(changed, maker).accepted());
    }
};

QTEST_GUILESS_MAIN(OfferCancellationTest)
#include "test_offer_cancellation.moc"
