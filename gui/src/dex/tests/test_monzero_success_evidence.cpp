#include "../MonzeroSuccessTransactionEvidence.h"
#include <QtTest>

namespace {
const QString txid(64, 'a'), blockHash(64, 'b');
QJsonObject wallet()
{
    return {{"transfer", QJsonObject{{"txid", txid}, {"type", "out"},
        {"amount", 125.0}, {"fee", 3.0}, {"height", 100.0}, {"confirmations", 3.0},
        {"locked", false}, {"double_spend_seen", false},
        {"destinations", QJsonArray{
            QJsonObject{{"address", "buyer"}, {"amount", 120.0}},
            QJsonObject{{"address", "fee-recipient"}, {"amount", 5.0}}}}}}};
}
QJsonObject block()
{
    return {{"status", "OK"}, {"untrusted", false}, {"miner_tx_hash", ""},
        {"tx_hashes", QJsonArray{txid}}, {"block_header", QJsonObject{
            {"hash", blockHash}, {"height", 100.0}, {"orphan_status", false}}}};
}
QJsonObject node()
{
    return {{"status", "OK"}, {"synchronized", true}, {"untrusted", false},
            {"nettype", "fakechain"}, {"height", 103.0}, {"target_height", 103.0}};
}
QVariantMap validate(const QJsonObject &walletEvidence, const QJsonObject &blockEvidence = block())
{
    return MonzeroSuccessTransactionEvidence::validate(walletEvidence, blockEvidence, node(),
        txid, "buyer", "120", "fee-recipient", "5", "4", 3);
}

SettlementTerms::Terms terms()
{
    SettlementTerms::Terms t;
    t.protocolId = "btc-xmz-adaptor-draft-v1";
    t.payAsset = "BTC"; t.receiveAsset = "XMZ";
    t.payGenesis = QString(64, '1');
    t.receiveGenesis = t.xmzGenesis = QString(64, '2');
    t.payNetwork = "btc-regtest";
    t.receiveNetwork = t.xmzNetwork = "xmz-fakechain";
    t.payAmountAtomic = "10000"; t.receiveAmountAtomic = "120";
    t.feeAtomic = "5"; t.payNativeFeeMaxAtomic = "300";
    t.receiveNativeFeeMaxAtomic = t.xmzNativeFeeMaxAtomic = "4";
    t.payRecipient = "pay"; t.receiveRecipient = "buyer";
    t.feeRecipient = "fee-recipient"; t.payRefundRecipient = "pay-refund";
    t.receiveRefundRecipient = "receive-refund";
    t.xmzRefundRecipient = "fee-refund";
    t.payAnchorHeight = t.receiveAnchorHeight = t.xmzAnchorHeight = "100";
    t.payRefundHeight = "300"; t.receiveRefundHeight = "200";
    t.xmzRefundHeight = "210";
    t.payConfirmations = t.receiveConfirmations = t.xmzConfirmations = 3;
    t.payLockCommitment = QByteArray(32, 'a');
    t.payRedeemCommitment = QByteArray(32, 'b');
    t.payCancelCommitment = QByteArray(32, 'c');
    t.payRefundCommitment = QByteArray(32, 'd');
    t.payPunishCommitment = QByteArray(32, 'e');
    t.receiveLockCommitment = QByteArray(32, 'f');
    t.receiveRefundCommitment = QByteArray(32, 'g');
    t.xmzFeeLockCommitment = QByteArray(32, 'h');
    t.xmzFeeRefundCommitment = QByteArray(32, 'i');
    t.payCancelBlocks = 72; t.payPunishBlocks = 72;
    t.payContractKeyA = QByteArray::fromHex(
        "0279be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798");
    t.payContractKeyB = QByteArray::fromHex(
        "02c6047f9441ed7d6d3045406e95c07cd85c778e4b8cef3ca7abac09b95c709ee5");
    t.makerAdaptorSecpKey = t.payContractKeyA;
    t.takerAdaptorSecpKey = t.payContractKeyB;
    t.makerSwapEd25519Key = QByteArray(32, 'm');
    t.takerSwapEd25519Key = QByteArray(32, 'n');
    t.makerDleqProofCommitment = QByteArray(32, 'o');
    t.takerDleqProofCommitment = QByteArray(32, 'p');
    t.makerKey = QByteArray(32, 'q'); t.takerKey = QByteArray(32, 'r');
    t.payerKey = t.takerKey;
    t.nonce = QByteArray(32, 's');
    t.offerFillReservationCommitment = QByteArray(32, 't'); t.quoteExpiry = 200;
    t.refundDeadline = 400; t.feeFromAcquiredXmz = true;
    return t;
}
}

