#include "../RelayEnvelope.h"

#include <QtTest>
#include <limits>

namespace {
RelayEnvelope::Terms terms()
{
    RelayEnvelope::Terms result;
    result.protocol = "monzero-dex-relay-v1";
    result.network = "isolated-btc-xmz";
    result.type = "offer";
    result.sequence = 7;
    result.createdAt = 100;
    result.expiresAt = 3700;
    result.artifact = QByteArray::fromHex(
        "1234567890abcdef1234567890abcdef1234567890abcdef1234567890abcdef");
    return result;
}
}

class RelayEnvelopeTest : public QObject
{
    Q_OBJECT
private slots:
    void authenticatesCanonicalEnvelope()
    {
        OfferIdentity sender;
        const auto created = RelayEnvelope::create(terms(), sender);
        QVERIFY(created.accepted());
        QVERIFY(created.payload().startsWith("MONZERO-DEX-RELAY-V1\n"));
        QVERIFY(created.details().value("authenticated").toBool());
        QVERIFY(!created.details().value("executable").toBool());

        const auto verified = RelayEnvelope::verify(terms(),
            created.senderPublicKey(), created.signature(), terms().protocol,
            terms().network, 101);
        QVERIFY(verified.accepted());
        QCOMPARE(verified.commitment(), created.commitment());
        QCOMPARE(verified.terms().artifact, terms().artifact);
    }

    void rejectsMutationsAndImpostors()
    {
        OfferIdentity sender, impostor;
        const auto created = RelayEnvelope::create(terms(), sender);
        QVERIFY(created.accepted());

        auto changed = terms(); changed.sequence++;
        QVERIFY(!verify(changed, created, changed.protocol, changed.network, 101));
        changed = terms();
        changed.artifact[0] = char(changed.artifact.at(0) ^ 1);
        QVERIFY(!verify(changed, created, changed.protocol, changed.network, 101));
        changed = terms(); changed.type = "cancellation";
        QVERIFY(!verify(changed, created, changed.protocol, changed.network, 101));
        changed = terms(); changed.protocol = "other-protocol";
        QVERIFY(!verify(changed, created, changed.protocol, changed.network, 101));
        changed = terms(); changed.network = "other-network";
        QVERIFY(!verify(changed, created, changed.protocol, changed.network, 101));

        QVERIFY(!RelayEnvelope::verify(terms(), impostor.publicKey(),
            created.signature(), terms().protocol, terms().network, 101).accepted());
        QVERIFY(!RelayEnvelope::verify(terms(), created.senderPublicKey(),
            created.signature().left(63), terms().protocol, terms().network, 101)
                     .accepted());
    }

    void rejectsInvalidTermsAndLifetime()
    {
        OfferIdentity sender;
        auto changed = terms(); changed.protocol = "bad\nprotocol";
        QVERIFY(!RelayEnvelope::create(changed, sender).accepted());
        changed = terms(); changed.network = "BadNetwork";
        QVERIFY(!RelayEnvelope::create(changed, sender).accepted());
        changed = terms(); changed.type = "fill";
        QVERIFY(!RelayEnvelope::create(changed, sender).accepted());
        changed = terms(); changed.sequence = 0;
        QVERIFY(!RelayEnvelope::create(changed, sender).accepted());
        changed = terms(); changed.artifact = QByteArray(32, '\0');
        QVERIFY(!RelayEnvelope::create(changed, sender).accepted());
        changed = terms(); changed.artifact.chop(1);
        QVERIFY(!RelayEnvelope::create(changed, sender).accepted());
        changed = terms(); changed.expiresAt++;
        QVERIFY(!RelayEnvelope::create(changed, sender).accepted());
        changed = terms(); changed.createdAt = std::numeric_limits<qint64>::max();
        changed.expiresAt = std::numeric_limits<qint64>::max();
        QVERIFY(!RelayEnvelope::create(changed, sender).accepted());
    }

    void rejectsFutureAndExpiredEnvelope()
    {
        OfferIdentity sender;
        const auto created = RelayEnvelope::create(terms(), sender);
        QVERIFY(!RelayEnvelope::verify(terms(), created.senderPublicKey(),
            created.signature(), terms().protocol, terms().network, 99).accepted());
        QVERIFY(!RelayEnvelope::verify(terms(), created.senderPublicKey(),
            created.signature(), terms().protocol, terms().network, 3700).accepted());
        QVERIFY(!RelayEnvelope::verify(terms(), created.senderPublicKey(),
            created.signature(), terms().protocol, terms().network, -1).accepted());
    }

private:
    static bool verify(const RelayEnvelope::Terms &candidate,
                       const RelayEnvelope::Evidence &created,
                       const QString &protocol, const QString &network,
                       qint64 now)
    {
        return RelayEnvelope::verify(candidate, created.senderPublicKey(),
            created.signature(), protocol, network, now).accepted();
    }
};

QTEST_GUILESS_MAIN(RelayEnvelopeTest)
#include "test_relay_envelope.moc"
