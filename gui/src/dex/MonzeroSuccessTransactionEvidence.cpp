#include "MonzeroSuccessTransactionEvidence.h"
#include "NodeRpcValidator.h"

#include <QCryptographicHash>
#include <QDataStream>
#include <QJsonArray>
#include <QRegularExpression>
#include <algorithm>
#include <cmath>
#include <limits>

namespace {
const QRegularExpression hash64("^[0-9a-f]{64}$");
const QRegularExpression atomicPattern("^(0|[1-9][0-9]{0,15})$");
constexpr quint64 maximumExactJsonInteger = 9007199254740991ULL;

QVariantMap rejected(const QString &error)
{
    return {{"valid", false}, {"confirmed", false}, {"feeEarned", false},
            {"feeEnforcementVerified", false}, {"executable", false}, {"error", error}};
}

bool exactInteger(const QJsonValue &input, quint64 &value)
{
    if (!input.isDouble()) return false;
    const double number = input.toDouble();
    if (!std::isfinite(number) || number < 0 || number != std::floor(number)
        || number > static_cast<double>(maximumExactJsonInteger)) return false;
    value = static_cast<quint64>(number);
    return true;
}

bool atomic(const QString &text, quint64 &value, bool positive = true)
{
    bool ok = false;
    value = text.toULongLong(&ok);
    return ok && atomicPattern.match(text).hasMatch()
        && value <= maximumExactJsonInteger && (!positive || value != 0);
}

bool address(const QString &value)
{
    if (value.isEmpty() || value.toUtf8().size() > 256) return false;
    for (const QChar character : value)
        if (character.unicode() < 33 || character.unicode() > 126) return false;
    return true;
}

QByteArray field(const QByteArray &value)
{
    QByteArray encoded;
    QDataStream stream(&encoded, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    stream << quint32(value.size());
    encoded += value;
    return encoded;
}
}

namespace MonzeroSuccessTransactionEvidence {
QVariantMap validate(const QJsonObject &walletResult, const QJsonObject &blockResult,
                     const QJsonObject &nodeInfo, const QString &expectedTxid,
                     const QString &deliveryAddress, const QString &deliveryAmountText,
                     const QString &feeAddress, const QString &feeAmountText,
                     const QString &maximumNativeFeeText, int requiredConfirmations)
{
    quint64 deliveryAmount = 0, feeAmount = 0, maximumNativeFee = 0, nodeHeight = 0;
    if (!hash64.match(expectedTxid).hasMatch() || !address(deliveryAddress)
        || !address(feeAddress) || deliveryAddress == feeAddress
        || !atomic(deliveryAmountText, deliveryAmount)
        || !atomic(feeAmountText, feeAmount)
        || !atomic(maximumNativeFeeText, maximumNativeFee, false)
        || deliveryAmount > maximumExactJsonInteger - feeAmount
        || requiredConfirmations < 1 || requiredConfirmations > 1000)
        return rejected("Invalid Monzero success-payment policy");
    if (nodeInfo.value("status") != QJsonValue("OK")
        || nodeInfo.value("synchronized") != QJsonValue(true)
        || nodeInfo.value("untrusted") != QJsonValue(false)
        || nodeInfo.value("nettype") != QJsonValue("fakechain")
        || !exactInteger(nodeInfo.value("height"), nodeHeight) || nodeHeight == 0)
        return rejected("Untrusted or unsynchronized Monzero node evidence");
    if (!walletResult.value("transfer").isObject())
        return rejected("Missing Monzero sender-wallet evidence");

    const QJsonObject transfer = walletResult.value("transfer").toObject();
    quint64 amount = 0, nativeFee = 0, height = 0, confirmations = 0;
    const QString transferType = transfer.value("type").toString();
    if (transfer.value("txid") != QJsonValue(expectedTxid)
        || (transferType != "out" && transferType != "pending")
        || transfer.value("double_spend_seen") != QJsonValue(false)
        || !exactInteger(transfer.value("amount"), amount)
        || !exactInteger(transfer.value("fee"), nativeFee)
        || !exactInteger(transfer.value("height"), height)
        || !exactInteger(transfer.value("confirmations"), confirmations)
        || amount != deliveryAmount + feeAmount || nativeFee > maximumNativeFee
        || !transfer.value("destinations").isArray())
        return rejected("Monzero sender transaction does not match committed amounts");

    const QJsonArray destinations = transfer.value("destinations").toArray();
    if (destinations.size() != 2) return rejected("Unexpected Monzero success destinations");
    bool deliveryFound = false, feeFound = false;
    for (const QJsonValue &value : destinations) {
        if (!value.isObject()) return rejected("Malformed Monzero success destination");
        const QJsonObject destination = value.toObject();
        quint64 destinationAmount = 0;
        if (destination.size() != 2 || !exactInteger(destination.value("amount"), destinationAmount))
            return rejected("Malformed Monzero success destination");
        if (destination.value("address") == QJsonValue(deliveryAddress)
            && destinationAmount == deliveryAmount && !deliveryFound) deliveryFound = true;
        else if (destination.value("address") == QJsonValue(feeAddress)
                 && destinationAmount == feeAmount && !feeFound) feeFound = true;
        else return rejected("Monzero success destination mismatch");
    }
    if (!deliveryFound || !feeFound)
        return rejected("Missing Monzero delivery or fee destination");

    const quint64 tipHeight = nodeHeight - 1;
    const bool pending = transferType == "pending";
    if (pending) {
        if (!blockResult.isEmpty() || height != 0 || confirmations != 0
            || transfer.value("locked") != QJsonValue(true))
            return rejected("Inconsistent pending Monzero success evidence");
        return {{"valid", true}, {"confirmed", false}, {"feeEarned", false},
                {"feeEnforcementVerified", false},
                {"nativeFeeAtomic", QString::number(nativeFee)}, {"executable", false}};
    }
    if (transfer.value("locked") != QJsonValue(false) || height == 0
        || confirmations == 0 || height > tipHeight || confirmations != nodeHeight - height
        || blockResult.value("status") != QJsonValue("OK")
        || blockResult.value("untrusted") != QJsonValue(false)
        || !blockResult.value("block_header").isObject())
        return rejected("Inconsistent confirmed Monzero success evidence");
    const QJsonObject header = blockResult.value("block_header").toObject();
    quint64 blockHeight = 0;
    if (!hash64.match(header.value("hash").toString()).hasMatch()
        || !exactInteger(header.value("height"), blockHeight) || blockHeight != height
        || header.value("orphan_status") != QJsonValue(false))
        return rejected("Invalid Monzero success block evidence");
    bool included = blockResult.value("miner_tx_hash") == QJsonValue(expectedTxid);
    if (blockResult.value("tx_hashes").isArray())
        for (const QJsonValue &candidate : blockResult.value("tx_hashes").toArray())
            if (candidate == QJsonValue(expectedTxid)) included = true;
    if (!included) return rejected("Monzero success transaction is absent from its block");

    const bool confirmed = confirmations >= static_cast<quint64>(requiredConfirmations);
    return {{"valid", true}, {"confirmed", confirmed}, {"feeEarned", confirmed},
            // Observing these outputs does not constrain alternate spends by
            // whoever ultimately controls the complete shared spend key.
            {"feeEnforcementVerified", false},
            {"transactionId", expectedTxid}, {"confirmations", QString::number(confirmations)},
            {"blockHash", header.value("hash").toString()},
            {"blockHeight", QString::number(blockHeight)},
            {"nodeHeight", QString::number(tipHeight)},
            {"deliveryAtomic", deliveryAmountText}, {"protocolFeeAtomic", feeAmountText},
            {"nativeFeeAtomic", QString::number(nativeFee)}, {"executable", false}};
}

Evidence validateEvidence(const SettlementTerms::Terms &terms, qint64 now,
                          const QString &observedGenesis,
                          const QString &expectedTransactionId,
                          const QJsonObject &walletResult,
                          const QJsonObject &blockResult,
                          const QJsonObject &nodeInfo,
                          const QString &expectedNetwork)
{
    Evidence evidence;
    evidence.m_details = rejected("Invalid signed XMZ success-payment policy");
    if (now < 0) return evidence;
    const SettlementTerms::Result signedTerms = SettlementTerms::rebuildAccepted(terms);
    if (!signedTerms.valid || terms.payAsset != "BTC" || terms.receiveAsset != "XMZ"
        || terms.receiveNetwork != terms.xmzNetwork
        || terms.receiveNetwork != expectedNetwork
        || terms.receiveGenesis != terms.xmzGenesis
        || !NodeRpcValidator::monzero(nodeInfo, observedGenesis, terms.xmzGenesis)
                .value("ready").toBool()) return evidence;
    const int requiredConfirmations = std::max(terms.receiveConfirmations,
                                               terms.xmzConfirmations);
    const QVariantMap receiveCeiling = validate(walletResult, blockResult, nodeInfo,
        expectedTransactionId, terms.receiveRecipient, terms.receiveAmountAtomic,
        terms.feeRecipient, terms.feeAtomic, terms.receiveNativeFeeMaxAtomic,
        requiredConfirmations);
    const QVariantMap feeCeiling = validate(walletResult, blockResult, nodeInfo,
        expectedTransactionId, terms.receiveRecipient, terms.receiveAmountAtomic,
        terms.feeRecipient, terms.feeAtomic, terms.xmzNativeFeeMaxAtomic,
        requiredConfirmations);
    evidence.m_details = receiveCeiling;
    if (!receiveCeiling.value("valid").toBool()
        || !feeCeiling.value("valid").toBool()
        || !receiveCeiling.value("confirmed").toBool()
        || !receiveCeiling.value("feeEarned").toBool()
        || receiveCeiling.value("executable").toBool()) return Evidence{};
    evidence.m_termsCommitment = signedTerms.commitment;
    evidence.m_transactionId = expectedTransactionId;
    evidence.m_blockHash = receiveCeiling.value("blockHash").toString();
    evidence.m_chain = terms.xmzNetwork;
    QByteArray preimage("MONZERO-DEX-XMZ-SUCCESS-EVIDENCE-V1\n");
    for (const QByteArray &value : {evidence.m_termsCommitment,
             evidence.m_chain.toUtf8(), observedGenesis.toLatin1(),
             evidence.m_transactionId.toLatin1(), evidence.m_blockHash.toLatin1(),
             receiveCeiling.value("blockHeight").toString().toLatin1(),
             receiveCeiling.value("nodeHeight").toString().toLatin1(),
             receiveCeiling.value("confirmations").toString().toLatin1(),
             terms.receiveRecipient.toUtf8(), terms.receiveAmountAtomic.toLatin1(),
             terms.feeRecipient.toUtf8(), terms.feeAtomic.toLatin1(),
             receiveCeiling.value("nativeFeeAtomic").toString().toLatin1(),
             terms.receiveNativeFeeMaxAtomic.toLatin1(),
             terms.xmzNativeFeeMaxAtomic.toLatin1(),
             QByteArray::number(requiredConfirmations)})
        preimage += field(value);
    evidence.m_commitment =
        QCryptographicHash::hash(preimage, QCryptographicHash::Sha256);
    evidence.m_details["termsVerified"] = true;
    evidence.m_details["nodeIdentityVerified"] = true;
    evidence.m_details["bothNativeFeeCeilingsVerified"] = true;
    evidence.m_details["executable"] = false;
    return evidence;
}
}
