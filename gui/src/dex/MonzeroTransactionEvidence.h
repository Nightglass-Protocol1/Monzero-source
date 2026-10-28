#ifndef MONZERO_TRANSACTION_EVIDENCE_H
#define MONZERO_TRANSACTION_EVIDENCE_H

#include <QJsonObject>
#include <QVariantMap>

namespace MonzeroTransactionEvidence {
QVariantMap validate(const QJsonObject &walletResult, const QJsonObject &blockResult,
                     const QJsonObject &nodeInfo, const QString &expectedTransactionId,
                     const QString &expectedAddress, const QString &expectedAmountAtomic,
                     int requiredConfirmations);
}

#endif
