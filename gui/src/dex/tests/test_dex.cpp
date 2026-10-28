#include "../DexEngine.h"
#include "../DexUtils.h"
#include "../OfferBookStore.h"
#include <QTemporaryDir>
#include <QDateTime>
#include <QThreadPool>
#include <QSignalSpy>
#include <QCoreApplication>
#include <QTest>

class DexEngineTest : public QObject
{
    Q_OBJECT
private slots:
    void researchSnapshotCannotEnableTrading() {
        QTemporaryDir directory;
        DexEngine engine;
        const auto path = directory.filePath("offers.json");
        QVERIFY(!engine.readResearchMarket(path).value("valid").toBool());
        const auto now = QDateTime::currentSecsSinceEpoch();
        OfferIdentity maker;
        SignedOffer::Terms terms{"monzero-dex-offer-v1", "isolated-btc-xmz",
            "12345678-1234-4abc-8def-1234567890ab", "BTC/XMZ", "sell",
            "100000", "117250000000000000", now, now + 600};
        OfferBookStore book(path, terms.protocolId, terms.network);
        QVERIFY(book.insert(SignedOffer::create(terms, maker), now));
        const auto result = engine.readResearchMarket(path);
        QVERIFY(result.value("valid").toBool());
        QCOMPARE(result.value("offers").toList().size(), 1);
        QVERIFY(!result.value("executable").toBool());
        QVERIFY(!result.value("liquidityVerified").toBool());
        QVERIFY(!engine.signLocalOffer(terms.offerId));
        QVERIFY(!engine.publishOffer(terms.offerId));
        QVERIFY(engine.localOffers().isEmpty());
        QSignalSpy ready(&engine, &DexEngine::researchMarketReady);
        QVERIFY(engine.refreshResearchMarket(path));
        QVERIFY(engine.researchMarketBusy());
        QVERIFY(!engine.refreshResearchMarket(path));
        QCOMPARE(ready.size(), 0);
        QTRY_COMPARE(ready.size(), 1);
        QVERIFY(!engine.researchMarketBusy());
        const auto asyncResult = ready.at(0).at(0).toMap();
        QVERIFY(asyncResult.value("valid").toBool());
        QCOMPARE(asyncResult.value("offers").toList().size(), 1);
        QVERIFY(!asyncResult.value("executable").toBool());
        QVERIFY(engine.refreshResearchMarket(directory.filePath("missing.json")));
        QTRY_COMPARE(ready.size(), 2);
        QVERIFY(!ready.at(1).at(0).toMap().value("valid").toBool());
        QVERIFY(!engine.researchMarketBusy());
    }

    void destructionDuringSnapshotDoesNotLeaveEngineReferences() {
        QTemporaryDir directory;
        auto *engine = new DexEngine;
        QVERIFY(engine->refreshResearchMarket(directory.filePath("missing.json")));
        delete engine;
        QVERIFY(QThreadPool::globalInstance()->waitForDone(5000));
        QCoreApplication::processEvents();
    }

