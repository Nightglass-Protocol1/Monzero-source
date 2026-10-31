#include "../SettlementTerms.h"
#include <QtTest>

class SettlementTest : public QObject {
    Q_OBJECT
private slots:
    void bindingAndRejection() {
        SettlementTerms::Terms t;
        t.protocolId = "btc-xmz-adaptor-draft-v1";
        t.payAsset = "BTC"; t.receiveAsset = "XMZ";
        t.payGenesis = QString(64, 'a');
        t.receiveGenesis = t.xmzGenesis = QString(64, 'b');
        t.payAmountAtomic = "100"; t.receiveAmountAtomic = "1000"; t.feeAtomic = "1";
        t.payNativeFeeMaxAtomic = "10"; t.receiveNativeFeeMaxAtomic = "11";
        t.xmzNativeFeeMaxAtomic = "12";
        t.payRecipient = "test-pay"; t.receiveRecipient = "test-receive"; t.feeRecipient = "test-fee";
        t.payRefundRecipient = "refund-pay"; t.receiveRefundRecipient = "refund-receive";
        t.xmzRefundRecipient = "refund-fee";
        t.payNetwork = "btc-regtest"; t.receiveNetwork = t.xmzNetwork = "xmz-fakechain";
        t.payAnchorHeight = "100"; t.receiveAnchorHeight = "200"; t.xmzAnchorHeight = "200";
        t.payRefundHeight = "120"; t.receiveRefundHeight = "230"; t.xmzRefundHeight = "240";
        t.payConfirmations = 3; t.receiveConfirmations = 5; t.xmzConfirmations = 5;
        t.payLockCommitment = QByteArray(32, 'l'); t.payRefundCommitment = QByteArray(32, 'r');
        t.receiveLockCommitment = QByteArray(32, 'm'); t.receiveRefundCommitment = QByteArray(32, 's');
        t.xmzFeeLockCommitment = QByteArray(32, 'f'); t.xmzFeeRefundCommitment = QByteArray(32, 't');
        t.payRedeemCommitment = QByteArray(32, 'u');
        t.payCancelCommitment = QByteArray(32, 'v');
        t.payPunishCommitment = QByteArray(32, 'w');
        t.payCancelBlocks = 72; t.payPunishBlocks = 72;
        t.payContractKeyA = QByteArray::fromHex("0279be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798");
        t.payContractKeyB = QByteArray::fromHex("02c6047f9441ed7d6d3045406e95c07cd85c778e4b8cef3ca7abac09b95c709ee5");
        t.makerAdaptorSecpKey = t.payContractKeyA;
        t.takerAdaptorSecpKey = t.payContractKeyB;
        t.makerSwapEd25519Key = QByteArray::fromHex("5866666666666666666666666666666666666666666666666666666666666666");
        t.takerSwapEd25519Key = QByteArray::fromHex("c9a3f86aae465f0e56513864510f3997561fa2c9e85ea21dc2292309f3cd6022");
        t.makerDleqProofCommitment = QByteArray(32, 'd');
        t.takerDleqProofCommitment = QByteArray(32, 'e');
        t.makerKey = QByteArray(32, 'a'); t.takerKey = QByteArray(32, 'b'); t.nonce = QByteArray(32, 'c');
        t.payerKey = t.takerKey;
        t.offerFillReservationCommitment = QByteArray(32, 'g');
        t.quoteExpiry = 200; t.refundDeadline = 300; t.feeFromAcquiredXmz = true;
        const auto original = SettlementTerms::build(t, 100);
        QVERIFY(original.valid);
        // Preview registration must not extend the Bitcoin-shaped signing domain.
        for (const QString &asset : {QString("XMR"), QString("DOGE"), QString("ETH"), QString("RUNE")}) {
            auto substituted = t;
            substituted.payAsset = asset;
            QCOMPARE(SettlementTerms::build(substituted, 100).error, QString("Unsupported asset pair"));
            QVERIFY(!SettlementTerms::rebuildAccepted(substituted).valid);
            substituted = t;
            substituted.receiveAsset = asset;
            QCOMPARE(SettlementTerms::build(substituted, 100).error, QString("Unsupported asset pair"));
            QVERIFY(!SettlementTerms::rebuildAccepted(substituted).valid);
        }
        QCOMPARE(original.commitment, SettlementTerms::build(t, 100).commitment);
        const auto acceptedAfterExpiry = SettlementTerms::rebuildAccepted(t);
        QVERIFY(acceptedAfterExpiry.valid);
        QCOMPARE(acceptedAfterExpiry.payload, original.payload);
        QCOMPARE(acceptedAfterExpiry.commitment, original.commitment);
        auto changed = t; changed.feeRecipient = "attacker";
        QVERIFY(original.commitment != SettlementTerms::build(changed, 100).commitment);
        changed = t; changed.feeAtomic = "2";
        QVERIFY(original.commitment != SettlementTerms::build(changed, 100).commitment);
        changed = t; changed.nonce[0] = 'd';
        QVERIFY(original.commitment != SettlementTerms::build(changed, 100).commitment);
        changed = t; changed.offerFillReservationCommitment[0] = 'x';
        QVERIFY(original.commitment != SettlementTerms::build(changed, 100).commitment);
        changed = t; changed.offerFillReservationCommitment.clear();
        QVERIFY(!SettlementTerms::build(changed, 100).valid);
        QVERIFY(!SettlementTerms::build(t, 200).valid);
        changed = t; changed.refundDeadline = 200;
        QVERIFY(!SettlementTerms::build(changed, 100).valid);
        QVERIFY(!SettlementTerms::rebuildAccepted(changed).valid);
        changed = t; changed.feeAtomic = "18446744073709551616";
        QVERIFY(!SettlementTerms::build(changed, 100).valid);
        changed = t; changed.feeAtomic = "01";
        QVERIFY(!SettlementTerms::build(changed, 100).valid);
        // The fee-free native BTC/XMZ milestone must use a distinct signing
        // domain, not reinterpret fee-bearing V7 quotes (including recovery).
        changed = t; changed.feeAtomic = "0";
        QVERIFY(!SettlementTerms::build(changed, 100).valid);
        QVERIFY(!SettlementTerms::rebuildAccepted(changed).valid);
        changed.feeRecipient.clear();
        changed.xmzFeeLockCommitment.clear();
        changed.xmzFeeRefundCommitment.clear();
        QVERIFY(!SettlementTerms::build(changed, 100).valid);
        QVERIFY(!SettlementTerms::rebuildAccepted(changed).valid);
        changed = t; changed.feeFromAcquiredXmz = false;
        QVERIFY(!SettlementTerms::build(changed, 100).valid);
        changed = t; changed.xmzGenesis = QString(64, 'c');
        QVERIFY(!SettlementTerms::build(changed, 100).valid);
        changed = t; changed.payLockCommitment[0] = 'x';
        QVERIFY(original.commitment != SettlementTerms::build(changed, 100).commitment);
        changed = t; changed.xmzRefundHeight = "204";
        QVERIFY(!SettlementTerms::build(changed, 100).valid);
        changed = t; changed.payConfirmations = 0;
        QVERIFY(!SettlementTerms::build(changed, 100).valid);
        changed = t; changed.receiveNetwork = "other-xmz";
        QVERIFY(!SettlementTerms::build(changed, 100).valid);
        changed = t; changed.xmzFeeRefundCommitment = changed.xmzFeeLockCommitment;
        QVERIFY(!SettlementTerms::build(changed, 100).valid);
        changed = t; changed.payContractKeyA = changed.payContractKeyB;
        QVERIFY(!SettlementTerms::build(changed, 100).valid);
        changed = t; changed.payContractKeyA[0] = 0x04;
        QVERIFY(!SettlementTerms::build(changed, 100).valid);
        changed = t; changed.receiveContractKeyA = t.payContractKeyA;
        changed.receiveContractKeyB = t.payContractKeyB;
        QVERIFY(!SettlementTerms::build(changed, 100).valid);
        changed = t; changed.payCancelBlocks = 3;
        QVERIFY(!SettlementTerms::build(changed, 100).valid);
        changed = t; changed.payPunishCommitment = changed.payCancelCommitment;
        QVERIFY(!SettlementTerms::build(changed, 100).valid);
        changed = t; changed.receiveRedeemCommitment = QByteArray(32, 'z');
        QVERIFY(!SettlementTerms::build(changed, 100).valid);
        changed = t; changed.payRedeemCommitment[0] = 'x';
        QVERIFY(original.commitment != SettlementTerms::build(changed, 100).commitment);
        changed = t;
        changed.payContractKeyA[1] = static_cast<char>(changed.payContractKeyA.at(1) ^ 1);
        QVERIFY(original.commitment != SettlementTerms::build(changed, 100).commitment);
        changed = t; changed.makerDleqProofCommitment[0] = 'x';
        QVERIFY(original.commitment != SettlementTerms::build(changed, 100).commitment);
        changed = t; changed.takerAdaptorSecpKey = changed.makerAdaptorSecpKey;
        QVERIFY(!SettlementTerms::build(changed, 100).valid);
        changed = t; changed.takerSwapEd25519Key = changed.makerSwapEd25519Key;
        QVERIFY(!SettlementTerms::build(changed, 100).valid);
        changed = t; changed.takerDleqProofCommitment = changed.makerDleqProofCommitment;
        QVERIFY(!SettlementTerms::build(changed, 100).valid);
        changed = t; changed.payerKey = QByteArray(32, 'z');
        QVERIFY(!SettlementTerms::build(changed, 100).valid);
        changed = t; changed.payerKey.clear();
        QVERIFY(!SettlementTerms::build(changed, 100).valid);
        changed = t; changed.payerKey = t.makerKey;
        QVERIFY(original.commitment != SettlementTerms::build(changed, 100).commitment);
        QVERIFY(SettlementTerms::build(changed, 100).valid);
    }
};
QTEST_GUILESS_MAIN(SettlementTest)
#include "test_settlement.moc"
