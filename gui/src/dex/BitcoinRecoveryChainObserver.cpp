#include "BitcoinRecoveryChainObserver.h"

#include "NodeRpcValidator.h"

#include <QCryptographicHash>
#include <QDataStream>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <cmath>

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
        || number > 9007199254740991.0) return false;
    result = static_cast<quint64>(number);
    return true;
}
}

namespace BitcoinRecoveryChainObserver {
Observation observe(const ReadOnlyRpcClient &client,
                    const BitcoinSignedRecoveryEvidence::Evidence &signedEvidence,
                    const QByteArray &rawTransactionHex,
                    const QByteArray &authorization)
{
    Observation observation;
    observation.m_details = {{"valid", false}, {"found", false},
                             {"confirmed", false}, {"executable", false}};
    const QByteArray raw = QByteArray::fromHex(rawTransactionHex);
    if (!client.configured() || !signedEvidence.accepted()
        || signedEvidence.settlementTermsCommitment().size() != 32 || raw.isEmpty()
        || raw.toHex() != rawTransactionHex
        || QCryptographicHash::hash(raw, QCryptographicHash::Sha256)
            != signedEvidence.rawTransactionCommitment()) {
        observation.m_details["error"] = "Chain observation does not match signed recovery artifact";
        return observation;
    }

    const auto firstTip = client.call("getbestblockhash", QJsonArray{}, authorization);
    const auto infoResult = client.call("getblockchaininfo", QJsonArray{}, authorization);
    const auto genesisResult = client.call("getblockhash", QJsonArray{0}, authorization);
    if (!firstTip.ok || !firstTip.payload.isString()
        || !hash64.match(firstTip.payload.toString()).hasMatch()
        || !infoResult.ok || !infoResult.payload.isObject()
        || !genesisResult.ok || !genesisResult.payload.isString()
        || !NodeRpcValidator::bitcoin(infoResult.payload.toObject(),
                                      genesisResult.payload.toString(),
                                      signedEvidence.expectedGenesis())
                .value("ready").toBool()) {
        observation.m_details["error"] = "Bitcoin observation node identity or synchronization mismatch";
        return observation;
    }
    const QString tip = firstTip.payload.toString();
    const QJsonObject info = infoResult.payload.toObject();
    quint64 nodeHeight = 0;
    if (info.value("bestblockhash") != QJsonValue(tip)
        || !integer(info.value("blocks"), nodeHeight)) {
        observation.m_details["error"] = "Bitcoin observation tip and chain information disagree";
        return observation;
    }

    const auto rawResult = client.call("getrawtransaction",
        QJsonArray{signedEvidence.txid(), false}, authorization);
    const auto verboseResult = client.call("getrawtransaction",
        QJsonArray{signedEvidence.txid(), true}, authorization);
    const bool missing = rawResult.rpcError && rawResult.rpcErrorCode == -5
        && verboseResult.rpcError && verboseResult.rpcErrorCode == -5;
    if (missing) {
        const auto finalTip = client.call("getbestblockhash", QJsonArray{}, authorization);
        if (!finalTip.ok || finalTip.payload != QJsonValue(tip)) {
            observation.m_details["error"] =
                "Bitcoin tip changed during missing recovery observation";
            return observation;
        }
        observation.m_signedCommitment = signedEvidence.journalCommitment();
        observation.m_settlementTermsCommitment =
            signedEvidence.settlementTermsCommitment();
        observation.m_txid = signedEvidence.txid();
        observation.m_chain = signedEvidence.chain();
        observation.m_leg = signedEvidence.leg();
        observation.m_branch = signedEvidence.branch();
        observation.m_blockHeight = "0";
        observation.m_confirmations = "0";
        observation.m_nodeHeight = QString::number(nodeHeight);
        observation.m_requiredConfirmations = signedEvidence.requiredConfirmations();
        QByteArray broadcastPreimage("MONZERO-DEX-BTC-RECOVERY-BROADCAST-OBSERVED-V2\n");
        for (const QByteArray &value : {observation.m_settlementTermsCommitment,
                 observation.m_signedCommitment,
                 signedEvidence.rawTransactionCommitment(), observation.m_txid.toLatin1(),
                 signedEvidence.wtxid().toLatin1(), observation.m_chain.toUtf8(),
                 observation.m_leg.toUtf8(), observation.m_branch.toUtf8()})
            broadcastPreimage += field(value);
        observation.m_broadcastCommitment =
            QCryptographicHash::hash(broadcastPreimage, QCryptographicHash::Sha256);
        QByteArray preimage("MONZERO-DEX-BTC-RECOVERY-MISSING-V2\n");
        for (const QByteArray &value : {observation.m_broadcastCommitment,
                 tip.toLatin1(), QByteArray::number(nodeHeight)})
            preimage += field(value);
        observation.m_commitment =
            QCryptographicHash::hash(preimage, QCryptographicHash::Sha256);
        observation.m_details = {{"valid", true}, {"found", false},
            {"nodeHeight", observation.m_nodeHeight}, {"confirmed", false},
            {"tipPinned", true}, {"nodeIdentityVerified", true},
            {"executable", false}};
        return observation;
    }
    if (!rawResult.ok || rawResult.payload != QJsonValue(QString::fromLatin1(rawTransactionHex))
        || !verboseResult.ok || !verboseResult.payload.isObject()) {
        observation.m_details["error"] = "Bitcoin node did not return the exact signed recovery transaction";
        return observation;
    }
    const QJsonObject transaction = verboseResult.payload.toObject();
    if (transaction.value("txid") != QJsonValue(signedEvidence.txid())
        || transaction.value("hash") != QJsonValue(signedEvidence.wtxid())) {
        observation.m_details["error"] = "Bitcoin observed transaction identity mismatch";
        return observation;
    }

    QString blockHash;
    quint64 blockHeight = 0, confirmations = 0;
    if (transaction.contains("blockhash")) {
        blockHash = transaction.value("blockhash").toString();
        const auto headerResult = client.call("getblockheader",
            QJsonArray{blockHash, true}, authorization);
        if (!hash64.match(blockHash).hasMatch() || !headerResult.ok
            || !headerResult.payload.isObject()) {
            observation.m_details["error"] = "Cannot obtain Bitcoin recovery block evidence";
            return observation;
        }
        const QJsonObject header = headerResult.payload.toObject();
        quint64 headerConfirmations = 0, transactionConfirmations = 0;
        if (header.value("hash") != QJsonValue(blockHash)
            || !integer(header.value("height"), blockHeight)
            || !integer(header.value("confirmations"), headerConfirmations)
            || !integer(transaction.value("confirmations"), transactionConfirmations)
            || transaction.value("in_active_chain") != QJsonValue(true)
            || blockHeight > nodeHeight || headerConfirmations == 0
            || transactionConfirmations != headerConfirmations
            || transactionConfirmations != nodeHeight - blockHeight + 1) {
            observation.m_details["error"] = "Inconsistent Bitcoin recovery confirmation evidence";
            return observation;
        }
        confirmations = transactionConfirmations;
    } else if ((transaction.contains("confirmations")
                   && transaction.value("confirmations") != QJsonValue(0))
               || transaction.contains("in_active_chain")) {
        observation.m_details["error"] = "Inconsistent Bitcoin recovery mempool evidence";
        return observation;
    }

    const auto finalTip = client.call("getbestblockhash", QJsonArray{}, authorization);
    if (!finalTip.ok || finalTip.payload != QJsonValue(tip)) {
        observation.m_details["error"] = "Bitcoin tip changed during recovery observation";
        return observation;
    }

    observation.m_signedCommitment = signedEvidence.journalCommitment();
    observation.m_settlementTermsCommitment =
        signedEvidence.settlementTermsCommitment();
    observation.m_txid = signedEvidence.txid();
    observation.m_chain = signedEvidence.chain();
    observation.m_leg = signedEvidence.leg();
    observation.m_branch = signedEvidence.branch();
    observation.m_confirmed = confirmations
        >= static_cast<quint64>(signedEvidence.requiredConfirmations());
    observation.m_blockHash = blockHash;
    observation.m_blockHeight = QString::number(blockHeight);
    observation.m_confirmations = QString::number(confirmations);
    observation.m_nodeHeight = QString::number(nodeHeight);
    observation.m_requiredConfirmations = signedEvidence.requiredConfirmations();
    observation.m_found = true;
    QByteArray broadcastPreimage("MONZERO-DEX-BTC-RECOVERY-BROADCAST-OBSERVED-V2\n");
    for (const QByteArray &value : {observation.m_settlementTermsCommitment,
             observation.m_signedCommitment,
             signedEvidence.rawTransactionCommitment(), observation.m_txid.toLatin1(),
             signedEvidence.wtxid().toLatin1(), observation.m_chain.toUtf8(),
             observation.m_leg.toUtf8(), observation.m_branch.toUtf8()})
        broadcastPreimage += field(value);
    observation.m_broadcastCommitment =
        QCryptographicHash::hash(broadcastPreimage, QCryptographicHash::Sha256);
    QByteArray preimage("MONZERO-DEX-BTC-RECOVERY-OBSERVED-V2\n");
    for (const QByteArray &value : {observation.m_broadcastCommitment,
             tip.toLatin1(), blockHash.toLatin1(),
             QByteArray::number(blockHeight), QByteArray::number(confirmations),
             QByteArray::number(nodeHeight),
             QByteArray::number(signedEvidence.requiredConfirmations())})
        preimage += field(value);
    observation.m_commitment =
        QCryptographicHash::hash(preimage, QCryptographicHash::Sha256);
    observation.m_details = {{"valid", true}, {"found", true},
        {"blockHash", blockHash}, {"blockHeight", QString::number(blockHeight)},
        {"confirmations", QString::number(confirmations)},
        {"nodeHeight", QString::number(nodeHeight)},
        {"requiredConfirmations", signedEvidence.requiredConfirmations()},
        {"confirmed", observation.m_confirmed}, {"tipPinned", true},
        {"nodeIdentityVerified", true}, {"executable", false}};
    return observation;
}
}
