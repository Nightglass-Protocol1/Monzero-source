#ifndef MONZERO_BITCOIN_SHARED_OUTPUT_SCRIPT_H
#define MONZERO_BITCOIN_SHARED_OUTPUT_SCRIPT_H

#include <QString>
#include <QVariantMap>

namespace BitcoinSharedOutputScript {
QVariantMap validateProgram(const QString &scriptPubKeyHex,
                            const QString &firstPublicKeyHex,
                            const QString &secondPublicKeyHex);
QVariantMap validate(const QString &witnessScriptHex,
                     const QString &scriptPubKeyHex,
                     const QString &firstPublicKeyHex,
                     const QString &secondPublicKeyHex);
}

#endif
