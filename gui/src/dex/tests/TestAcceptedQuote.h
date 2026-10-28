#ifndef MONZERO_TEST_ACCEPTED_QUOTE_H
#define MONZERO_TEST_ACCEPTED_QUOTE_H

#include "../OfferIdentity.h"
#include "../SignedQuoteAcceptor.h"

namespace TestAcceptedQuote {
inline AcceptedQuote create(const QString &replayPath, qint64 now = 100)
{
    OfferIdentity maker;
    SettlementTerms::Terms t;
    t.protocolId = "btc-xmz-adaptor-draft-v1";
    t.payAsset = "BTC"; t.receiveAsset = "XMZ";
    t.payGenesis = QString(64, '1');
    t.receiveGenesis = t.xmzGenesis = QString(64, '2');
    t.payNetwork = "btc-regtest";
    t.receiveNetwork = t.xmzNetwork = "xmz-fakechain";
    t.payAmountAtomic = "10000"; t.receiveAmountAtomic = "1000";
    t.feeAtomic = "1";
    t.payNativeFeeMaxAtomic = "500"; t.receiveNativeFeeMaxAtomic = "10";
    t.xmzNativeFeeMaxAtomic = "10";
    t.payRecipient = "pay"; t.receiveRecipient = "receive"; t.feeRecipient = "fee";
    t.payRefundRecipient = "pay-refund";
    t.receiveRefundRecipient = "receive-refund";
    t.xmzRefundRecipient = "fee-refund";
    t.payAnchorHeight = t.receiveAnchorHeight = t.xmzAnchorHeight = "100";
    t.payRefundHeight = "300"; t.receiveRefundHeight = "200";
    t.xmzRefundHeight = "210";
    t.payConfirmations = 6; t.receiveConfirmations = t.xmzConfirmations = 5;
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
    t.makerKey = maker.publicKey(); t.takerKey = QByteArray(32, 't');
    t.payerKey = t.takerKey;
    t.nonce = QByteArray(32, 'u');
    t.offerFillReservationCommitment = QByteArray(32, 'v');
    t.quoteExpiry = now + 100; t.refundDeadline = now + 300;
    t.feeFromAcquiredXmz = true;
    const QByteArray domain("journal-test-domain");
    const SettlementTerms::Result built = SettlementTerms::build(t, now);
    SignedQuoteAcceptor acceptor(replayPath, t.protocolId, t.payNetwork,
                                 t.receiveNetwork, t.xmzNetwork, domain);
    return acceptor.acceptEvidence(t, maker.sign(built.payload, domain), now);
}
}

#endif
