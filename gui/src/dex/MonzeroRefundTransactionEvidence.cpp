#include "MonzeroRefundTransactionEvidence.h"
#include "NodeRpcValidator.h"

#include <QCryptographicHash>
#include <QDataStream>
#include <QJsonArray>
#include <QRegularExpression>
#include <cmath>

namespace {
const QRegularExpression hash64("^[0-9a-f]{64}$");
const QRegularExpression atomicPattern("^(0|[1-9][0-9]{0,15})$");
constexpr quint64 maximumExactJsonInteger = 9007199254740991ULL;

QByteArray field(const QByteArray &value)
{
    QByteArray encoded;
    QDataStream stream(&encoded, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    stream << quint32(value.size());
    encoded += value;
    return encoded;
}

QVariantMap rejected(const QString &error)
{
    return {{"valid", false}, {"confirmed", false}, {"refunded", false},
            {"executable", false}, {"error", error}};
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

QString legName(MonzeroRefundTransactionEvidence::Leg leg)
{
    return leg == MonzeroRefundTransactionEvidence::Leg::ReceivePrincipal
        ? "receive" : "xmz_fee";
}
}

namespace MonzeroRefundTransactionEvidence {
QByteArray intentCommitment(Leg leg, const QString &network,
    const QString &genesis, const QString &recipient, const QString &amountAtomic,
    const QString &maximumNativeFeeAtomic, const QString &refundHeight)
{
    QByteArray preimage("MONZERO-DEX-XMZ-REFUND-INTENT-V1\n");
    for (const QByteArray &value : {legName(leg).toUtf8(), network.toUtf8(),
             genesis.toLatin1(), recipient.toUtf8(), amountAtomic.toLatin1(),
             maximumNativeFeeAtomic.toLatin1(), refundHeight.toLatin1()})
        preimage += field(value);
    return QCryptographicHash::hash(preimage, QCryptographicHash::Sha256);
}

Evidence validateEvidence(const SettlementTerms::Terms &terms, Leg leg, qint64 now,
    const QString &observedGenesis, const QString &expectedTxid,
    const QJsonObject &walletResult, const QJsonObject &blockResult,
    const QJsonObject &nodeInfo, const QString &expectedNetwork)
{
    Evidence evidence;
    evidence.m_details = rejected("Invalid signed XMZ refund policy");
    const SettlementTerms::Result signedTerms = SettlementTerms::rebuildAccepted(terms);
    if (now < terms.refundDeadline || !signedTerms.valid
        || terms.payAsset != "BTC" || terms.receiveAsset != "XMZ"
        || terms.receiveNetwork != terms.xmzNetwork
        || terms.receiveNetwork != expectedNetwork
        || terms.receiveGenesis != terms.xmzGenesis
        || !NodeRpcValidator::monzero(nodeInfo, observedGenesis, terms.xmzGenesis)
                .value("ready").toBool()) return evidence;

    const bool principal = leg == Leg::ReceivePrincipal;
    const QString recipient = principal ? terms.receiveRefundRecipient
                                        : terms.xmzRefundRecipient;
    const QString amountText = principal ? terms.receiveAmountAtomic : terms.feeAtomic;
    const QString maximumFeeText = principal ? terms.receiveNativeFeeMaxAtomic
                                             : terms.xmzNativeFeeMaxAtomic;
    const QString refundHeightText = principal ? terms.receiveRefundHeight
                                               : terms.xmzRefundHeight;
    const QByteArray signedIntent = principal ? terms.receiveRefundCommitment
                                              : terms.xmzFeeRefundCommitment;
    quint64 amount = 0, maximumFee = 0, refundHeight = 0, nodeHeight = 0;
    const QByteArray expectedIntent = intentCommitment(leg, terms.xmzNetwork,
        terms.xmzGenesis, recipient, amountText, maximumFeeText, refundHeightText);
    if (!hash64.match(expectedTxid).hasMatch() || !address(recipient)
        || !atomic(amountText, amount) || !atomic(maximumFeeText, maximumFee, false)
        || !atomic(refundHeightText, refundHeight, false)
        || signedIntent != expectedIntent
        || !exactInteger(nodeInfo.value("height"), nodeHeight) || nodeHeight == 0
        || nodeHeight - 1 < refundHeight || !walletResult.value("transfer").isObject())
        return evidence;

    const QJsonObject transfer = walletResult.value("transfer").toObject();
    quint64 sent = 0, nativeFee = 0, height = 0, confirmations = 0;
    if (transfer.value("txid") != QJsonValue(expectedTxid)
        || transfer.value("type") != QJsonValue("out")
        || transfer.value("double_spend_seen") != QJsonValue(false)
        || transfer.value("locked") != QJsonValue(false)
        || !exactInteger(transfer.value("amount"), sent) || sent != amount
        || !exactInteger(transfer.value("fee"), nativeFee) || nativeFee > maximumFee
        || !exactInteger(transfer.value("height"), height) || height == 0
        || !exactInteger(transfer.value("confirmations"), confirmations)
        || height > nodeHeight - 1 || confirmations != nodeHeight - height
        || confirmations < static_cast<quint64>(terms.xmzConfirmations)
        || !transfer.value("destinations").isArray()) return evidence;
    const QJsonArray destinations = transfer.value("destinations").toArray();
    if (destinations.size() != 1 || !destinations.first().isObject()) return evidence;
    const QJsonObject destination = destinations.first().toObject();
    quint64 destinationAmount = 0;
    if (destination.size() != 2 || destination.value("address") != recipient
        || !exactInteger(destination.value("amount"), destinationAmount)
        || destinationAmount != amount || blockResult.value("status") != "OK"
        || blockResult.value("untrusted") != QJsonValue(false)
        || !blockResult.value("block_header").isObject()) return evidence;
    const QJsonObject header = blockResult.value("block_header").toObject();
    quint64 blockHeight = 0;
    const QString blockHash = header.value("hash").toString();
    if (!hash64.match(blockHash).hasMatch()
        || !exactInteger(header.value("height"), blockHeight) || blockHeight != height
        || header.value("orphan_status") != QJsonValue(false)) return evidence;
    bool included = blockResult.value("miner_tx_hash") == expectedTxid;
    if (blockResult.value("tx_hashes").isArray())
        for (const QJsonValue &candidate : blockResult.value("tx_hashes").toArray())
            if (candidate == expectedTxid) included = true;
    if (!included) return evidence;

    evidence.m_termsCommitment = signedTerms.commitment;
    evidence.m_intentCommitment = expectedIntent;
    evidence.m_transactionId = expectedTxid;
    evidence.m_chain = terms.xmzNetwork;
    evidence.m_leg = legName(leg);
    QByteArray preimage("MONZERO-DEX-XMZ-REFUND-EVIDENCE-V1\n");
    for (const QByteArray &value : {evidence.m_termsCommitment,
             evidence.m_intentCommitment, evidence.m_leg.toUtf8(),
             evidence.m_chain.toUtf8(), observedGenesis.toLatin1(),
             expectedTxid.toLatin1(), blockHash.toLatin1(),
             QByteArray::number(blockHeight), QByteArray::number(nodeHeight - 1),
             QByteArray::number(confirmations), QByteArray::number(nativeFee)})
        preimage += field(value);
    evidence.m_commitment = QCryptographicHash::hash(preimage,
        QCryptographicHash::Sha256);
    evidence.m_details = {{"valid", true}, {"confirmed", true},
        {"refunded", true}, {"leg", evidence.m_leg},
        {"transactionId", expectedTxid}, {"blockHash", blockHash},
        {"blockHeight", QString::number(blockHeight)},
        {"nodeHeight", QString::number(nodeHeight - 1)},
        {"confirmations", QString::number(confirmations)},
        {"termsVerified", true}, {"intentVerified", true},
        {"nodeIdentityVerified", true}, {"executable", false}};
    return evidence;
}
}
