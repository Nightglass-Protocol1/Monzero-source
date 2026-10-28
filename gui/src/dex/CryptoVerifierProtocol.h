#ifndef MONZERO_CRYPTO_VERIFIER_PROTOCOL_H
#define MONZERO_CRYPTO_VERIFIER_PROTOCOL_H

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QVariantMap>

namespace CryptoVerifierProtocol {
struct Request {
    bool valid = false;
    QJsonObject message;
    QByteArray commitment;
    QString operation;
    QString requestId;
    QString error;
};

Request adaptorSignatureRequest(const QString &requestId,
                                const QString &digestHex,
                                const QString &signingPublicKeyHex,
                                const QString &encryptionPublicKeyHex,
                                const QString &encryptedSignatureHex);
Request crossCurveDleqRequest(const QString &requestId,
                             const QString &secp256k1PublicKeyHex,
                             const QString &ed25519PublicKeyHex,
                             const QByteArray &proof,
                             const QByteArray &expectedProofCommitment);
QByteArray dleqProofCommitment(const QByteArray &proof);
// Rebuilds the commitment from the exact supported request fields.
bool canonicalRequest(const Request &request);
QVariantMap validateResponse(const QByteArray &body, const Request &expected,
                             int maximumBytes = 4096);
QString implementationId();
}

#endif