class MonzeroSuccessEvidenceTest : public QObject
{
    Q_OBJECT
private slots:
    void provesDeliveryAndFeeInOneConfirmedTransaction()
    {
        const QVariantMap result = validate(wallet());
        QVERIFY2(result.value("valid").toBool(), qPrintable(result.value("error").toString()));
        QVERIFY(result.value("confirmed").toBool());
        QVERIFY(result.value("feeEarned").toBool());
        QVERIFY(result.contains("feeEnforcementVerified"));
        QVERIFY(!result.value("feeEnforcementVerified").toBool());
        QVERIFY(!result.value("executable").toBool());

        const auto evidence = MonzeroSuccessTransactionEvidence::validateEvidence(
            terms(), 100, QString(64, '2'), txid, wallet(), block(), node());
        QVERIFY2(evidence.accepted(),
                 qPrintable(evidence.details().value("error").toString()));
        QCOMPARE(evidence.transactionId(), txid);
        QCOMPARE(evidence.blockHash(), blockHash);
        QVERIFY(evidence.details().value("termsVerified").toBool());
        QVERIFY(evidence.details().value("nodeIdentityVerified").toBool());
        QVERIFY(evidence.details().value("bothNativeFeeCeilingsVerified").toBool());
        QVERIFY(!evidence.details().value("executable").toBool());
        QVERIFY(MonzeroSuccessTransactionEvidence::validateEvidence(
            terms(), 1000, QString(64, '2'), txid, wallet(), block(), node())
                    .accepted());

        auto changed = terms(); changed.receiveRecipient = "attacker";
        QVERIFY(!MonzeroSuccessTransactionEvidence::validateEvidence(
            changed, 100, QString(64, '2'), txid, wallet(), block(), node()).accepted());
        changed = terms(); changed.xmzNativeFeeMaxAtomic = "2";
        QVERIFY(!MonzeroSuccessTransactionEvidence::validateEvidence(
            changed, 100, QString(64, '2'), txid, wallet(), block(), node()).accepted());
        QVERIFY(!MonzeroSuccessTransactionEvidence::validateEvidence(
            terms(), 100, QString(64, '9'), txid, wallet(), block(), node()).accepted());
    }
    void pendingTransactionNeverEarnsFee()
    {
        QJsonObject changed = wallet(); QJsonObject transfer = changed.value("transfer").toObject();
        transfer["type"] = "pending"; transfer["height"] = 0.0;
        transfer["confirmations"] = 0.0; transfer["locked"] = true;
        changed["transfer"] = transfer;
        const QVariantMap result = validate(changed, {});
        QVERIFY(result.value("valid").toBool());
        QVERIFY(!result.value("confirmed").toBool());
        QVERIFY(!result.value("feeEarned").toBool());
    }
    void rejectsSplitSubstitutedAndExcessFeePayments()
    {
        QJsonObject changed = wallet(); QJsonObject transfer = changed.value("transfer").toObject();
        QJsonArray destinations = transfer.value("destinations").toArray();
        QJsonObject fee = destinations[1].toObject(); fee["amount"] = 4.0;
        destinations[1] = fee; transfer["destinations"] = destinations; changed["transfer"] = transfer;
        QVERIFY(!validate(changed).value("valid").toBool());
        changed = wallet(); transfer = changed.value("transfer").toObject();
        destinations = transfer.value("destinations").toArray();
        destinations.append(QJsonObject{{"address", "attacker"}, {"amount", 1.0}});
        transfer["destinations"] = destinations; changed["transfer"] = transfer;
        QVERIFY(!validate(changed).value("valid").toBool());
        changed = wallet(); transfer = changed.value("transfer").toObject();
        transfer["fee"] = 5.0; changed["transfer"] = transfer;
        QVERIFY(!validate(changed).value("valid").toBool());
    }
    void rejectsOrphanAndWrongTransactionType()
    {
        QJsonObject changedBlock = block(); QJsonObject header = changedBlock.value("block_header").toObject();
        header["orphan_status"] = true; changedBlock["block_header"] = header;
        QVERIFY(!validate(wallet(), changedBlock).value("valid").toBool());
        QJsonObject changed = wallet(); QJsonObject transfer = changed.value("transfer").toObject();
        transfer["type"] = "in"; changed["transfer"] = transfer;
        QVERIFY(!validate(changed).value("valid").toBool());
    }
};

QTEST_GUILESS_MAIN(MonzeroSuccessEvidenceTest)
#include "test_monzero_success_evidence.moc"
