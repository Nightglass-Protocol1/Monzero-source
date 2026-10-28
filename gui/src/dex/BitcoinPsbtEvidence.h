#ifndef MONZERO_BITCOIN_PSBT_EVIDENCE_H
#define MONZERO_BITCOIN_PSBT_EVIDENCE_H

#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QVariantMap>

namespace BitcoinPsbtEvidence {

// prevouts contains normalized gettxout results with the queried txid and vout
// added by the adapter. Every result must come from the same expected best block.
QVariantMap validateLock(const QJsonObject &decodedPsbt,
                         const QJsonArray &prevouts,
                         const QByteArray &expectedTemplateCommitment,
                         const QString &expectedAddress,
                         const QString &expectedScriptHex,
                         const QString &firstContractKeyHex,
                         const QString &secondContractKeyHex,
                         const QString &expectedAmountAtomic,
                         const QString &maximumFeeAtomic,
                         const QString &expectedBestBlock,
                         int minimumInputConfirmations);
}

#endif
