#include "../BitcoinTransactionEvidence.h"
#include <QtTest>

namespace {
QJsonObject transaction()
{
    return {{"txid", QString(64, 'a')}, {"blockhash", QString(64, 'b')},
            {"confirmations", 3}, {"in_active_chain", true},
            {"vout", QJsonArray{
                QJsonObject{{"value", 0.00000100}, {"n", 0},
                    {"scriptPubKey", QJsonObject{{"address", "recipient"}}}},
                QJsonObject{{"value", 0.00000025}, {"n", 1},
                    {"scriptPubKey", QJsonObject{{"address", "recipient"}}}},
                QJsonObject{{"value", 1.0}, {"n", 2},
                    {"scriptPubKey", QJsonObject{{"address", "change"}}}}
            }}};
}
QJsonObject header() { return {{"hash", QString(64, 'b')}, {"height", 101}, {"confirmations", 3}}; }
}

class BitcoinEvidenceTest : public QObject {
    Q_OBJECT
private slots:
    void validatesExactDestinationAndConfirmations() {
        const auto result = BitcoinTransactionEvidence::validate(transaction(), header(),
            QString(64, 'a'), "recipient", "125", "103", 3);
        QVERIFY(result.value("valid").toBool());
        QVERIFY(result.value("confirmed").toBool());
        QVERIFY(!result.value("executable").toBool());
        QCOMPARE(result.value("blockHeight").toString(), QString("101"));
    }
    void rejectsAmountIdentityAndChainMismatch() {
        QVERIFY(!BitcoinTransactionEvidence::validate(transaction(), header(), QString(64, 'c'),
            "recipient", "125", "103", 3).value("valid").toBool());
        QVERIFY(!BitcoinTransactionEvidence::validate(transaction(), header(), QString(64, 'a'),
            "recipient", "124", "103", 3).value("valid").toBool());
        auto wrongHeader = header(); wrongHeader["hash"] = QString(64, 'c');
        QVERIFY(!BitcoinTransactionEvidence::validate(transaction(), wrongHeader, QString(64, 'a'),
            "recipient", "125", "103", 3).value("valid").toBool());
        wrongHeader = header(); wrongHeader["confirmations"] = 2;
        QVERIFY(!BitcoinTransactionEvidence::validate(transaction(), wrongHeader, QString(64, 'a'),
            "recipient", "125", "103", 3).value("valid").toBool());
        auto inactive = transaction(); inactive["in_active_chain"] = false;
        QVERIFY(!BitcoinTransactionEvidence::validate(inactive, header(), QString(64, 'a'),
            "recipient", "125", "103", 3).value("valid").toBool());
    }
    void validatesMempoolWithoutClaimingConfirmation() {
        auto mempool = transaction();
        mempool.remove("blockhash"); mempool.remove("confirmations"); mempool.remove("in_active_chain");
        const auto result = BitcoinTransactionEvidence::validate(mempool, {}, QString(64, 'a'),
            "recipient", "125", "103", 3);
        QVERIFY(result.value("valid").toBool());
        QVERIFY(!result.value("confirmed").toBool());
        QCOMPARE(result.value("confirmations").toString(), QString("0"));
    }
    void rejectsFractionalSatoshisAndDuplicateIndexes() {
        auto fractional = transaction();
        auto outputs = fractional.value("vout").toArray();
        auto first = outputs[0].toObject(); first["value"] = 0.000001001; outputs[0] = first;
        fractional["vout"] = outputs;
        QVERIFY(!BitcoinTransactionEvidence::validate(fractional, header(), QString(64, 'a'),
            "recipient", "125", "103", 3).value("valid").toBool());
        auto duplicate = transaction(); outputs = duplicate.value("vout").toArray();
        auto second = outputs[1].toObject(); second["n"] = 0; outputs[1] = second; duplicate["vout"] = outputs;
        QVERIFY(!BitcoinTransactionEvidence::validate(duplicate, header(), QString(64, 'a'),
            "recipient", "125", "103", 3).value("valid").toBool());
    }
};

QTEST_GUILESS_MAIN(BitcoinEvidenceTest)
#include "test_bitcoin_evidence.moc"
