#include "BitcoinSpendPsbtRpcVerifier.h"
#include "BitcoinFeeEvidence.h"
#include "BitcoinSpendPsbtEvidence.h"

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

namespace BitcoinSpendPsbtRpcVerifier {
QVariantMap validate(const ReadOnlyRpcClient &client, const QByteArray &psbtBase64,
                     const QByteArray &authorization, const QByteArray &expectedCommitment,
                     const QString &expectedParentTxid, const QString &expectedParentVout,
                     const QString &expectedParentAmount, const QString &expectedParentScript,
                     const QString &firstContractKeyHex, const QString &secondContractKeyHex,
                     const QString &expectedRecipient, const QString &expectedRecipientScript,
                     const QString &expectedSequence, const QString &maximumFee,
                     int minimumConfirmations, int feeConfirmationTarget,
                     int maximumFeeHeadroomBasisPoints)
{
    if (!client.configured() || !canonicalPsbt(psbtBase64))
        return rejected("Invalid Bitcoin settlement PSBT or RPC configuration");

    const auto firstTip = client.call("getbestblockhash", QJsonArray{}, authorization);
    if (!firstTip.ok || !firstTip.payload.isString()
        || !hash64.match(firstTip.payload.toString()).hasMatch())
        return rejected("Cannot pin Bitcoin settlement UTXO-set tip");
    const QString bestBlock = firstTip.payload.toString();

    const auto decoded = client.call("decodepsbt",
        QJsonArray{QString::fromLatin1(psbtBase64)}, authorization);
    if (!decoded.ok || !decoded.payload.isObject())
        return rejected("Cannot decode Bitcoin settlement PSBT");
    const QJsonObject psbt = decoded.payload.toObject();
    if (!psbt.value("tx").isObject())
        return rejected("Decoded Bitcoin settlement PSBT has no transaction");
    const QJsonObject tx = psbt.value("tx").toObject();
    const QJsonArray inputs = tx.value("vin").toArray();
    if (inputs.size() != 1 || !inputs[0].isObject())
        return rejected("Bitcoin settlement PSBT must spend exactly one input");
    const QJsonObject input = inputs[0].toObject();
    const QString txid = input.value("txid").toString();
    bool expectedVoutOk = false;
    const quint64 expectedVoutNumber = expectedParentVout.toULongLong(&expectedVoutOk);
    quint64 vout = 0, virtualSize = 0;
    if (!hash64.match(txid).hasMatch() || !uint32Value(input.value("vout"), vout)
        || !uint32Value(tx.value("vsize"), virtualSize) || virtualSize == 0
        || virtualSize > 4000000 || !hash64.match(expectedParentTxid).hasMatch()
        || !expectedVoutOk || QString::number(expectedVoutNumber) != expectedParentVout
        || expectedVoutNumber > std::numeric_limits<quint32>::max()
        || txid != expectedParentTxid || vout != expectedVoutNumber)
        return rejected("Malformed Bitcoin settlement transaction evidence");

    const auto observed = client.call("gettxout",
        QJsonArray{txid, static_cast<double>(vout), true}, authorization);
    if (!observed.ok || !observed.payload.isObject())
        return rejected("Bitcoin settlement parent is missing or spent");
    QJsonObject parent = observed.payload.toObject();
    parent["txid"] = txid;
    parent["vout"] = static_cast<double>(vout);

    const auto finalTip = client.call("getbestblockhash", QJsonArray{}, authorization);
    if (!finalTip.ok || finalTip.payload != QJsonValue(bestBlock))
        return rejected("Bitcoin settlement UTXO-set tip changed during validation");

    const QVariantMap evidence = BitcoinSpendPsbtEvidence::validate(
        psbt, parent, expectedCommitment, expectedParentTxid, expectedParentVout,
        expectedParentAmount, expectedParentScript, firstContractKeyHex,
        secondContractKeyHex, expectedRecipient,
        expectedRecipientScript, expectedSequence, maximumFee, bestBlock,
        minimumConfirmations);
    if (!evidence.value("valid").toBool())
        return rejected(evidence.value("error").toString());

    const auto feeEstimate = client.call("estimatesmartfee",
        QJsonArray{feeConfirmationTarget, "conservative"}, authorization);
    if (!feeEstimate.ok || !feeEstimate.payload.isObject())
        return rejected("Cannot obtain Bitcoin settlement fee estimate");
    const QVariantMap feeEvidence = BitcoinFeeEvidence::validate(
        feeEstimate.payload.toObject(), feeConfirmationTarget, virtualSize,
        evidence.value("feeAtomic").toString(), maximumFee,
        maximumFeeHeadroomBasisPoints);
    if (!feeEvidence.value("valid").toBool())
        return rejected(feeEvidence.value("error").toString());

    QVariantMap result = evidence;
    result["rpcVerified"] = true;
    result["bestBlock"] = bestBlock;
    result["feeEstimateVerified"] = true;
    result["feeRateAtomicPerKvb"] = feeEvidence.value("feeRateAtomicPerKvb");
    result["networkPolicyCeilingAtomic"] = feeEvidence.value("networkPolicyCeilingAtomic");
    result["executable"] = false;
    return result;
}
}
