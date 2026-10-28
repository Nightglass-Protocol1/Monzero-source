#include "BitcoinRecoveryMempoolVerifier.h"

#include <QCryptographicHash>
#include <QDataStream>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <cmath>
#include <limits>

namespace {
const QRegularExpression hash64("^[0-9a-f]{64}$");

QByteArray field(const QByteArray &value)
{
    QByteArray encoded;
    QDataStream stream(&encoded, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    stream << quint32(value.size());
    encoded += value;
    return encoded;
}

bool exactPositiveInteger(const QJsonValue &input, quint64 maximum, quint64 &value)
{
    if (!input.isDouble()) return false;
    const double number = input.toDouble();
    if (!std::isfinite(number) || number < 1 || number != std::floor(number)
        || number > static_cast<double>(maximum)) return false;
    value = static_cast<quint64>(number);
    return true;
}

bool bitcoinAmount(const QJsonValue &input, quint64 &satoshis)
{
    if (!input.isDouble()) return false;
    const double coins = input.toDouble();
    const double scaled = coins * 100000000.0;
    const double rounded = std::round(scaled);
    if (!std::isfinite(coins) || coins < 0 || coins > 21000000.0
        || std::fabs(scaled - rounded) > 0.000001) return false;
    satoshis = static_cast<quint64>(rounded);
    return true;
}
}

namespace BitcoinRecoveryMempoolVerifier {
Approval validate(const ReadOnlyRpcClient &client,
                  const BitcoinSignedRecoveryEvidence::Evidence &signedEvidence,
                  const QByteArray &rawTransactionHex,
                  const QByteArray &authorization)
{
    Approval approval;
    approval.m_details = {{"valid", false}, {"mempoolPolicyVerified", false},
                          {"executable", false}};
    const QByteArray raw = QByteArray::fromHex(rawTransactionHex);
    if (!client.configured() || !signedEvidence.accepted() || raw.isEmpty()
        || raw.toHex() != rawTransactionHex
        || QCryptographicHash::hash(raw, QCryptographicHash::Sha256)
            != signedEvidence.rawTransactionCommitment()) {
        approval.m_details["error"] = "Mempool check does not match signed recovery artifact";
        return approval;
    }

    const auto firstTip = client.call("getbestblockhash", QJsonArray{}, authorization);
    if (!firstTip.ok || !firstTip.payload.isString()
        || !hash64.match(firstTip.payload.toString()).hasMatch()) {
        approval.m_details["error"] = "Cannot pin Bitcoin mempool policy tip";
        return approval;
    }
    const QString bestBlock = firstTip.payload.toString();
    const auto tested = client.call("testmempoolaccept",
        QJsonArray{QJsonArray{QString::fromLatin1(rawTransactionHex)}}, authorization);
    const auto finalTip = client.call("getbestblockhash", QJsonArray{}, authorization);
    if (!finalTip.ok || finalTip.payload != QJsonValue(bestBlock)) {
        approval.m_details["error"] = "Bitcoin tip changed during mempool policy validation";
        return approval;
    }
    if (!tested.ok || !tested.payload.isArray()
        || tested.payload.toArray().size() != 1
        || !tested.payload.toArray()[0].isObject()) {
        approval.m_details["error"] = "Malformed Bitcoin mempool policy response";
        return approval;
    }
    const QJsonObject result = tested.payload.toArray()[0].toObject();
    quint64 virtualSize = 0, feeAtomic = 0;
    bool expectedFeeOk = false;
    const quint64 expectedFee = signedEvidence.feeAtomic().toULongLong(&expectedFeeOk);
    if (result.value("allowed") != QJsonValue(true)
        || result.value("txid") != QJsonValue(signedEvidence.txid())
        || result.value("wtxid") != QJsonValue(signedEvidence.wtxid())
        || !exactPositiveInteger(result.value("vsize"), 4000000, virtualSize)
        || virtualSize != signedEvidence.virtualSize()
        || !result.value("fees").isObject()
        || !bitcoinAmount(result.value("fees").toObject().value("base"), feeAtomic)
        || !expectedFeeOk || feeAtomic != expectedFee) {
        approval.m_details["error"] = "Bitcoin Core did not accept the exact signed recovery transaction";
        return approval;
    }

    approval.m_signedCommitment = signedEvidence.journalCommitment();
    approval.m_txid = signedEvidence.txid();
    approval.m_wtxid = signedEvidence.wtxid();
    approval.m_bestBlock = bestBlock;
    QByteArray preimage("MONZERO-DEX-BTC-RECOVERY-MEMPOOL-V1\n");
    for (const QByteArray &value : {approval.m_signedCommitment,
             signedEvidence.rawTransactionCommitment(), approval.m_txid.toLatin1(),
             approval.m_wtxid.toLatin1(), bestBlock.toLatin1(),
             QByteArray::number(virtualSize), QByteArray::number(feeAtomic)})
        preimage += field(value);
    approval.m_commitment =
        QCryptographicHash::hash(preimage, QCryptographicHash::Sha256);
    approval.m_details = {{"valid", true}, {"mempoolPolicyVerified", true},
                          {"tipPinned", true},
                          {"vsize", QString::number(virtualSize)},
                          {"feeAtomic", QString::number(feeAtomic)},
                          {"executable", false}};
    return approval;
}
}
