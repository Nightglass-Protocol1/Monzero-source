#ifndef MONZERO_BITCOIN_PSBT_RPC_VERIFIER_H
#define MONZERO_BITCOIN_PSBT_RPC_VERIFIER_H

#include "ReadOnlyRpcClient.h"

#include <QByteArray>
#include <QString>
#include <QVariantMap>

namespace BitcoinPsbtRpcVerifier {
QVariantMap validateLock(const ReadOnlyRpcClient &client,
                         const QByteArray &psbtBase64,
                         const QByteArray &authorization,
                         const QByteArray &expectedTemplateCommitment,
                         const QString &expectedAddress,
                         const QString &expectedScriptHex,
                         const QString &firstContractKeyHex,
                         const QString &secondContractKeyHex,
                         const QString &expectedAmountAtomic,
                         const QString &maximumFeeAtomic,
                         int minimumInputConfirmations,
                         int feeConfirmationTarget = 6,
                         int maximumFeeHeadroomBasisPoints = 20000);
}

#endif
