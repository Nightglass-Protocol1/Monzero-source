#include "../MonzeroRefundTransactionEvidence.h"
#include <QtTest>

namespace {
const QString genesis(64, '2'), blockHash(64, 'b');

SettlementTerms::Terms terms()
{
    SettlementTerms::Terms t;
    t.protocolId = "btc-xmz-adaptor-draft-v1";
    t.payAsset = "BTC"; t.receiveAsset = "XMZ";
    t.payGenesis = QString(64, '1'); t.receiveGenesis = t.xmzGenesis = genesis;
    t.payNetwork = "btc-regtest";
    t.receiveNetwork = t.xmzNetwork = "xmz-fakechain";
    t.payAmountAtomic = "10000"; t.receiveAmountAtomic = "120"; t.feeAtomic = "5";
    t.payNativeFeeMaxAtomic = "300"; t.receiveNativeFeeMaxAtomic = "4";
    t.xmzNativeFeeMaxAtomic = "3";
    t.payRecipient = "pay"; t.receiveRecipient = "receive"; t.feeRecipient = "fee";
    t.payRefundRecipient = "pay-refund";
    t.receiveRefundRecipient = "receive-refund";
    t.xmzRefundRecipient = "fee-refund";
    t.payAnchorHeight = t.receiveAnchorHeight = t.xmzAnchorHeight = "100";
    t.payRefundHeight = "300"; t.receiveRefundHeight = "200";
    t.xmzRefundHeight = "210";
    t.payConfirmations = 6; t.receiveConfirmations = t.xmzConfirmations = 5;
    t.payLockCommitment = QByteArray(32, 'a'); t.payRedeemCommitment = QByteArray(32, 'b');
    t.payCancelCommitment = QByteArray(32, 'c'); t.payRefundCommitment = QByteArray(32, 'd');
    t.payPunishCommitment = QByteArray(32, 'e'); t.receiveLockCommitment = QByteArray(32, 'f');
    t.xmzFeeLockCommitment = QByteArray(32, 'h');
    t.receiveRefundCommitment = MonzeroRefundTransactionEvidence::intentCommitment(
        MonzeroRefundTransactionEvidence::Leg::ReceivePrincipal, t.xmzNetwork,
        t.xmzGenesis, t.receiveRefundRecipient, t.receiveAmountAtomic,
        t.receiveNativeFeeMaxAtomic, t.receiveRefundHeight);
    t.xmzFeeRefundCommitment = MonzeroRefundTransactionEvidence::intentCommitment(
        MonzeroRefundTransactionEvidence::Leg::ProtocolFee, t.xmzNetwork,
        t.xmzGenesis, t.xmzRefundRecipient, t.feeAtomic,
        t.xmzNativeFeeMaxAtomic, t.xmzRefundHeight);
    t.payCancelBlocks = 72; t.payPunishBlocks = 72;
    t.payContractKeyA = QByteArray::fromHex("0279be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798");
    t.payContractKeyB = QByteArray::fromHex("02c6047f9441ed7d6d3045406e95c07cd85c778e4b8cef3ca7abac09b95c709ee5");
    t.makerAdaptorSecpKey = t.payContractKeyA; t.takerAdaptorSecpKey = t.payContractKeyB;
    t.makerSwapEd25519Key = QByteArray(32, 'm'); t.takerSwapEd25519Key = QByteArray(32, 'n');
    t.makerDleqProofCommitment = QByteArray(32, 'o'); t.takerDleqProofCommitment = QByteArray(32, 'p');
    t.makerKey = QByteArray(32, 'q'); t.takerKey = QByteArray(32, 'r');
    t.payerKey = t.takerKey;
    t.nonce = QByteArray(32, 's'); t.offerFillReservationCommitment = QByteArray(32, 't');
    t.quoteExpiry = 200; t.refundDeadline = 400; t.feeFromAcquiredXmz = true;
    return t;
}

QJsonObject node(double height = 220)
{
    return {{"status", "OK"}, {"synchronized", true}, {"untrusted", false},
            {"nettype", "fakechain"}, {"height", height}, {"target_height", height}};
}

QJsonObject wallet(const QString &txid, const QString &recipient, double amount,
                   double fee, double height = 214, double confirmations = 6)
{
    return {{"transfer", QJsonObject{{"txid", txid}, {"type", "out"},
        {"amount", amount}, {"fee", fee}, {"height", height},
        {"confirmations", confirmations}, {"locked", false},
        {"double_spend_seen", false}, {"destinations", QJsonArray{
            QJsonObject{{"address", recipient}, {"amount", amount}}}}}}};
}

QJsonObject block(const QString &txid, double height = 214)
{
    return {{"status", "OK"}, {"untrusted", false}, {"miner_tx_hash", ""},
        {"tx_hashes", QJsonArray{txid}}, {"block_header", QJsonObject{
            {"hash", blockHash}, {"height", height}, {"orphan_status", false}}}};
}
}

