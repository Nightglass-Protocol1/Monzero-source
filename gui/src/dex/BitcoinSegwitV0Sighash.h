#ifndef MONZERO_BITCOIN_SEGWIT_V0_SIGHASH_H
#define MONZERO_BITCOIN_SEGWIT_V0_SIGHASH_H

#include <QJsonObject>
#include <QString>
#include <QVariantMap>

namespace BitcoinSegwitV0Sighash {
QVariantMap computeAll(const QJsonObject &unsignedTransaction,
                       int inputIndex,
                       const QString &witnessScriptHex,
                       const QString &parentAmountAtomic);
}

#endif
