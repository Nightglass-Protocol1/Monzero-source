#include "../OfferBookStore.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

namespace {
SignedOffer::Terms terms(const QString &id =
    "12345678-1234-4abc-8def-1234567890ab")
{
    return {"monzero-dex-offer-v1", "isolated-btc-xmz", id, "BTC/XMZ",
        "sell", "100000", "117250000000000000", 100, 3700};
}
}

class OfferBookTest : public QObject
{
    Q_OBJECT
private slots:
    void verifiesReadOnlySnapshotsWhileWriterIsLocked()
    {
        QTemporaryDir directory;
        const auto path = directory.filePath("offers.json");
        const auto snapshot = [&](qint64 now) {
            return OfferBookStore::readSnapshot(path, "monzero-dex-offer-v1", "isolated-btc-xmz", now);
        };
        QVERIFY(!snapshot(101).value("valid").toBool());
        OfferIdentity maker;
        const auto offer = SignedOffer::create(terms(), maker);
        OfferBookStore writer(path, "monzero-dex-offer-v1", "isolated-btc-xmz");
        QVERIFY(writer.insert(offer, 101));
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto before = file.readAll();
        file.close();
        const auto observed = snapshot(102);
        QVERIFY(observed.value("valid").toBool());
        QVERIFY(observed.value("signaturesVerified").toBool());
        QVERIFY(!observed.value("liquidityVerified").toBool());
        QVERIFY(!observed.value("executable").toBool());
        QCOMPARE(observed.value("offers").toList().size(), 1);
        QCOMPARE(snapshot(3700).value("activeCount").toInt(), 0);
        QVERIFY(!OfferBookStore::readSnapshot(path, "monzero-dex-offer-v1",
            "wrong-network", 102).value("valid").toBool());
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), before);
        file.close();
        const auto cancellation = SignedOfferCancellation::create(
            {"monzero-dex-offer-v1", "isolated-btc-xmz", offer.offerId(), offer.commitment(), 200}, maker);
        QVERIFY(writer.cancel(cancellation, 201));
        QCOMPARE(snapshot(201).value("activeCount").toInt(), 0);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QCOMPARE(file.write("broken"), qint64(6));
        file.close();
        const auto corrupted = snapshot(201);
        QVERIFY(!corrupted.value("valid").toBool());
        QVERIFY(corrupted.value("offers").toList().isEmpty());
    }

    void persistsOnlyAuthenticatedOffers()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath("offers.json");
        OfferIdentity maker;
        const auto evidence = SignedOffer::create(terms(), maker);
        {
            OfferBookStore store(path, "monzero-dex-offer-v1", "isolated-btc-xmz");
            QVERIFY(store.ready());
            QVERIFY(store.insert(evidence, 101));
            QVERIFY(store.insert(evidence, 102));
            QCOMPARE(store.records().size(), 1);
            QCOMPARE(store.active(3699).size(), 1);
            QCOMPARE(store.active(3700).size(), 0);
        }
        OfferBookStore restored(path, "monzero-dex-offer-v1", "isolated-btc-xmz");
        QVERIFY(restored.ready());
        QCOMPARE(restored.records().size(), 1);
        // A clock correction after restart must not expose an offer before
        // its signed validity interval begins.
        QCOMPARE(restored.active(99).size(), 0);
        QCOMPARE(restored.active(100).size(), 1);
        QCOMPARE(restored.active(3699).size(), 1);
        QCOMPARE(restored.active(3700).size(), 0);
        QCOMPARE(QFileInfo(path).permissions() & (QFileDevice::ReadGroup
            | QFileDevice::WriteGroup | QFileDevice::ReadOther
            | QFileDevice::WriteOther), QFileDevice::Permissions{});
    }

    void rejectsWrongDomainExpiryAndDuplicateIdMutation()
    {
        QTemporaryDir directory;
        OfferIdentity maker;
        OfferBookStore store(directory.filePath("offers.json"),
            "monzero-dex-offer-v1", "isolated-btc-xmz");
        const auto original = SignedOffer::create(terms(), maker);
        QVERIFY(store.insert(original, 101));
        auto changedTerms = terms();
        changedTerms.priceAtomic = "117250000000000001";
        const auto changed = SignedOffer::create(changedTerms, maker);
        QVERIFY(!store.insert(changed, 101));
        QVERIFY(!store.insert(original, 3700));
        OfferBookStore wrongDomain(directory.filePath("other.json"),
            "monzero-dex-offer-v1", "other-network");
        QVERIFY(!wrongDomain.insert(original, 101));
        SignedOffer::Evidence empty;
        QVERIFY(!store.insert(empty, 101));
    }

    void requiresExactMakerSignedCancellation()
    {
        QTemporaryDir directory;
        const QString path = directory.filePath("offers.json");
        OfferIdentity maker, impostor;
        const auto offer = SignedOffer::create(terms(), maker);
        {
            OfferBookStore store(path, "monzero-dex-offer-v1", "isolated-btc-xmz");
            QVERIFY(store.insert(offer, 101));
            SignedOfferCancellation::Terms cancellationTerms{terms().protocolId,
                terms().network, terms().offerId, offer.commitment(), 200};
            const auto impostorCancellation =
                SignedOfferCancellation::create(cancellationTerms, impostor);
            QVERIFY(!store.cancel(impostorCancellation, 201));
            auto wrongTerms = cancellationTerms;
            wrongTerms.offerCommitment[0] =
                wrongTerms.offerCommitment.at(0) == 'x' ? 'y' : 'x';
            QVERIFY(!store.cancel(SignedOfferCancellation::create(wrongTerms, maker),
                                  201));
            auto lateTerms = cancellationTerms;
            lateTerms.cancelledAt = terms().expiresAt;
            QVERIFY(!store.cancel(SignedOfferCancellation::create(lateTerms, maker),
                                  terms().expiresAt));
            const auto cancellation =
                SignedOfferCancellation::create(cancellationTerms, maker);
            QVERIFY(store.cancel(cancellation, 201));
            QVERIFY(store.cancel(cancellation, 202));
            QCOMPARE(store.cancellations().size(), 1);
            QCOMPARE(store.active(199).size(), 1);
            QCOMPARE(store.active(202).size(), 0);
        }
        OfferBookStore restored(path, "monzero-dex-offer-v1", "isolated-btc-xmz");
        QVERIFY(restored.ready());
        QCOMPARE(restored.cancellations().size(), 1);
        QCOMPARE(restored.active(203).size(), 0);
    }

    void crossesOnlyActiveUncancelledOffers()
    {
        QTemporaryDir directory;
        OfferIdentity buyer, seller;
        auto buyTerms = terms("11111111-1111-4111-8111-111111111111");
        buyTerms.side = "buy";
        buyTerms.priceAtomic = "120";
        auto sellTerms = terms("22222222-2222-4222-8222-222222222222");
        sellTerms.side = "sell";
        sellTerms.priceAtomic = "110";
        const auto buy = SignedOffer::create(buyTerms, buyer);
        const auto sell = SignedOffer::create(sellTerms, seller);
        OfferBookStore store(directory.filePath("offers.json"),
            "monzero-dex-offer-v1", "isolated-btc-xmz");
        QVERIFY(store.insert(buy, 101));
        QVERIFY(store.insert(sell, 101));
        QVERIFY(store.cross(buy.offerId(), sell.offerId(), 101).accepted());
        SignedOfferCancellation::Terms cancellationTerms{sellTerms.protocolId,
            sellTerms.network, sellTerms.offerId, sell.commitment(), 200};
        QVERIFY(store.cancel(
            SignedOfferCancellation::create(cancellationTerms, seller), 200));
        QVERIFY(!store.cross(buy.offerId(), sell.offerId(), 201).accepted());
        QVERIFY(!store.cross(buy.offerId(), "missing", 201).accepted());
    }

    void prunesAndFailsClosedOnTamperingAndConcurrency()
    {
        QTemporaryDir directory;
        const QString path = directory.filePath("offers.json");
        OfferIdentity maker;
        {
            OfferBookStore first(path, "monzero-dex-offer-v1", "isolated-btc-xmz");
            QVERIFY(first.insert(SignedOffer::create(terms(), maker), 101));
            OfferBookStore second(path, "monzero-dex-offer-v1", "isolated-btc-xmz");
            QVERIFY(!second.ready());
            QVERIFY(first.pruneExpired(3700));
            QCOMPARE(first.records().size(), 0);
        }
        QVERIFY(QFile::remove(path));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("{broken"), qint64(7));
        file.close();
        OfferBookStore corrupt(path, "monzero-dex-offer-v1", "isolated-btc-xmz");
        QVERIFY(!corrupt.ready());
    }

    void detectsPersistedSignatureMutation()
    {
        QTemporaryDir directory;
        const QString path = directory.filePath("offers.json");
        OfferIdentity maker;
        {
            OfferBookStore store(path, "monzero-dex-offer-v1", "isolated-btc-xmz");
            QVERIFY(store.insert(SignedOffer::create(terms(), maker), 101));
        }
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        auto document = QJsonDocument::fromJson(file.readAll());
        file.close();
        auto root = document.object();
        auto records = root.value("records").toArray();
        auto record = records.at(0).toObject();
        record["amountAtomic"] = "100001";
        records[0] = record;
        root["records"] = records;
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QVERIFY(file.write(QJsonDocument(root).toJson(QJsonDocument::Compact)) > 0);
        file.close();
        OfferBookStore tampered(path, "monzero-dex-offer-v1", "isolated-btc-xmz");
        QVERIFY(!tampered.ready());
    }
};

QTEST_GUILESS_MAIN(OfferBookTest)
#include "test_offer_book.moc"
