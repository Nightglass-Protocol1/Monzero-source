#include "BitcoinRecoveryPsbtRpcVerifier.h"

#include "BitcoinFeeEvidence.h"
#include "BitcoinSharedOutputScript.h"
#include "NodeRpcValidator.h"

#include <QCryptographicHash>
#include <QDataStream>
#include <QJsonArray>
#include <QRegularExpression>
#include <cmath>
#include <limits>

namespace {
const QRegularExpression base64("^[A-Za-z0-9+/]*={0,2}$");
const QRegularExpression hash64("^[0-9a-f]{64}$");

QVariantMap rejected(const QString &error)
{
    return {{"valid", false}, {"rpcVerified", false},
            {"branchPolicyVerified", false}, {"executable", false}, {"error", error}};
}

bool canonicalPsbt(const QByteArray &encoded)
{
    if (encoded.isEmpty() || encoded.size() > 3 * 1024 * 1024
        || encoded.size() % 4 != 0
        || !base64.match(QString::fromLatin1(encoded)).hasMatch()) return false;
    const QByteArray decoded = QByteArray::fromBase64(encoded);
    return decoded.size() <= 2 * 1024 * 1024 && decoded.startsWith("psbt\xff")
        && decoded.toBase64() == encoded;
}

bool uint32Value(const QJsonValue &value, quint64 &result)
{
    if (!value.isDouble()) return false;
    const double number = value.toDouble();
    if (!std::isfinite(number) || number < 0 || number != std::floor(number)
        || number > std::numeric_limits<quint32>::max()) return false;
    result = static_cast<quint64>(number);
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

namespace BitcoinRecoveryPsbtRpcVerifier {
QVariantMap validate(const ReadOnlyRpcClient &client, const SettlementTerms::Terms &terms,
                     BitcoinRecoveryPsbtEvidence::Leg leg,
                     BitcoinRecoveryPsbtEvidence::Branch branch, qint64 now,
                     const QByteArray &psbtBase64, const QByteArray &authorization,
                     const QString &expectedParentTxid, const QString &expectedParentVout,
                     const QString &expectedParentAmount, const QString &expectedParentScript,
                     const QString &firstContractKeyHex, const QString &secondContractKeyHex,
                     const QString &expectedRecipient, const QString &expectedRecipientScript,
                     int feeConfirmationTarget, int maximumFeeHeadroomBasisPoints)
{
    if (!client.configured() || !canonicalPsbt(psbtBase64)
        || !hash64.match(expectedParentTxid).hasMatch())
        return rejected("Invalid Bitcoin recovery PSBT or RPC configuration");

    const bool pay = leg == BitcoinRecoveryPsbtEvidence::Leg::Pay;
    const QString expectedGenesis = pay ? terms.payGenesis : terms.receiveGenesis;
    const auto chainInfo = client.call("getblockchaininfo", QJsonArray{}, authorization);
    const auto genesis = client.call("getblockhash", QJsonArray{0}, authorization);
    if (!chainInfo.ok || !chainInfo.payload.isObject() || !genesis.ok
        || !genesis.payload.isString()
        || !NodeRpcValidator::bitcoin(chainInfo.payload.toObject(),
                                      genesis.payload.toString(), expectedGenesis)
                .value("ready").toBool())
        return rejected("Bitcoin recovery node identity or synchronization mismatch");

    const auto firstTip = client.call("getbestblockhash", QJsonArray{}, authorization);
    if (!firstTip.ok || !firstTip.payload.isString()
        || !hash64.match(firstTip.payload.toString()).hasMatch())
        return rejected("Cannot pin Bitcoin recovery UTXO-set tip");
    const QString bestBlock = firstTip.payload.toString();
    quint64 tipHeight = 0;
    if (chainInfo.payload.toObject().value("bestblockhash") != QJsonValue(bestBlock)
        || !uint32Value(chainInfo.payload.toObject().value("blocks"), tipHeight))
        return rejected("Bitcoin recovery chain information and pinned tip disagree");

    const auto decoded = client.call("decodepsbt",
        QJsonArray{QString::fromLatin1(psbtBase64)}, authorization);
    if (!decoded.ok || !decoded.payload.isObject()
        || !decoded.payload.toObject().value("tx").isObject())
        return rejected("Cannot decode Bitcoin recovery PSBT");
    const QJsonObject psbt = decoded.payload.toObject();
    const QJsonObject child = psbt.value("tx").toObject();
    const QJsonArray inputs = child.value("vin").toArray();
    quint64 vout = 0, virtualSize = 0;
    bool expectedVoutOk = false;
    const quint64 expectedVoutNumber = expectedParentVout.toULongLong(&expectedVoutOk);
    if (inputs.size() != 1 || !inputs[0].isObject()
        || inputs[0].toObject().value("txid") != QJsonValue(expectedParentTxid)
        || !uint32Value(inputs[0].toObject().value("vout"), vout)
        || !uint32Value(child.value("vsize"), virtualSize) || virtualSize == 0
        || virtualSize > 4000000 || !expectedVoutOk
        || QString::number(expectedVoutNumber) != expectedParentVout
        || vout != expectedVoutNumber)
        return rejected("Malformed Bitcoin recovery transaction evidence");

    const auto rawParent = client.call("getrawtransaction",
        QJsonArray{expectedParentTxid, true}, authorization);
    if (!rawParent.ok || !rawParent.payload.isObject()
        || rawParent.payload.toObject().value("txid") != QJsonValue(expectedParentTxid))
        return rejected("Cannot decode signed Bitcoin recovery parent");

    const auto observed = client.call("gettxout",
        QJsonArray{expectedParentTxid, static_cast<double>(vout), true}, authorization);
    if (!observed.ok || !observed.payload.isObject())
        return rejected("Bitcoin recovery parent is missing or spent");
    QJsonObject parentPrevout = observed.payload.toObject();
    parentPrevout["txid"] = expectedParentTxid;
    parentPrevout["vout"] = static_cast<double>(vout);

    const auto finalTip = client.call("getbestblockhash", QJsonArray{}, authorization);
    if (!finalTip.ok || finalTip.payload != QJsonValue(bestBlock))
        return rejected("Bitcoin recovery UTXO-set tip changed during validation");

    QVariantMap result = BitcoinRecoveryPsbtEvidence::validate(terms, leg, branch, now,
        psbt, rawParent.payload.toObject(), parentPrevout, expectedParentTxid,
        expectedParentVout, expectedParentAmount, expectedParentScript,
        firstContractKeyHex, secondContractKeyHex, expectedRecipient,
        expectedRecipientScript, bestBlock);
    if (!result.value("valid").toBool())
        return rejected(result.value("error").toString());
    bool confirmationsOk = false;
    const quint64 parentConfirmations =
        result.value("parentConfirmations").toString().toULongLong(&confirmationsOk);
    if (!confirmationsOk || parentConfirmations == 0
        || parentConfirmations > tipHeight + 1)
        return rejected("Bitcoin recovery parent height is inconsistent");
    result["parentBlockHeight"] =
        QString::number(tipHeight - parentConfirmations + 1);
    result["nodeHeight"] = QString::number(tipHeight);

    const auto feeEstimate = client.call("estimatesmartfee",
        QJsonArray{feeConfirmationTarget, "conservative"}, authorization);
    if (!feeEstimate.ok || !feeEstimate.payload.isObject())
        return rejected("Cannot obtain Bitcoin recovery fee estimate");
    const QString maximumFee = leg == BitcoinRecoveryPsbtEvidence::Leg::Pay
        ? terms.payNativeFeeMaxAtomic : terms.receiveNativeFeeMaxAtomic;
    const QVariantMap fee = BitcoinFeeEvidence::validate(feeEstimate.payload.toObject(),
        feeConfirmationTarget, virtualSize, result.value("feeAtomic").toString(),
        maximumFee, maximumFeeHeadroomBasisPoints);
    if (!fee.value("valid").toBool()) return rejected(fee.value("error").toString());

    result["rpcVerified"] = true;
    result["nodeIdentityVerified"] = true;
    result["bestBlock"] = bestBlock;
    result["feeEstimateVerified"] = true;
    result["feeRateAtomicPerKvb"] = fee.value("feeRateAtomicPerKvb");
    result["networkPolicyCeilingAtomic"] = fee.value("networkPolicyCeilingAtomic");
    result["executable"] = false;
    return result;
}

Evidence validateEvidence(const ReadOnlyRpcClient &client,
                     const SettlementTerms::Terms &terms,
                     BitcoinRecoveryPsbtEvidence::Leg leg,
                     BitcoinRecoveryPsbtEvidence::Branch branch, qint64 now,
                     const QByteArray &psbtBase64, const QByteArray &authorization,
                     const QString &expectedParentTxid, const QString &expectedParentVout,
                     const QString &expectedParentAmount, const QString &expectedParentScript,
                     const QString &firstContractKeyHex, const QString &secondContractKeyHex,
                     const QString &expectedRecipient, const QString &expectedRecipientScript,
                     int feeConfirmationTarget, int maximumFeeHeadroomBasisPoints)
{
    Evidence evidence;
    evidence.m_details = validate(client, terms, leg, branch, now, psbtBase64,
        authorization, expectedParentTxid, expectedParentVout, expectedParentAmount,
        expectedParentScript, firstContractKeyHex, secondContractKeyHex,
        expectedRecipient, expectedRecipientScript, feeConfirmationTarget,
        maximumFeeHeadroomBasisPoints);
    const QVariantMap &result = evidence.m_details;
    if (!result.value("valid").toBool() || !result.value("rpcVerified").toBool()
        || !result.value("branchPolicyVerified").toBool()
        || !result.value("nodeIdentityVerified").toBool()
        || !result.value("feeEstimateVerified").toBool()
        || !result.value("provenanceVerified").toBool()
        || !result.value("scriptVerified").toBool()
        || !result.value("sighashVerified").toBool()
        || result.value("executable").toBool()) return evidence;

    evidence.m_settlementTermsCommitment = QByteArray::fromHex(
        result.value("settlementTermsCommitment").toString().toLatin1());
    const QByteArray childCommitment = QByteArray::fromHex(
        result.value("templateCommitment").toString().toLatin1());
    const QByteArray parentCommitment = QByteArray::fromHex(
        result.value("parentTemplateCommitment").toString().toLatin1());
    const QByteArray sighash = QByteArray::fromHex(
        result.value("sighashHex").toString().toLatin1());
    const QByteArray bestBlock = QByteArray::fromHex(
        result.value("bestBlock").toString().toLatin1());
    if (evidence.m_settlementTermsCommitment.size() != 32
        || childCommitment.size() != 32 || parentCommitment.size() != 32
        || sighash.size() != 32 || bestBlock.size() != 32) return Evidence{};

    evidence.m_leg = result.value("leg").toString();
    evidence.m_branch = result.value("branch").toString();
    evidence.m_chain = leg == BitcoinRecoveryPsbtEvidence::Leg::Pay
        ? terms.payNetwork : terms.receiveNetwork;
    evidence.m_requiredConfirmations = leg == BitcoinRecoveryPsbtEvidence::Leg::Pay
        ? terms.payConfirmations : terms.receiveConfirmations;
    evidence.m_expectedGenesis = leg == BitcoinRecoveryPsbtEvidence::Leg::Pay
        ? terms.payGenesis : terms.receiveGenesis;
    evidence.m_parentTxid = expectedParentTxid;
    evidence.m_parentVoutAtomic = expectedParentVout;
    evidence.m_parentBlockHeight = result.value("parentBlockHeight").toString();
    if (branch == BitcoinRecoveryPsbtEvidence::Branch::Cancel)
        evidence.m_relativeBlocks = leg == BitcoinRecoveryPsbtEvidence::Leg::Pay
            ? terms.payCancelBlocks : terms.receiveCancelBlocks;
    else if (branch == BitcoinRecoveryPsbtEvidence::Branch::Punish)
        evidence.m_relativeBlocks = leg == BitcoinRecoveryPsbtEvidence::Leg::Pay
            ? terms.payPunishBlocks : terms.receivePunishBlocks;
    evidence.m_firstContractKeyHex = firstContractKeyHex;
    evidence.m_secondContractKeyHex = secondContractKeyHex;
    const QVariantMap script = BitcoinSharedOutputScript::validateProgram(
        expectedParentScript, firstContractKeyHex, secondContractKeyHex);
    if (!script.value("valid").toBool()) return Evidence{};
    evidence.m_witnessScriptHex = script.value("witnessScriptHex").toString();
    QByteArray preimage("MONZERO-DEX-BTC-RECOVERY-PREPARED-V1\n");
    for (const QByteArray &value : {evidence.m_settlementTermsCommitment,
             evidence.m_leg.toUtf8(), evidence.m_branch.toUtf8(),
             evidence.m_chain.toUtf8(), childCommitment, parentCommitment,
             sighash, bestBlock, evidence.m_firstContractKeyHex.toLatin1(),
             evidence.m_secondContractKeyHex.toLatin1(),
             evidence.m_witnessScriptHex.toLatin1(),
             QByteArray::number(evidence.m_requiredConfirmations),
             evidence.m_expectedGenesis.toLatin1(),
             evidence.m_parentTxid.toLatin1(),
             evidence.m_parentVoutAtomic.toLatin1(),
             evidence.m_parentBlockHeight.toLatin1(),
             QByteArray::number(evidence.m_relativeBlocks),
             result.value("feeAtomic").toString().toLatin1(),
             result.value("networkPolicyCeilingAtomic").toString().toLatin1()})
        preimage += field(value);
    evidence.m_journalCommitment =
        QCryptographicHash::hash(preimage, QCryptographicHash::Sha256);
    return evidence;
}
}
