#include "BitcoinRecoveryMaturityVerifier.h"

#include "NodeRpcValidator.h"

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

bool integer(const QJsonValue &value, quint64 &result)
{
    if (!value.isDouble()) return false;
    const double number = value.toDouble();
    if (!std::isfinite(number) || number < 0 || number != std::floor(number)
        || number > static_cast<double>(std::numeric_limits<quint32>::max()))
        return false;
    result = static_cast<quint64>(number);
    return true;
}
}

namespace BitcoinRecoveryMaturityVerifier {
Evidence validate(const ReadOnlyRpcClient &client,
                  const BitcoinRecoveryPsbtRpcVerifier::Evidence &prepared,
                  const QByteArray &authorization)
{
    Evidence evidence;
    evidence.m_details = {{"valid", false}, {"mature", false},
                          {"executable", false}};
    bool voutOk = false, parentHeightOk = false;
    const quint64 vout = prepared.parentVoutAtomic().toULongLong(&voutOk);
    const quint64 preparedParentHeight =
        prepared.parentBlockHeight().toULongLong(&parentHeightOk);
    if (!client.configured() || !prepared.accepted()
        || (prepared.branch() != "cancel" && prepared.branch() != "punish")
        || prepared.relativeBlocks() == 0 || !voutOk || !parentHeightOk
        || prepared.parentVoutAtomic() != QString::number(vout)
        || vout > std::numeric_limits<quint32>::max()
        || !hash64.match(prepared.parentTxid()).hasMatch()) {
        evidence.m_details["error"] = "Invalid relative-timelock recovery evidence";
        return evidence;
    }

    const auto firstTip = client.call("getbestblockhash", QJsonArray{}, authorization);
    const auto infoResult = client.call("getblockchaininfo", QJsonArray{}, authorization);
    const auto genesisResult = client.call("getblockhash", QJsonArray{0}, authorization);
    if (!firstTip.ok || !firstTip.payload.isString()
        || !hash64.match(firstTip.payload.toString()).hasMatch()
        || !infoResult.ok || !infoResult.payload.isObject()
        || !genesisResult.ok || !genesisResult.payload.isString()
        || !NodeRpcValidator::bitcoin(infoResult.payload.toObject(),
            genesisResult.payload.toString(), prepared.expectedGenesis())
                .value("ready").toBool()) {
        evidence.m_details["error"] = "Bitcoin maturity node identity mismatch";
        return evidence;
    }
    const QString tip = firstTip.payload.toString();
    quint64 tipHeight = 0;
    const QJsonObject info = infoResult.payload.toObject();
    if (info.value("bestblockhash") != QJsonValue(tip)
        || !integer(info.value("blocks"), tipHeight)
        || tipHeight == std::numeric_limits<quint32>::max()) {
        evidence.m_details["error"] = "Bitcoin maturity tip is inconsistent";
        return evidence;
    }

    const auto outputResult = client.call("gettxout", QJsonArray{
        prepared.parentTxid(), static_cast<double>(vout), true}, authorization);
    if (!outputResult.ok || !outputResult.payload.isObject()) {
        evidence.m_details["error"] = "Bitcoin recovery parent is spent or missing";
        return evidence;
    }
    const QJsonObject output = outputResult.payload.toObject();
    quint64 confirmations = 0;
    if (output.value("bestblock") != QJsonValue(tip)
        || output.value("coinbase") != QJsonValue(false)
        || !integer(output.value("confirmations"), confirmations)
        || confirmations == 0 || confirmations > tipHeight + 1) {
        evidence.m_details["error"] = "Bitcoin recovery parent age is malformed";
        return evidence;
    }
    const quint64 liveParentHeight = tipHeight - confirmations + 1;
    if (liveParentHeight != preparedParentHeight
        || preparedParentHeight > std::numeric_limits<quint64>::max()
               - prepared.relativeBlocks()) {
        evidence.m_details["error"] = "Bitcoin recovery parent height changed";
        return evidence;
    }
    const auto finalTip = client.call("getbestblockhash", QJsonArray{}, authorization);
    if (!finalTip.ok || finalTip.payload != QJsonValue(tip)) {
        evidence.m_details["error"] = "Bitcoin maturity tip changed during validation";
        return evidence;
    }

    const quint64 earliest = preparedParentHeight + prepared.relativeBlocks();
    const quint64 candidate = tipHeight + 1;
    const quint64 remaining = candidate >= earliest ? 0 : earliest - candidate;
    evidence.m_details = {{"valid", true}, {"mature", remaining == 0},
        {"parentBlockHeight", QString::number(liveParentHeight)},
        {"tipHeight", QString::number(tipHeight)},
        {"candidateBlockHeight", QString::number(candidate)},
        {"earliestBlockHeight", QString::number(earliest)},
        {"blocksRemaining", QString::number(remaining)},
        {"tipPinned", true}, {"nodeIdentityVerified", true},
        {"parentUnspent", true}, {"executable", false}};
    if (remaining != 0) return evidence;

    evidence.m_preparedCommitment = prepared.journalCommitment();
    evidence.m_leg = prepared.leg();
    evidence.m_branch = prepared.branch();
    evidence.m_chain = prepared.chain();
    evidence.m_earliestBlockHeight = QString::number(earliest);
    QByteArray preimage("MONZERO-DEX-BTC-RECOVERY-MATURITY-V1\n");
    for (const QByteArray &value : {prepared.settlementTermsCommitment(),
             evidence.m_preparedCommitment, evidence.m_leg.toUtf8(),
             evidence.m_chain.toUtf8(),
             evidence.m_branch.toUtf8(), prepared.parentTxid().toLatin1(),
             prepared.parentVoutAtomic().toLatin1(),
             prepared.parentBlockHeight().toLatin1(),
             QByteArray::number(prepared.relativeBlocks()), tip.toLatin1(),
             QByteArray::number(tipHeight), QByteArray::number(candidate),
             QByteArray::number(earliest)})
        preimage += field(value);
    evidence.m_commitment =
        QCryptographicHash::hash(preimage, QCryptographicHash::Sha256);
    return evidence;
}
}