class MonzeroRefundEvidenceTest : public QObject
{
    Q_OBJECT
private slots:
    void verifiesBothDistinctRefundLegs()
    {
        const QString principalTx(64, 'a'), feeTx(64, 'c');
        const auto principal = MonzeroRefundTransactionEvidence::validateEvidence(
            terms(), MonzeroRefundTransactionEvidence::Leg::ReceivePrincipal,
            400, genesis, principalTx,
            wallet(principalTx, "receive-refund", 120, 4), block(principalTx), node());
        QVERIFY2(principal.accepted(), qPrintable(principal.details().value("error").toString()));
        QCOMPARE(principal.leg(), QString("receive"));
        const auto fee = MonzeroRefundTransactionEvidence::validateEvidence(
            terms(), MonzeroRefundTransactionEvidence::Leg::ProtocolFee,
            400, genesis, feeTx,
            wallet(feeTx, "fee-refund", 5, 3), block(feeTx), node());
        QVERIFY2(fee.accepted(), qPrintable(fee.details().value("error").toString()));
        QCOMPARE(fee.leg(), QString("xmz_fee"));
        QVERIFY(principal.commitment() != fee.commitment());
    }

    void rejectsImmatureSubstitutedAndUnsafeEvidence()
    {
        const QString txid(64, 'a');
        const auto verify = [&](SettlementTerms::Terms t, qint64 now,
                                QJsonObject w, QJsonObject b, QJsonObject n) {
            return MonzeroRefundTransactionEvidence::validateEvidence(t,
                MonzeroRefundTransactionEvidence::Leg::ReceivePrincipal,
                now, genesis, txid, w, b, n).accepted();
        };
        QVERIFY(!verify(terms(), 399, wallet(txid, "receive-refund", 120, 4),
                        block(txid), node()));
        QVERIFY(!verify(terms(), 400, wallet(txid, "receive-refund", 120, 4),
                        block(txid), node(200)));
        auto changed = terms(); changed.receiveRefundRecipient = "attacker";
        QVERIFY(!verify(changed, 400, wallet(txid, "attacker", 120, 4),
                        block(txid), node()));
        changed = terms(); changed.receiveRefundCommitment = QByteArray(32, 'z');
        QVERIFY(!verify(changed, 400, wallet(txid, "receive-refund", 120, 4),
                        block(txid), node()));
        QVERIFY(!verify(terms(), 400, wallet(txid, "receive-refund", 120, 5),
                        block(txid), node()));
        QJsonObject orphan = block(txid);
        QJsonObject header = orphan.value("block_header").toObject();
        header["orphan_status"] = true; orphan["block_header"] = header;
        QVERIFY(!verify(terms(), 400, wallet(txid, "receive-refund", 120, 4),
                        orphan, node()));
    }
};

QTEST_GUILESS_MAIN(MonzeroRefundEvidenceTest)
#include "test_monzero_refund_evidence.moc"
