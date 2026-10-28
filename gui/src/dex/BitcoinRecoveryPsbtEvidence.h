#ifndef MONZERO_BITCOIN_RECOVERY_PSBT_EVIDENCE_H
#define MONZERO_BITCOIN_RECOVERY_PSBT_EVIDENCE_H

#include "SettlementTerms.h"

#include <QJsonObject>
#include <QVariantMap>

namespace BitcoinRecoveryPsbtEvidence {
enum class Leg { Pay, Receive };
enum class Branch { Redeem, Cancel, Refund, Punish };

QVariantMap validate(const SettlementTerms::Terms &terms, Leg leg, Branch branch,
                     qint64 now, const QJsonObject &decodedPsbt,
                     const QJsonObject &decodedParentTransaction,
                     const QJsonObject &parentPrevout,
                     const QString &expectedParentTxid,
                     const QString &expectedParentVoutAtomic,
                     const QString &expectedParentAmountAtomic,
                     const QString &expectedParentScriptHex,
                     const QString &firstContractKeyHex,
                     const QString &secondContractKeyHex,
                     const QString &expectedRecipient,
                     const QString &expectedRecipientScriptHex,
                     const QString &expectedBestBlock);
}

#endif
