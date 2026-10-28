#include "SettlementTerms.h"
#include <QCryptographicHash>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>
#include <limits>

namespace SettlementTerms {
static Result validateAndBuild(const Terms &t, qint64 now, bool requireFreshQuote)
{
    Result r;
    const auto reject = [&](const QString &error) { r.error = error; return r; };
    const QRegularExpression identifier("^[a-z0-9][a-z0-9._-]{0,63}$");
    if (!identifier.match(t.protocolId).hasMatch()) return reject("Invalid settlement protocol identifier");
    const QStringList assets{"XMZ", "BTC", "LTC", "BCH"};
    if (!assets.contains(t.payAsset) || !assets.contains(t.receiveAsset) || t.payAsset == t.receiveAsset)
        return reject("Unsupported asset pair");
    const QRegularExpression hash("^[0-9a-f]{64}$");
    for (const auto &genesis : {t.payGenesis, t.receiveGenesis, t.xmzGenesis})
        if (!hash.match(genesis).hasMatch()) return reject("Invalid genesis identifier");
    if ((t.payAsset == "XMZ" && t.payGenesis != t.xmzGenesis)
        || (t.receiveAsset == "XMZ" && t.receiveGenesis != t.xmzGenesis))
        return reject("Inconsistent XMZ network");
    for (const auto &network : {t.payNetwork, t.receiveNetwork, t.xmzNetwork})
        if (!identifier.match(network).hasMatch()) return reject("Invalid deployment network identifier");
    if ((t.payAsset == "XMZ" && t.payNetwork != t.xmzNetwork)
        || (t.receiveAsset == "XMZ" && t.receiveNetwork != t.xmzNetwork))
        return reject("Inconsistent XMZ deployment identifier");
    const QRegularExpression integer("^[1-9][0-9]{0,19}$");
    for (const auto &amount : {t.payAmountAtomic, t.receiveAmountAtomic, t.feeAtomic,
         t.payNativeFeeMaxAtomic, t.receiveNativeFeeMaxAtomic, t.xmzNativeFeeMaxAtomic}) {
        bool ok = false;
        amount.toULongLong(&ok);
        if (!ok || !integer.match(amount).hasMatch()) return reject("Invalid positive atomic amount");
    }
    for (const auto &address : {t.payRecipient, t.receiveRecipient, t.feeRecipient,
         t.payRefundRecipient, t.receiveRefundRecipient, t.xmzRefundRecipient}) {
        if (address.isEmpty() || address.toUtf8().size() > 256)
            return reject("Invalid recipient length");
        for (const QChar ch : address)
            if (ch.unicode() < 33 || ch.unicode() > 126) return reject("Invalid recipient characters");
    }
    if (t.makerKey.size() != 32 || t.takerKey.size() != 32 || t.makerKey == t.takerKey
        || t.nonce.size() != 32
        || t.offerFillReservationCommitment.size() != 32
        || t.offerFillReservationCommitment == QByteArray(32, '\0'))
        return reject("Invalid identities, nonce, or fill reservation");
    if (t.payerKey != t.makerKey && t.payerKey != t.takerKey)
        return reject("Payer identity must be the maker or taker key");
    if (now < 0 || t.quoteExpiry <= 0
        || (requireFreshQuote && t.quoteExpiry <= now)
        || t.refundDeadline <= t.quoteExpiry)
        return reject("Expired quote or invalid deadline order");
    if (t.feeFromAcquiredXmz != (t.receiveAsset == "XMZ"))
        return reject("Fee source must match XMZ acquisition");
    if (t.feeFromAcquiredXmz && t.feeAtomic.toULongLong() >= t.receiveAmountAtomic.toULongLong())
        return reject("Fee consumes acquired XMZ");

    const auto validCommitmentPair = [](const QByteArray &lock, const QByteArray &refund) {
        return lock.size() == 32 && refund.size() == 32 && lock != QByteArray(32, '\0')
            && refund != QByteArray(32, '\0') && lock != refund;
    };
    if (!validCommitmentPair(t.payLockCommitment, t.payRefundCommitment)
        || !validCommitmentPair(t.receiveLockCommitment, t.receiveRefundCommitment)
        || !validCommitmentPair(t.xmzFeeLockCommitment, t.xmzFeeRefundCommitment))
        return reject("Invalid lock or refund commitment");
    const auto validUtxoRecovery = [&](const QString &asset, const QByteArray &lock,
            const QByteArray &refund, const QByteArray &redeem, const QByteArray &cancel,
            const QByteArray &punish, quint32 cancelBlocks, quint32 punishBlocks,
            int confirmations) {
        if (asset == "XMZ") return redeem.isEmpty() && cancel.isEmpty() && punish.isEmpty()
            && cancelBlocks == 0 && punishBlocks == 0;
        const QList<QByteArray> commitments{lock, refund, redeem, cancel, punish};
        QSet<QByteArray> distinct;
        for (const QByteArray &commitment : commitments) {
            if (commitment.size() != 32 || commitment == QByteArray(32, '\0')) return false;
            distinct.insert(commitment);
        }
        return distinct.size() == commitments.size()
            && cancelBlocks > static_cast<quint32>(confirmations)
            && punishBlocks > static_cast<quint32>(confirmations)
            && cancelBlocks <= 65535 && punishBlocks <= 65535;
    };
    const auto compressedKey = [](const QByteArray &key) {
        return key.size() == 33
            && (static_cast<unsigned char>(key.at(0)) == 0x02
                || static_cast<unsigned char>(key.at(0)) == 0x03);
    };
    const auto validContractKeys = [&](const QString &asset, const QByteArray &a,
                                       const QByteArray &b) {
        if (asset == "XMZ") return a.isEmpty() && b.isEmpty();
        return compressedKey(a) && compressedKey(b) && a != b;
    };
    if (!validContractKeys(t.payAsset, t.payContractKeyA, t.payContractKeyB)
        || !validContractKeys(t.receiveAsset, t.receiveContractKeyA,
                              t.receiveContractKeyB))
        return reject("Invalid chain-specific contract keys");
    const auto nonzero32 = [](const QByteArray &value) {
        return value.size() == 32 && value != QByteArray(32, '\0');
    };
    if (!compressedKey(t.makerAdaptorSecpKey) || !compressedKey(t.takerAdaptorSecpKey)
        || t.makerAdaptorSecpKey == t.takerAdaptorSecpKey
        || !nonzero32(t.makerSwapEd25519Key) || !nonzero32(t.takerSwapEd25519Key)
        || t.makerSwapEd25519Key == t.takerSwapEd25519Key
        || !nonzero32(t.makerDleqProofCommitment)
        || !nonzero32(t.takerDleqProofCommitment)
        || t.makerDleqProofCommitment == t.takerDleqProofCommitment)
        return reject("Invalid adaptor identity or DLEQ commitment");
    for (const int confirmations : {t.payConfirmations, t.receiveConfirmations, t.xmzConfirmations})
        if (confirmations < 1 || confirmations > 1000)
            return reject("Invalid confirmation policy");
    if (!validUtxoRecovery(t.payAsset, t.payLockCommitment, t.payRefundCommitment,
            t.payRedeemCommitment, t.payCancelCommitment, t.payPunishCommitment,
            t.payCancelBlocks, t.payPunishBlocks, t.payConfirmations)
        || !validUtxoRecovery(t.receiveAsset, t.receiveLockCommitment,
            t.receiveRefundCommitment, t.receiveRedeemCommitment,
            t.receiveCancelCommitment, t.receivePunishCommitment,
            t.receiveCancelBlocks, t.receivePunishBlocks, t.receiveConfirmations))
        return reject("Invalid UTXO recovery transaction set or relative timelocks");
    const auto validWindow = [](const QString &anchorText, const QString &refundText,
                                int confirmations) {
        static const QRegularExpression height("^(0|[1-9][0-9]{0,19})$");
        bool anchorOk = false, refundOk = false;
        const quint64 anchor = anchorText.toULongLong(&anchorOk);
        const quint64 refund = refundText.toULongLong(&refundOk);
        return anchorOk && refundOk && height.match(anchorText).hasMatch()
            && height.match(refundText).hasMatch()
            && anchor <= std::numeric_limits<quint64>::max() - static_cast<quint64>(confirmations)
            && refund > anchor + static_cast<quint64>(confirmations);
    };
    if (!validWindow(t.payAnchorHeight, t.payRefundHeight, t.payConfirmations)
        || !validWindow(t.receiveAnchorHeight, t.receiveRefundHeight, t.receiveConfirmations)
        || !validWindow(t.xmzAnchorHeight, t.xmzRefundHeight, t.xmzConfirmations))
        return reject("Invalid chain refund window");

    QByteArray payload("MONZERO-DEX-SETTLEMENT-TERMS-V7\n");
    // Fixed field order and byte-length prefixes avoid delimiter ambiguity.
    const auto append = [&](const QByteArray &value) {
        payload += QByteArray::number(value.size()) + ":" + value;
    };
    for (const auto &value : {t.protocolId, t.payAsset, t.receiveAsset,
         t.payGenesis, t.receiveGenesis, t.xmzGenesis,
         t.payNetwork, t.receiveNetwork, t.xmzNetwork,
         t.payAmountAtomic, t.receiveAmountAtomic, t.feeAtomic,
         t.payNativeFeeMaxAtomic, t.receiveNativeFeeMaxAtomic, t.xmzNativeFeeMaxAtomic,
         t.payRecipient, t.receiveRecipient, t.feeRecipient,
         t.payRefundRecipient, t.receiveRefundRecipient, t.xmzRefundRecipient,
         t.payAnchorHeight, t.receiveAnchorHeight, t.xmzAnchorHeight,
         t.payRefundHeight, t.receiveRefundHeight, t.xmzRefundHeight}) append(value.toUtf8());
    append(QByteArray::number(t.payConfirmations));
    append(QByteArray::number(t.receiveConfirmations));
    append(QByteArray::number(t.xmzConfirmations));
    append(t.payLockCommitment); append(t.payRefundCommitment);
    append(t.receiveLockCommitment); append(t.receiveRefundCommitment);
    append(t.xmzFeeLockCommitment); append(t.xmzFeeRefundCommitment);
    append(t.payRedeemCommitment); append(t.payCancelCommitment);
    append(t.payPunishCommitment); append(t.receiveRedeemCommitment);
    append(t.receiveCancelCommitment); append(t.receivePunishCommitment);
    append(QByteArray::number(t.payCancelBlocks));
    append(QByteArray::number(t.payPunishBlocks));
    append(QByteArray::number(t.receiveCancelBlocks));
    append(QByteArray::number(t.receivePunishBlocks));
    append(t.payContractKeyA); append(t.payContractKeyB);
    append(t.receiveContractKeyA); append(t.receiveContractKeyB);
    append(t.makerAdaptorSecpKey); append(t.makerSwapEd25519Key);
    append(t.makerDleqProofCommitment); append(t.takerAdaptorSecpKey);
    append(t.takerSwapEd25519Key); append(t.takerDleqProofCommitment);
    append(t.makerKey); append(t.takerKey); append(t.payerKey); append(t.nonce);
    append(t.offerFillReservationCommitment);
    append(QByteArray::number(t.quoteExpiry)); append(QByteArray::number(t.refundDeadline));
    append(t.feeFromAcquiredXmz ? "acquired_xmz" : "existing_xmz");
    append("XMZ"); append("success_only");
    r.valid = true;
    r.payload = payload;
    r.commitment = QCryptographicHash::hash(payload, QCryptographicHash::Sha256);
    return r;
}

Result build(const Terms &terms, qint64 now)
{
    return validateAndBuild(terms, now, true);
}

bool decode(const QByteArray &payload, qint64 now, Terms &output)
{
    const QByteArray prefix("MONZERO-DEX-SETTLEMENT-TERMS-V7\n");
    if (payload.size() > 65536 || !payload.startsWith(prefix)) return false;
    int offset = prefix.size();
    bool valid = true;
    const auto read = [&]() -> QByteArray {
        if (!valid) return {};
        const int colon = payload.indexOf(':', offset);
        if (colon < offset || colon - offset > 5) { valid = false; return {}; }
        const auto text = payload.mid(offset, colon - offset);
        bool ok = false;
        const auto size = text.toUInt(&ok);
        if (!ok || text != QByteArray::number(size) || size > 8192
            || size > quint32(payload.size() - colon - 1)) { valid = false; return {}; }
        offset = colon + 1 + int(size);
        return payload.mid(colon + 1, int(size));
    };
    Terms t;
    for (auto *value : {&t.protocolId, &t.payAsset, &t.receiveAsset,
         &t.payGenesis, &t.receiveGenesis, &t.xmzGenesis,
         &t.payNetwork, &t.receiveNetwork, &t.xmzNetwork,
         &t.payAmountAtomic, &t.receiveAmountAtomic, &t.feeAtomic,
         &t.payNativeFeeMaxAtomic, &t.receiveNativeFeeMaxAtomic, &t.xmzNativeFeeMaxAtomic,
         &t.payRecipient, &t.receiveRecipient, &t.feeRecipient,
         &t.payRefundRecipient, &t.receiveRefundRecipient, &t.xmzRefundRecipient,
         &t.payAnchorHeight, &t.receiveAnchorHeight, &t.xmzAnchorHeight,
         &t.payRefundHeight, &t.receiveRefundHeight, &t.xmzRefundHeight})
        *value = QString::fromUtf8(read());
    for (auto *value : {&t.payConfirmations, &t.receiveConfirmations, &t.xmzConfirmations}) {
        bool ok = false; *value = read().toInt(&ok); valid = valid && ok;
    }
    for (auto *value : {&t.payLockCommitment, &t.payRefundCommitment,
         &t.receiveLockCommitment, &t.receiveRefundCommitment,
         &t.xmzFeeLockCommitment, &t.xmzFeeRefundCommitment,
         &t.payRedeemCommitment, &t.payCancelCommitment, &t.payPunishCommitment,
         &t.receiveRedeemCommitment, &t.receiveCancelCommitment, &t.receivePunishCommitment})
        *value = read();
    for (auto *value : {&t.payCancelBlocks, &t.payPunishBlocks,
                       &t.receiveCancelBlocks, &t.receivePunishBlocks}) {
        bool ok = false; *value = read().toUInt(&ok); valid = valid && ok;
    }
    for (auto *value : {&t.payContractKeyA, &t.payContractKeyB,
         &t.receiveContractKeyA, &t.receiveContractKeyB,
         &t.makerAdaptorSecpKey, &t.makerSwapEd25519Key, &t.makerDleqProofCommitment,
         &t.takerAdaptorSecpKey, &t.takerSwapEd25519Key, &t.takerDleqProofCommitment,
         &t.makerKey, &t.takerKey, &t.payerKey, &t.nonce, &t.offerFillReservationCommitment})
        *value = read();
    bool expiryOk = false, deadlineOk = false;
    t.quoteExpiry = read().toLongLong(&expiryOk);
    t.refundDeadline = read().toLongLong(&deadlineOk);
    const auto funding = read();
    t.feeFromAcquiredXmz = funding == "acquired_xmz";
    if (funding != "acquired_xmz" && funding != "existing_xmz") return false;
    if (read() != "XMZ" || read() != "success_only" || !valid
        || !expiryOk || !deadlineOk || offset != payload.size()) return false;
    const auto rebuilt = build(t, now);
    if (!rebuilt.valid || rebuilt.payload != payload) return false;
    output = t;
    return true;
}

Result rebuildAccepted(const Terms &terms)
{
    return validateAndBuild(terms, 0, false);
}
}
