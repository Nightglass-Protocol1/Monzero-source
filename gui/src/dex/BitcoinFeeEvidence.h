#ifndef MONZERO_BITCOIN_FEE_EVIDENCE_H
#define MONZERO_BITCOIN_FEE_EVIDENCE_H

#include <QJsonObject>
#include <QString>
#include <QVariantMap>

namespace BitcoinFeeEvidence {
QVariantMap validate(const QJsonObject &estimate,
                     int requestedConfirmationTarget,
                     quint64 estimatedVirtualSize,
                     const QString &actualFeeAtomic,
                     const QString &signedMaximumFeeAtomic,
                     int maximumHeadroomBasisPoints = 20000);
}

#endif
