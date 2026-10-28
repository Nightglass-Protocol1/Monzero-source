#include "BitcoinFeeEvidence.h"

#include <QJsonArray>
#include <QRegularExpression>
#include <cmath>
#include <limits>

namespace {
constexpr quint64 satoshisPerBitcoin = 100000000ULL;
constexpr quint64 maximumBitcoinSatoshis = 21000000ULL * satoshisPerBitcoin;

QVariantMap rejected(const QString &error)
{
    return {{"valid", false}, {"feeEstimateVerified", false},
            {"executable", false}, {"error", error}};
}

bool atomic(const QString &text, quint64 &value)
{
    static const QRegularExpression integer("^(0|[1-9][0-9]{0,15})$");
    bool ok = false;
    value = text.toULongLong(&ok);
    return ok && integer.match(text).hasMatch() && value <= maximumBitcoinSatoshis;
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

bool feeRate(const QJsonValue &input, quint64 &satoshisPerKvb)
{
    if (!input.isDouble()) return false;
    const double bitcoinsPerKvb = input.toDouble();
    if (!std::isfinite(bitcoinsPerKvb) || bitcoinsPerKvb <= 0
        || bitcoinsPerKvb > 21.0) return false;
    const double scaled = bitcoinsPerKvb * static_cast<double>(satoshisPerBitcoin);
    const double rounded = std::round(scaled);
    if (std::fabs(scaled - rounded) > 0.000001
        || rounded < 1 || rounded > static_cast<double>(maximumBitcoinSatoshis)) return false;
    satoshisPerKvb = static_cast<quint64>(rounded);
    return true;
}
}

namespace BitcoinFeeEvidence {
QVariantMap validate(const QJsonObject &estimate, int requestedConfirmationTarget,
                     quint64 estimatedVirtualSize, const QString &actualFeeAtomic,
                     const QString &signedMaximumFeeAtomic, int maximumHeadroomBasisPoints)
{
    quint64 actualFee = 0, signedMaximum = 0, returnedBlocks = 0, rate = 0;
    const QJsonValue errors = estimate.value("errors");
    if (requestedConfirmationTarget < 1 || requestedConfirmationTarget > 1008
        || estimatedVirtualSize < 1 || estimatedVirtualSize > 4000000
        || maximumHeadroomBasisPoints < 10000 || maximumHeadroomBasisPoints > 100000
        || !atomic(actualFeeAtomic, actualFee) || actualFee == 0
        || !atomic(signedMaximumFeeAtomic, signedMaximum) || signedMaximum == 0
        || actualFee > signedMaximum
        || !exactPositiveInteger(estimate.value("blocks"), 1008, returnedBlocks)
        || returnedBlocks > static_cast<quint64>(requestedConfirmationTarget)
        || !feeRate(estimate.value("feerate"), rate)
        || (errors.isArray() && !errors.toArray().isEmpty())
        || (!errors.isUndefined() && !errors.isArray()))
        return rejected("Invalid or unsafe Bitcoin fee estimate");

    // ceil(rate * vsize / 1000), followed by bounded integer headroom.
    if (rate > (std::numeric_limits<quint64>::max() - 999) / estimatedVirtualSize)
        return rejected("Bitcoin fee estimate overflow");
    const quint64 estimatedFee = (rate * estimatedVirtualSize + 999) / 1000;
    if (estimatedFee > (std::numeric_limits<quint64>::max() - 9999)
                           / static_cast<quint64>(maximumHeadroomBasisPoints))
        return rejected("Bitcoin fee estimate overflow");
    const quint64 policyCeiling =
        (estimatedFee * static_cast<quint64>(maximumHeadroomBasisPoints) + 9999) / 10000;
    if (actualFee > policyCeiling)
        return rejected("Bitcoin PSBT fee exceeds the verified network estimate policy");

    return {{"valid", true}, {"feeEstimateVerified", true},
            {"feeRateAtomicPerKvb", QString::number(rate)},
            {"estimatedFeeAtomic", QString::number(estimatedFee)},
            {"networkPolicyCeilingAtomic", QString::number(policyCeiling)},
            {"signedMaximumFeeAtomic", QString::number(signedMaximum)},
            {"executable", false}};
}
}
