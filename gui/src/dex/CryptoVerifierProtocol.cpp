#include "CryptoVerifierProtocol.h"

#include <QCryptographicHash>
#include <QDataStream>
#include <QJsonDocument>
#include <QJsonValue>
#include <QRegularExpression>

namespace {
const QString implementation =
    "comit-dc6ba84bbb1fe5ecc69581fec7dd8529567c4e32:"
    "ecdsa_fun-0.10.0:sigma_fun-0.7.0:bincode-1.3.3";
const QRegularExpression hex64("^[0-9a-f]{64}$");
const QRegularExpression compressedKey("^0[23][0-9a-f]{64}$");
const QRegularExpression edKey("^[0-9a-f]{64}$");
const QRegularExpression adaptorSignature("^[0-9a-f]{324}$");
const QRegularExpression errorCode("^[a-z][a-z0-9_]{0,63}$");

QByteArray field(const QByteArray &value)
{
    QByteArray encoded;
    QDataStream stream(&encoded, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    stream << quint32(value.size());
    encoded += value;
    return encoded;
}

CryptoVerifierProtocol::Request rejected(const QString &error)
{
    CryptoVerifierProtocol::Request result;
    result.error = error;
    return result;
}

CryptoVerifierProtocol::Request build(const QString &operation, const QString &requestId,
                                      const QList<QByteArray> &commitmentFields,
                                      QJsonObject message)
{
    if (!hex64.match(requestId).hasMatch()) return rejected("Invalid crypto request nonce");
    QByteArray preimage("MONZERO-DEX-CRYPTO-REQUEST-V1\n");
    preimage += field(implementation.toUtf8());
    preimage += field(operation.toUtf8());
    preimage += field(requestId.toLatin1());
    for (const QByteArray &value : commitmentFields) preimage += field(value);
    const QByteArray commitment = QCryptographicHash::hash(preimage, QCryptographicHash::Sha256);
    message.insert("version", 1);
    message.insert("implementation", implementation);
    message.insert("operation", operation);
    message.insert("requestId", requestId);
    message.insert("requestCommitment", QString::fromLatin1(commitment.toHex()));
    CryptoVerifierProtocol::Request result;
    result.valid = true;
    result.message = std::move(message);
    result.commitment = commitment;
    result.operation = operation;
    result.requestId = requestId;
    return result;
}

QVariantMap invalidResponse(const QString &error)
{
    return {{"valid", false}, {"responseAuthenticated", false},
            {"cryptographicallyVerified", false}, {"executable", false}, {"error", error}};
}
}

namespace CryptoVerifierProtocol {
QString implementationId() { return implementation; }

QByteArray dleqProofCommitment(const QByteArray &proof)
{
    if (proof.isEmpty() || proof.size() > 256 * 1024) return {};
    QByteArray preimage("MONZERO-DEX-DLEQ-PROOF-V1\n");
    preimage += field(implementation.toUtf8());
    preimage += field(proof);
    return QCryptographicHash::hash(preimage, QCryptographicHash::Sha256);
}

Request adaptorSignatureRequest(const QString &requestId, const QString &digestHex,
                                const QString &signingKeyHex,
                                const QString &encryptionKeyHex,
                                const QString &encryptedSignatureHex)
{
    if (!hex64.match(digestHex).hasMatch() || !compressedKey.match(signingKeyHex).hasMatch()
        || !compressedKey.match(encryptionKeyHex).hasMatch() || signingKeyHex == encryptionKeyHex
        || !adaptorSignature.match(encryptedSignatureHex).hasMatch())
        return rejected("Invalid adaptor-signature request fields");
    return build("verify_ecdsa_adaptor", requestId,
        {QByteArray::fromHex(digestHex.toLatin1()), QByteArray::fromHex(signingKeyHex.toLatin1()),
         QByteArray::fromHex(encryptionKeyHex.toLatin1()),
         QByteArray::fromHex(encryptedSignatureHex.toLatin1())},
        {{"digestHex", digestHex}, {"signingPublicKeyHex", signingKeyHex},
         {"encryptionPublicKeyHex", encryptionKeyHex},
         {"encryptedSignatureHex", encryptedSignatureHex}});
}

Request crossCurveDleqRequest(const QString &requestId, const QString &secpKeyHex,
                             const QString &edKeyHex, const QByteArray &proof,
                             const QByteArray &expectedProofCommitment)
{
    const QByteArray actualProofCommitment = dleqProofCommitment(proof);
    if (!compressedKey.match(secpKeyHex).hasMatch() || !edKey.match(edKeyHex).hasMatch()
        || expectedProofCommitment.size() != 32
        || actualProofCommitment.isEmpty()
        || actualProofCommitment != expectedProofCommitment)
        return rejected("Invalid cross-curve DLEQ request fields");
    return build("verify_cross_curve_dleq", requestId,
        {QByteArray::fromHex(secpKeyHex.toLatin1()), QByteArray::fromHex(edKeyHex.toLatin1()), proof},
        {{"secp256k1PublicKeyHex", secpKeyHex}, {"ed25519PublicKeyHex", edKeyHex},
         {"proofBase64", QString::fromLatin1(proof.toBase64())}});
}

bool canonicalRequest(const Request &request)
{
    if (!request.valid || !request.error.isEmpty()) return false;
    const auto &message = request.message;
    Request rebuilt;
    if (request.operation == "verify_ecdsa_adaptor") {
        rebuilt = adaptorSignatureRequest(request.requestId,
            message.value("digestHex").toString(),
            message.value("signingPublicKeyHex").toString(),
            message.value("encryptionPublicKeyHex").toString(),
            message.value("encryptedSignatureHex").toString());
    } else if (request.operation == "verify_cross_curve_dleq") {
        const auto encoded = message.value("proofBase64").toString().toLatin1();
        if (encoded.size() > ((256 * 1024 + 2) / 3) * 4) return false;
        const auto proof = QByteArray::fromBase64(encoded);
        if (proof.toBase64() != encoded) return false;
        rebuilt = crossCurveDleqRequest(request.requestId,
            message.value("secp256k1PublicKeyHex").toString(),
            message.value("ed25519PublicKeyHex").toString(), proof,
            dleqProofCommitment(proof));
    } else return false;
    return rebuilt.valid && rebuilt.commitment == request.commitment
        && rebuilt.message == message;
}

QVariantMap validateResponse(const QByteArray &body, const Request &expected, int maximumBytes)
{
    if (!canonicalRequest(expected) || maximumBytes < 1 || maximumBytes > 64 * 1024
        || (expected.operation != "verify_ecdsa_adaptor"
            && expected.operation != "verify_cross_curve_dleq")
        || body.isEmpty() || body.size() > maximumBytes)
        return invalidResponse("Invalid or oversized crypto-verifier response");
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        return invalidResponse("Malformed crypto-verifier response");
    const QJsonObject response = document.object();
    if (response.size() != 8 || response.value("version") != QJsonValue(1)
        || response.value("implementation") != QJsonValue(implementation)
        || response.value("operation") != QJsonValue(expected.operation)
        || response.value("requestId") != QJsonValue(expected.requestId)
        || response.value("requestCommitment")
            != QJsonValue(QString::fromLatin1(expected.commitment.toHex()))
        || !response.value("verified").isBool()
        || !response.value("error").isString()
        || response.value("executable") != QJsonValue(false))
        return invalidResponse("Crypto-verifier response binding mismatch");
    const bool verified = response.value("verified").toBool();
    const QString error = response.value("error").toString();
    if ((verified && !error.isEmpty()) || (!verified && !errorCode.match(error).hasMatch()))
        return invalidResponse("Invalid crypto-verifier result semantics");
    return {{"valid", true}, {"responseAuthenticated", false},
            {"cryptographicallyVerified", verified}, {"error", error},
            {"requestCommitment", QString::fromLatin1(expected.commitment.toHex())},
            {"implementation", implementation}, {"executable", false},
            {"executionBlocker", "Verifier binary identity and independent vectors are not established"}};
}
}