    void testAtomicBounds() {
        quint64 value = 0;
        QVERIFY(!DexUtils::parseAtomic("1", -1, value));
        QVERIFY(!DexUtils::parseAtomic("1", 20, value));
        QVERIFY(!DexUtils::parseAtomic("18446744073709551616", 8, value));
        QVERIFY(DexUtils::parseAtomic("0.00000001", 8, value));
        QCOMPARE(value, quint64(1));
        QVERIFY(DexUtils::parseAtomic("1", 11, value));
        QCOMPARE(value, quint64(100000000000ULL));
        QCOMPARE(DexUtils::formatAtomic(value, 11), QString("1"));
        QVERIFY(DexUtils::parseAtomic("0.00000000001", 11, value));
        QCOMPARE(value, quint64(1));
        QVERIFY(!DexUtils::parseAtomic("0.000000000001", 11, value));
    }
    void testOfferLimitAndDebit() {
        DexEngine engine;
        auto quote = engine.previewOrder("BTC / XMZ", "BUY", "0.001", "1172500", "unknown");
        QCOMPARE(quote.value("settlementXmz").toString(), QString("1173.675"));
        for (int i = 0; i < 100; ++i)
            QVERIFY(engine.createLocalOffer("BTC / XMZ", "BUY", "1", "100", 3600).value("valid").toBool());
        QVERIFY(!engine.createLocalOffer("BTC / XMZ", "BUY", "1", "100", 3600).value("valid").toBool());
        QVERIFY(!engine.fundingPlan("BTC", "XMZ", "0", "1", "1", "100").value("executable").toBool());
    }
    void testFundingPlan() {
        DexEngine engine;
        auto plan = engine.fundingPlan("BTC", "XMZ", "0", "1", "1", "100");
        QVERIFY(plan.value("valid").toBool());
        QVERIFY(plan.value("funded").toBool());
        QVERIFY(!plan.value("balanceVerified").toBool());
        QVERIFY(!plan.value("executable").toBool());
        QCOMPARE(plan.value("netReceiveXmz").toString(), QString("98"));
        plan = engine.fundingPlan("BTC", "LTC", "1.99999999999", "1", "1", "0");
        QVERIFY(plan.value("valid").toBool());
        QVERIFY(!plan.value("funded").toBool());
        QCOMPARE(plan.value("shortfallXmz").toString(), QString("0.00000000001"));
        QVERIFY(engine.fundingPlan("BTC", "LTC", "2", "1", "1", "0").value("funded").toBool());
        plan = engine.fundingPlan("XMZ", "BTC", "102", "1", "1", "100");
        QVERIFY(plan.value("funded").toBool());
        QCOMPARE(plan.value("requiredUnlockedXmz").toString(), QString("102"));
        QVERIFY(!engine.fundingPlan("BTC", "XMZ", "0", "1", "1", "2.99999999999").value("valid").toBool());
        QVERIFY(!engine.fundingPlan("BTC", "LTC", "2", "1", "1", "1").value("valid").toBool());
        QVERIFY(!engine.fundingPlan("BTC", "BTC", "2", "1", "1", "0").value("valid").toBool());
        QVERIFY(!engine.fundingPlan("ETH", "XMZ", "2", "1", "1", "100").value("valid").toBool());
        QVERIFY(!engine.fundingPlan("BTC", "XMZ", "0", "0", "1", "100").value("valid").toBool());
        QVERIFY(!engine.fundingPlan("BTC", "XMZ", "-1", "1", "1", "100").value("valid").toBool());
        QVERIFY(!engine.fundingPlan("BTC", "XMZ", "0", "184467440.73709551615", "1", "100").value("valid").toBool());
        QVERIFY(!engine.fundingPlan("XMZ", "BTC", "0", "1", "1", "184467440.73709551615").value("valid").toBool());
    }
    void testValidation() {
        DexEngine engine;
        QVERIFY(engine.selfTest().value("passed").toBool());
        QVERIFY(!engine.createLocalOffer("BTC / XMZ", "BUY", "-1", "100", 3600).value("valid").toBool());
        QVERIFY(!engine.createLocalOffer("BTC / XMZ", "BUY", "1", "100", 0).value("valid").toBool());
        QVERIFY(!engine.activationReadiness("BTC / XMZ", {}).value("ready").toBool());
        const auto claimed = engine.activationReadiness("BTC / XMZ",
            {{"feeEnforcementVerified", true}, {"atomicSettlement", true}, {"ready", true}});
        QVERIFY(!claimed.value("ready").toBool());
        QVERIFY(claimed.contains("feeEnforcementVerified"));
        QVERIFY(!claimed.value("feeEnforcementVerified").toBool());
        QCOMPARE(claimed.value("checksRequired").toInt(), claimed.value("blockers").toStringList().size());
        QVERIFY(!engine.validateOffer({}));
        QVERIFY(!engine.signLocalOffer("missing"));
        QVERIFY(!engine.publishOffer("missing"));
        QVERIFY(!engine.matchOffers("a", "b"));
        QVERIFY(!engine.canTransition(OfferState::Draft, OfferState::Settled));
    }
    void testCancellation() {
        DexEngine engine;
        auto offer = engine.createLocalOffer("BTC / XMZ", "BUY", "1", "100", 3600);
        QCOMPARE(engine.localOffers().size(), 1);
        QVERIFY(engine.cancelLocalOffer(offer.value("id").toString()));
        QVERIFY(engine.localOffers().isEmpty());
        QVERIFY(!engine.cancelLocalOffer(offer.value("id").toString()));
    }
    void testOfferCreation() {
        DexEngine engine;
        auto result = engine.createLocalOffer("BTC / XMZ", "BUY", "1", "100", 3600);
        QVERIFY(result.value("valid").toBool());
    }
};

QTEST_GUILESS_MAIN(DexEngineTest)
#include "test_dex.moc"
