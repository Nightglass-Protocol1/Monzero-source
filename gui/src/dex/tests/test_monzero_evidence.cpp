#include "../MonzeroTransactionEvidence.h"
#include <QtTest>

namespace {
const QString txid(64, 'a');
const QString blockHash(64, 'b');
QJsonObject transfer(quint64 amount = 125)
{
    return {{"txid", txid}, {"address", "xmz-recipient"}, {"amount", static_cast<double>(amount)},
            {"height", 100}, {"confirmations", 3}, {"double_spend_seen", false},
            {"locked", false}, {"type", "in"}};
}
QJsonObject wallet() { return {{"transfers", QJsonArray{transfer()}}}; }
QJsonObject block()
{
    return {{"status", "OK"}, {"untrusted", false}, {"miner_tx_hash", ""},
            {"tx_hashes", QJsonArray{txid}},
            {"block_header", QJsonObject{{"hash", blockHash}, {"height", 100},
                                          {"orphan_status", false}}}};
}
QJsonObject node()
{
    return {{"status", "OK"}, {"synchronized", true}, {"untrusted", false},
            {"nettype", "fakechain"}, {"height", 103}};
}
}

class MonzeroEvidenceTest : public QObject {
    Q_OBJECT
private slots:
    void validatesIncomingSpendableTransfer() {
        const auto result = MonzeroTransactionEvidence::validate(wallet(), block(), node(), txid,
            "xmz-recipient", "125", 3);
        QVERIFY(result.value("valid").toBool());
        QVERIFY(result.value("confirmed").toBool());
        QVERIFY(result.value("spendable").toBool());
        QVERIFY(!result.value("executable").toBool());
        QCOMPARE(result.value("nodeHeight").toString(), QString("102"));
    }
    void aggregatesOutputsAndRequiresExactAmount() {
        auto first = transfer(100), second = transfer(25);
        QJsonObject combined{{"transfers", QJsonArray{first, second}}};
        QVERIFY(MonzeroTransactionEvidence::validate(combined, block(), node(), txid,
            "xmz-recipient", "125", 3).value("valid").toBool());
        QVERIFY(!MonzeroTransactionEvidence::validate(combined, block(), node(), txid,
            "xmz-recipient", "124", 3).value("valid").toBool());
    }
    void rejectsUnsafeOrInconsistentEvidence() {
        auto unsafe = wallet(); auto entries = unsafe.value("transfers").toArray();
        auto item = entries[0].toObject(); item["double_spend_seen"] = true; entries[0] = item;
        unsafe["transfers"] = entries;
        QVERIFY(!MonzeroTransactionEvidence::validate(unsafe, block(), node(), txid,
            "xmz-recipient", "125", 3).value("valid").toBool());
        auto wrongBlock = block(); wrongBlock["tx_hashes"] = QJsonArray{};
        QVERIFY(!MonzeroTransactionEvidence::validate(wallet(), wrongBlock, node(), txid,
            "xmz-recipient", "125", 3).value("valid").toBool());
        auto wrongNode = node(); wrongNode["height"] = 104;
        QVERIFY(!MonzeroTransactionEvidence::validate(wallet(), block(), wrongNode, txid,
            "xmz-recipient", "125", 3).value("valid").toBool());
        wrongNode = node(); wrongNode["untrusted"] = true;
        QVERIFY(!MonzeroTransactionEvidence::validate(wallet(), block(), wrongNode, txid,
            "xmz-recipient", "125", 3).value("valid").toBool());
    }
    void acceptsPoolOnlyWithoutBlockClaims() {
        auto pending = transfer(); pending["type"] = "pool"; pending["height"] = 0;
        pending["confirmations"] = 0; pending["locked"] = true;
        QJsonObject pendingWallet{{"transfers", QJsonArray{pending}}};
        const auto result = MonzeroTransactionEvidence::validate(pendingWallet, {}, node(), txid,
            "xmz-recipient", "125", 3);
        QVERIFY(result.value("valid").toBool());
        QVERIFY(!result.value("confirmed").toBool());
        QVERIFY(!result.value("spendable").toBool());
    }
};

QTEST_GUILESS_MAIN(MonzeroEvidenceTest)
#include "test_monzero_evidence.moc"
