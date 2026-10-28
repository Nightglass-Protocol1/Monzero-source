#ifndef MONZERO_SETTLEMENT_TERMS_H
#define MONZERO_SETTLEMENT_TERMS_H

#include <QByteArray>
#include <QString>

// Immutable signing payload builder only, NOT a settlement verifier.
namespace SettlementTerms {
struct Terms {
    QString protocolId;
    QString payAsset, receiveAsset;
    QString payGenesis, receiveGenesis, xmzGenesis;
    QString payNetwork, receiveNetwork, xmzNetwork;
    QString payAmountAtomic, receiveAmountAtomic, feeAtomic;
    QString payNativeFeeMaxAtomic, receiveNativeFeeMaxAtomic, xmzNativeFeeMaxAtomic;
    QString payRecipient, receiveRecipient, feeRecipient;
    QString payRefundRecipient, receiveRefundRecipient, xmzRefundRecipient;
    QString payAnchorHeight, receiveAnchorHeight, xmzAnchorHeight;
    QString payRefundHeight, receiveRefundHeight, xmzRefundHeight;
    int payConfirmations = 0, receiveConfirmations = 0, xmzConfirmations = 0;
    QByteArray payLockCommitment, payRefundCommitment;
    QByteArray receiveLockCommitment, receiveRefundCommitment;
    QByteArray xmzFeeLockCommitment, xmzFeeRefundCommitment;
    // A UTXO adaptor leg has five distinct transaction templates. Cancel is
    // relative to lock; punish is relative to cancel (BIP68 block units).
    QByteArray payRedeemCommitment, payCancelCommitment, payPunishCommitment;
    QByteArray receiveRedeemCommitment, receiveCancelCommitment, receivePunishCommitment;
    quint32 payCancelBlocks = 0, payPunishBlocks = 0;
    quint32 receiveCancelBlocks = 0, receivePunishBlocks = 0;
    // Ordered compressed secp256k1 keys for UTXO-chain shared outputs.
    // These must be empty for an XMZ leg.
    QByteArray payContractKeyA, payContractKeyB;
    QByteArray receiveContractKeyA, receiveContractKeyB;
    // Cross-curve adaptor identities and commitments to their serialized DLEQ
    // proofs. The proof bytes are exchanged separately and must hash to these.
    QByteArray makerAdaptorSecpKey, makerSwapEd25519Key, makerDleqProofCommitment;
    QByteArray takerAdaptorSecpKey, takerSwapEd25519Key, takerDleqProofCommitment;
    QByteArray makerKey, takerKey, nonce;
    // Must equal makerKey or takerKey exactly. Declares which identity funds,
    // signs, broadcasts and pays the native fee for the pay leg; the other
    // identity does the same for the receive leg. A signer must use this
    // field directly and must not derive funding authority from maker/taker
    // orientation or from the pay/receive asset labels alone.
    QByteArray payerKey;
    // Exact short-lived order-book quantity reservation selected by this quote.
    QByteArray offerFillReservationCommitment;
    qint64 quoteExpiry = 0;
    qint64 refundDeadline = 0;
    bool feeFromAcquiredXmz = false;
};
struct Result {
    bool valid = false;
    QByteArray payload;
    QByteArray commitment;
    QString error;
};
Result build(const Terms &terms, qint64 now);
// Decode current V7 terms without trusting the wire. Output changes only on
// complete canonical validation; signatures and deployment policy are separate.
// V7 added the mandatory payerKey funding-role field. No V6 artifact has ever
// been signed or accepted in this pre-release prototype (the DEX menu stays
// hidden behind a private opt-in env var and live execution is disabled), so
// this is a clean format break rather than a dual-format migration; nothing
// in the tree still calls the removed V6 decoder.
bool decode(const QByteArray &payload, qint64 now, Terms &output);
// Revalidates the immutable payload for an already accepted settlement. Quote
// freshness is intentionally not re-applied: recovery must remain available
// after quote expiry. Callers must separately prove prior quote acceptance.
Result rebuildAccepted(const Terms &terms);
}
#endif
