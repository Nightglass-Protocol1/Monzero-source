#ifndef MONZERO_TEST_LITECOIN_APP_FIXTURE_H
#define MONZERO_TEST_LITECOIN_APP_FIXTURE_H
#include <QElapsedTimer>
#include "../DexDeployment.h"
#include "../DexEngine.h"
#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>
#include <functional>

// Owned node-fixture transport only. This is never linked into the wallet.
// With one role, this process hosts only that trader; the other runs in a
// separate process (possibly another machine) and waits cover local engines.
inline void runLitecoinAppFixture(DexEngine &alice,DexEngine &bob,const QDir &directory,const QStringList &roles={"alice","bob"}) {
    QList<DexEngine*> local;
    if (roles.contains("alice")) local.append(&alice);
    if (roles.contains("bob")) local.append(&bob);
    const auto all=[&](const std::function<bool(DexEngine&)> &test){for (auto *engine:local) if (!test(*engine)) return false;return true;};
    const auto any=[&](const std::function<bool(DexEngine&)> &test){for (auto *engine:local) if (test(*engine)) return true;return false;};
    const auto write=[&](const QString &name,const QJsonObject &value) {
        QSaveFile file(directory.filePath(name));const auto bytes=QJsonDocument(value).toJson(QJsonDocument::Compact);
        return bytes.size()<=524288 && file.open(QIODevice::WriteOnly) && file.setPermissions(QFile::ReadOwner|QFile::WriteOwner)
            && file.write(bytes)==bytes.size() && file.commit();
    };
    const auto state=[](DexEngine &engine) {
        return QJsonObject::fromVariantMap({{"app",engine.litecoinAppStatus()},{"journal",engine.litecoinJournalStatus()},
            {"funding",engine.litecoinFundingStatus()},{"payment",engine.litecoinPaymentStatus()},
            {"peer",engine.litecoinNegotiationStatus()},{"reservation",engine.litecoinReservationStatus()},
            {"redemption",engine.litecoinRedemptionStatus()},{"lightFunding",engine.litecoinLightFundingStatus()},
            {"reconnects",engine.litecoinReconnectsForTesting()}});
    };
    QVERIFY(directory.exists());QVERIFY(write("ready.json",{{"schema","monzero-ltc-app-driver-ready-v1"}}));
    // Long chain waits send a keepalive every 240 s; this bounds a run at about 45 days.
    for (int sequence=1;sequence<=16384;++sequence) {
        const auto prefix=QString::number(sequence);const auto path=directory.filePath(prefix+"-request.json");
        QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(path),600000);
        QFile file(path);QVERIFY(file.open(QIODevice::ReadOnly));const auto bytes=file.read(8193);QVERIFY(bytes.size()<=8192);
        const auto request=QJsonDocument::fromJson(bytes).object();file.close();
        QCOMPARE(request.value("sequence").toInt(),sequence);
        const auto role=request.value("role").toString();QVERIFY(role=="alice" || role=="bob");QVERIFY(roles.contains(role));
        auto &engine=role=="alice"?alice:bob;const auto operation=request.value("operation").toString();
        const auto commitment=request.value("commitment").toString();const bool retry=request.value("retry").toBool();
        bool accepted=false;
        if (operation=="status" || operation=="quit") accepted=true;
        else if (operation=="start-review") accepted=engine.requestLitecoinStartReview();
        else if (operation=="start-submit") accepted=engine.submitLitecoinStart(commitment);
        else if (operation=="journal") accepted=engine.requestLitecoinJournalCheck();
        else if (operation=="drop-connection") accepted=engine.dropLitecoinTransportForTesting();
        else if (operation=="wait-reconnected") {
            // Not a test assertion: the driver reads the reply's state either way.
            const int before=request.value("reconnects").toInt();
            const auto reconnected=[&] {
                int count=0;for (auto *engine:local) count+=engine->litecoinReconnectsForTesting();
                return count>before && all([&](DexEngine &e){return e.litecoinNegotiationStatus().value("complete").toBool()
                    && (!request.value("recovery").toBool() || e.litecoinNegotiationStatus().value("recoveryComplete").toBool());});
            };
            QElapsedTimer waited;waited.start();
            while (!reconnected() && waited.elapsed()<300000) QTest::qWait(200);
            accepted=reconnected();
        }
        else if (operation=="funding-refresh") accepted=engine.requestLitecoinPhaseCheck("funding");
        else if (operation=="payment-refresh") accepted=engine.requestLitecoinPhaseCheck("payment");
        else if (operation=="light-configure") accepted=engine.configureLightWalletSetup(request.value("helper").toString(),request.value("digest").toString())
            && engine.openLocalLightWallet(DexDeployment::litecoinLightCoin(),QFileInfo(request.value("launcher").toString()).absolutePath());
        else if (operation=="light-resume-wallet") {
            accepted=engine.configureLightWalletSetup(request.value("helper").toString(),request.value("digest").toString());
            const auto wallet=engine.lightWallets().value(DexDeployment::litecoinLightCoin()).toMap();
            accepted=accepted && wallet.value("walletSignConfigured").toBool()
                && wallet.value("walletFolder").toString()==QFileInfo(request.value("launcher").toString()).absolutePath();
        }
        else if (operation=="light-select") accepted=engine.useLitecoinLightWallet();
        else if (operation=="light-terms") accepted=engine.queryLitecoinLightTerms();
        else if (operation=="light-prepare") accepted=engine.prepareLitecoinLightFunding(request.value("feeAtomic").toString(),1);
        else if (operation=="light-resume-recovery") accepted=engine.resumeLitecoinLightRecovery();
        else if (operation=="light-reopen") accepted=engine.reopenLitecoinLightFunding();
        else if (operation=="light-review") accepted=engine.reviewLitecoinLightSignature();
        else if (operation=="light-sign") {
            QFile secret(directory.filePath("light-password"));QVERIFY(secret.open(QIODevice::ReadOnly));
            auto bytes=secret.read(1025);secret.close();QVERIFY(bytes.size()>0 && bytes.size()<=1024);
            accepted=engine.signLitecoinLightFunding(commitment,QString::fromUtf8(bytes));bytes.fill(0);bytes.clear();
        }
        else if (operation=="light-send-review") accepted=engine.reviewLitecoinLightSend();
        else if (operation=="light-send") accepted=engine.sendLitecoinLightFunding(commitment);
        else if (operation=="light-observe") accepted=engine.observeLitecoinLightFunding();
        else if (operation=="light-retry-review") accepted=engine.reviewLitecoinLightRetry();
        else if (operation=="light-retry") accepted=engine.retryLitecoinLightFunding(commitment);
        else if (operation=="reserve") accepted=engine.prepareLitecoinReservation();
        else if (operation=="lock-review") accepted=engine.reviewLitecoinLockFunding();
        else if (operation=="lock-submit") accepted=engine.submitLitecoinLockFunding(commitment);
        else if (operation=="funding-prepare") accepted=engine.prepareLitecoinFunding();
        else if (operation=="funding-review") accepted=engine.requestLitecoinFundingReview(retry);
        else if (operation=="funding-submit") accepted=engine.submitLitecoinFunding(commitment);
        else if (operation=="funding-check") accepted=engine.checkLitecoinFunding();
        else if (operation=="share-proof") accepted=engine.startLitecoinRedemption({});
        else if (operation=="release-redemption") accepted=engine.startLitecoinRedemption(request.value("txid").toString());
        else if (operation=="redemption-review") accepted=engine.reviewLitecoinRedemptionTransaction();
        else if (operation=="redemption-submit") accepted=engine.submitLitecoinRedemptionTransaction(commitment);
        else if (operation=="redemption-check") accepted=engine.checkLitecoinRedemptionTransaction();
        else if (operation=="recovery-review") accepted=engine.reviewLitecoinRecoverySpend(request.value("kind").toString());
        else if (operation=="recovery-submit") accepted=engine.submitLitecoinRecoverySpend(commitment);
        else if (operation=="recovery-check") accepted=engine.checkLitecoinRecoverySpend(request.value("kind").toString());
        else if (operation=="wallet-restore") accepted=engine.restoreLitecoinRecoveredWallet();
        else if (operation=="wallet-open") accepted=engine.openLitecoinRecoveredWallet();
        else if (operation=="wallet-check") accepted=engine.checkLitecoinRecoveredWallet();
        else if (operation=="payout-prepare") accepted=engine.prepareLitecoinPayout();
        else if (operation=="refund-prepare") accepted=engine.prepareLitecoinRefund();
        else if (operation=="payout-review") accepted=engine.requestLitecoinPayoutReview(retry);
        else if (operation=="refund-review") accepted=engine.requestLitecoinRefundReview(retry);
        else if (operation=="payout-submit") accepted=engine.submitLitecoinPayout(commitment);
        else if (operation=="refund-submit") accepted=engine.submitLitecoinRefund(commitment);
        else if (operation=="payout-check") accepted=engine.checkLitecoinPayout();
        else if (operation=="refund-check") accepted=engine.checkLitecoinRefund();
        if (operation=="wait-release") {
            QTRY_VERIFY_WITH_TIMEOUT(bob.litecoinNegotiationStatus().value("canReleaseRedemption").toBool()
                || bob.litecoinNegotiationStatus().value("state")=="unknown",300000);
            accepted=bob.litecoinNegotiationStatus().value("canReleaseRedemption").toBool();
        }
        if (operation=="wait-negotiation" || operation=="wait-recovery" || operation=="wait-settlement") {
            const QString key=operation=="wait-negotiation"?"complete":operation=="wait-recovery"?"recoveryComplete":"settlementComplete";
            const auto done=[&]{return all([&](DexEngine &e){return e.litecoinNegotiationStatus().value(key).toBool();});};
            QTRY_VERIFY_WITH_TIMEOUT(done() || any([](DexEngine &e){return e.litecoinNegotiationStatus().value("state")=="unknown";}),300000);
            accepted=done();
        }
        QTRY_VERIFY_WITH_TIMEOUT(all([](DexEngine &e){return !e.litecoinAppStatus().value("busy").toBool();}),300000);
        QVERIFY(write(prefix+"-response.json",{{"schema","monzero-ltc-app-driver-response-v1"},{"sequence",sequence},
            {"accepted",accepted},{"alice",state(alice)},{"bob",state(bob)}}));
        if (operation=="quit") return;
    }
    QFAIL("Resident app fixture command limit exceeded");
}
#endif
