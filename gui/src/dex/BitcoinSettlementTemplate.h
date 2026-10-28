#ifndef MONZERO_BITCOIN_SETTLEMENT_TEMPLATE_H
#define MONZERO_BITCOIN_SETTLEMENT_TEMPLATE_H

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QVariantMap>

namespace BitcoinSettlementTemplate {

// Commits to the unsigned transaction fields in wire order. Script signatures
// and witnesses must be absent; this is a pre-signing boundary.
QByteArray commitment(const QJsonObject &transaction);
// Reconstructs the same template commitment from a transaction already seen on
// chain. Unlocking script/witness data is ignored, while consensus template
// fields remain fully committed. The caller must separately bind the txid.
QByteArray observedCommitment(const QJsonObject &transaction);

QVariantMap validateLock(const QJsonObject &transaction,
                         const QByteArray &expectedCommitment,
                         const QString &expectedAddress,
                         const QString &expectedScriptHex,
                         const QString &expectedAmountAtomic,
                         const QString &actualFeeAtomic,
                         const QString &maximumFeeAtomic);

QVariantMap validateSpend(const QJsonObject &transaction,
                          const QByteArray &expectedCommitment,
                          const QString &expectedParentTransactionId,
                          const QString &expectedParentVout,
                          const QString &expectedParentAmountAtomic,
                          const QString &expectedRecipient,
                          const QString &expectedRecipientScriptHex,
                          const QString &expectedSequence,
                          const QString &maximumFeeAtomic);
}

#endif
