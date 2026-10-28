#include "BitcoinPsbtRpcVerifier.h"
#include "BitcoinFeeEvidence.h"
#include "BitcoinPsbtEvidence.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <cmath>
#include <limits>

namespace {
const QRegularExpression base64("^[A-Za-z0-9+/]*={0,2}$");
const QRegularExpression hash64("^[0-9a-f]{64}$");

QVariantMap rejected(const QString &error)
{
    return {{"valid", false}, {"rpcVerified", false},
            {"executable", false}, {"error", error}};
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
}

namespace BitcoinPsbtRpcVerifier {
QVariantMap validateLock(const ReadOnlyRpcClient &client, const QByteArray &psbtBase64,
                         const QByteArray &authorization,
                         const QByteArray &expectedCommitment,
                         const QString &expectedAddress, const QString &expectedScriptHex,
                         const QString &firstContractKeyHex,
                         const QString &secondContractKeyHex,
                         const QString &expectedAmountAtomic, const QString &maximumFeeAtomic,
                         int minimumInputConfirmations, int feeConfirmationTarget,
                         int maximumFeeHeadroomBasisPoints)
{
    if (!client.configured() || !canonicalPsbt(psbtBase64))
        return rejected("Invalid Bitcoin PSBT or RPC configuration");

    const auto firstTip = client.call("getbestblockhash", QJsonArray{}, authorization);
    if (!firstTip.ok || !firstTip.payload.isString()
        || !hash64.match(firstTip.payload.toString()).hasMatch())
        return rejected("Cannot pin Bitcoin UTXO-set tip");
    const QString bestBlock = firstTip.payload.toString();

    const auto decoded = client.call("decodepsbt",
        QJsonArray{QString::fromLatin1(psbtBase64)}, authorization);
    if (!decoded.ok || !decoded.payload.isObject())
        return rejected("Cannot decode Bitcoin PSBT");
    const QJsonObject psbt = decoded.payload.toObject();
    if (!psbt.value("tx").isObject() || !psbt.value("tx").toObject().value("vin").isArray())
        return rejected("Decoded Bitcoin PSBT has no transaction inputs");

    quint64 estimatedVirtualSize = 0;
    if (!uint32Value(psbt.value("tx").toObject().value("vsize"), estimatedVirtualSize)
        || estimatedVirtualSize == 0 || estimatedVirtualSize > 4000000)
        return rejected("Decoded Bitcoin PSBT has no safe virtual-size estimate");

    QJsonArray prevouts;
    const QJsonArray inputs = psbt.value("tx").toObject().value("vin").toArray();
    if (inputs.isEmpty() || inputs.size() > 1000)
        return rejected("Invalid Bitcoin PSBT input count");
    for (const QJsonValue &value : inputs) {
        if (!value.isObject()) return rejected("Malformed Bitcoin PSBT input");
        const QJsonObject input = value.toObject();
        const QString txid = input.value("txid").toString();
        quint64 vout = 0;
        if (!hash64.match(txid).hasMatch() || !uint32Value(input.value("vout"), vout))
            return rejected("Malformed Bitcoin PSBT outpoint");
        const auto observed = client.call("gettxout",
            QJsonArray{txid, static_cast<double>(vout), true}, authorization);
        if (!observed.ok || !observed.payload.isObject())
            return rejected("Bitcoin PSBT input is missing or spent");
        QJsonObject normalized = observed.payload.toObject();
        normalized["txid"] = txid;
        normalized["vout"] = static_cast<double>(vout);
        prevouts.append(normalized);
    }

    const auto finalTip = client.call("getbestblockhash", QJsonArray{}, authorization);
    if (!finalTip.ok || finalTip.payload != QJsonValue(bestBlock))
        return rejected("Bitcoin UTXO-set tip changed during validation");

    const QVariantMap evidence = BitcoinPsbtEvidence::validateLock(
        psbt, prevouts, expectedCommitment, expectedAddress, expectedScriptHex,
        firstContractKeyHex, secondContractKeyHex,
        expectedAmountAtomic, maximumFeeAtomic, bestBlock, minimumInputConfirmations);
    if (!evidence.value("valid").toBool()) return rejected(evidence.value("error").toString());

    const auto feeEstimate = client.call("estimatesmartfee",
        QJsonArray{feeConfirmationTarget, "conservative"}, authorization);
    if (!feeEstimate.ok || !feeEstimate.payload.isObject())
        return rejected("Cannot obtain Bitcoin fee estimate");
    const QVariantMap feeEvidence = BitcoinFeeEvidence::validate(
        feeEstimate.payload.toObject(), feeConfirmationTarget, estimatedVirtualSize,
        evidence.value("feeAtomic").toString(), maximumFeeAtomic,
        maximumFeeHeadroomBasisPoints);
    if (!feeEvidence.value("valid").toBool())
        return rejected(feeEvidence.value("error").toString());

    QVariantMap result = evidence;
    result["rpcVerified"] = true;
    result["bestBlock"] = bestBlock;
    result["feeEstimateVerified"] = true;
    result["feeRateAtomicPerKvb"] = feeEvidence.value("feeRateAtomicPerKvb");
    result["networkPolicyCeilingAtomic"] =
        feeEvidence.value("networkPolicyCeilingAtomic");
    result["executable"] = false;
    return result;
}
}
