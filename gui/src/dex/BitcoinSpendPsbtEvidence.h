#ifndef MONZERO_BITCOIN_SPEND_PSBT_EVIDENCE_H
#define MONZERO_BITCOIN_SPEND_PSBT_EVIDENCE_H

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QVariantMap>

namespace BitcoinSpendPsbtEvidence {
QVariantMap validate(const QJsonObject &decodedPsbt,
                     const QJsonObject &parentPrevout,
                     const QByteArray &expectedTemplateCommitment,
                     const QString &expectedParentTxid,
                     const QString &expectedParentVoutAtomic,
                     const QString &expectedParentAmountAtomic,
                     const QString &expectedParentScriptHex,
                     const QString &firstContractKeyHex,
                     const QString &secondContractKeyHex,
                     const QString &expectedRecipient,
                     const QString &expectedRecipientScriptHex,
                     const QString &expectedSequenceAtomic,
                     const QString &maximumFeeAtomic,
                     const QString &expectedBestBlock,
                     int minimumParentConfirmations);
}

#endif
