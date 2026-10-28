#include "../XmzFundingCoordinator.h"
#include <QTemporaryDir>
#include <QtTest>

class XmzFundingTest : public QObject {
    Q_OBJECT
    const QString genesis = QString(64, 'a');
    QJsonObject node() const { return {{"nettype", "fakechain"}, {"status", "OK"},
        {"synchronized", true}, {"untrusted", false}, {"height", 121}, {"target_height", 0}}; }
    QJsonObject wallet() const { return {{"balance", 1000}, {"unlocked_balance", 1000},
        {"multisig_import_needed", false}}; }
private slots:
    void verifiedReservations() {
        QTemporaryDir directory;
        XmzFundingCoordinator funding(directory.filePath("reservations.json"));
        auto result = funding.reserve("offer-a", "600", wallet(), {{"height", 121}}, node(), genesis, genesis);
        QVERIFY(result.value("valid").toBool());
        QVERIFY(result.value("balanceVerified").toBool());
        QVERIFY(!result.value("executable").toBool());
        QVERIFY(!funding.reserve("offer-b", "401", wallet(), {{"height", 121}}, node(), genesis, genesis)
                     .value("valid").toBool());
        QVERIFY(funding.reserve("offer-b", "400", wallet(), {{"height", 121}}, node(), genesis, genesis)
                    .value("valid").toBool());
    }
    void rejectsUntrustedOrStaleEvidence() {
        QTemporaryDir directory;
        XmzFundingCoordinator funding(directory.filePath("reservations.json"));
        auto badNode = node(); badNode["nettype"] = "mainnet";
        QVERIFY(!funding.reserve("a", "1", wallet(), {{"height", 121}}, badNode, genesis, genesis).value("valid").toBool());
        QVERIFY(!funding.reserve("b", "1", wallet(), {{"height", 120}}, node(), genesis, genesis).value("valid").toBool());
        auto badWallet = wallet(); badWallet["unlocked_balance"] = 1001;
        QVERIFY(!funding.reserve("c", "1", badWallet, {{"height", 121}}, node(), genesis, genesis).value("valid").toBool());
        badWallet = wallet(); badWallet["multisig_import_needed"] = true;
        QVERIFY(!funding.reserve("d", "1", badWallet, {{"height", 121}}, node(), genesis, genesis).value("valid").toBool());
        badWallet = wallet(); badWallet["balance"] = 9007199254740992.0;
        QVERIFY(!funding.reserve("e", "1", badWallet, {{"height", 121}}, node(), genesis, genesis).value("valid").toBool());
        QVERIFY(!funding.reserve("f", "1", wallet(), {{"height", 121}}, node(), QString(64, 'b'), genesis).value("valid").toBool());
    }
};
QTEST_GUILESS_MAIN(XmzFundingTest)
#include "test_xmz_funding.moc"
