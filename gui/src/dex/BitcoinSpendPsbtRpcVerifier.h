#ifndef MONZERO_BITCOIN_SPEND_PSBT_RPC_VERIFIER_H
#define MONZERO_BITCOIN_SPEND_PSBT_RPC_VERIFIER_H

#include "ReadOnlyRpcClient.h"

#include <QByteArray>
#include <QString>
#include <QVariantMap>

namespace BitcoinSpendPsbtRpcVerifier {
QVariantMap validate(const ReadOnlyRpcClient &client,
                     const QByteArray &psbtBase64,
                     const QByteArray &authorization,
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
                     int minimumParentConfirmations,
                     int feeConfirmationTarget = 6,
                     int maximumFeeHeadroomBasisPoints = 20000);
}

#endif
