#ifndef MONZERO_BITCOIN_ECDSA_SIGNATURE_VERIFIER_H
#define MONZERO_BITCOIN_ECDSA_SIGNATURE_VERIFIER_H

#include <QString>
#include <QVariantMap>

namespace BitcoinEcdsaSignatureVerifier {
// Verifies a strict-DER, low-S secp256k1 signature followed by SIGHASH_ALL (01).
// This is an observational boundary and never signs or authorizes execution.
QVariantMap verifyAll(const QString &digestHex,
                      const QString &compressedPublicKeyHex,
                      const QString &signatureWithHashTypeHex);
}

#endif
