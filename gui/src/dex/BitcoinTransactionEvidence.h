#ifndef MONZERO_BITCOIN_TRANSACTION_EVIDENCE_H
#define MONZERO_BITCOIN_TRANSACTION_EVIDENCE_H

#include <QJsonObject>
#include <QVariantMap>

namespace BitcoinTransactionEvidence {
QVariantMap validate(const QJsonObject &transaction, const QJsonObject &blockHeader,
                     const QString &expectedTransactionId, const QString &expectedAddress,
                     const QString &expectedAmountAtomic, const QString &nodeHeight,
                     int requiredConfirmations);
}

#endif
