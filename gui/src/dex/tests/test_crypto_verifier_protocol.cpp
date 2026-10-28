#include "../CryptoVerifierProtocol.h"

#include <QJsonDocument>
#include <QProcess>
#include <QProcessEnvironment>
#include <QtTest>

namespace {
const QString nonce(64, '1');
const QString digest(64, '2');
const QString keyA = "0279be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798";
const QString keyB = "02c6047f9441ed7d6d3045406e95c07cd85c778e4b8cef3ca7abac09b95c709ee5";
QByteArray response(const CryptoVerifierProtocol::Request &request, bool verified,
                    const QString &error = {})
{
    return QJsonDocument(QJsonObject{{"version", 1},
        {"implementation", CryptoVerifierProtocol::implementationId()},
        {"operation", request.operation}, {"requestId", request.requestId},
        {"requestCommitment", QString::fromLatin1(request.commitment.toHex())},
        {"verified", verified}, {"error", error}, {"executable", false}})
        .toJson(QJsonDocument::Compact);
}
}

class CryptoVerifierProtocolTest : public QObject
{
    Q_OBJECT
private slots:
    void rejectsMutatedRequestDespiteMatchingResponseEnvelope()
    {
        const auto adaptor = CryptoVerifierProtocol::adaptorSignatureRequest(
            nonce, digest, keyA, keyB, QString(324, 'a'));
        const QByteArray proof("proof-one");
        const auto dleq = CryptoVerifierProtocol::crossCurveDleqRequest(
            nonce, keyA, QString(64, '3'), proof,
            CryptoVerifierProtocol::dleqProofCommitment(proof));
        for (const auto &original : {adaptor, dleq}) {
            QVERIFY(CryptoVerifierProtocol::canonicalRequest(original));
            for (const auto &key : original.message.keys()) {
                auto changed = original;
                changed.message[key] = "substituted";
                QVERIFY(!CryptoVerifierProtocol::canonicalRequest(changed));
                QVERIFY(!CryptoVerifierProtocol::validateResponse(response(changed, true), changed)
                             .value("valid").toBool());
            }
            auto changed = original;
            changed.commitment = QByteArray(32, 'x');
            QVERIFY(!CryptoVerifierProtocol::validateResponse(response(changed, true), changed)
                         .value("valid").toBool());
            changed = original;
            changed.message["extra"] = false;
            QVERIFY(!CryptoVerifierProtocol::canonicalRequest(changed));
        }
        auto changed = dleq;
        changed.message["proofBase64"] = QString::fromLatin1(proof.toBase64()) + "\n";
        QVERIFY(!CryptoVerifierProtocol::canonicalRequest(changed));
    }

    void rejectsUnimplementedSigningSchemes()
    {
        for (const auto &operation : {QString("verify_clsag_adaptor"),
                QString("sign_multisig"), QString("verify_ed25519_adaptor"), QString()}) {
            auto request = CryptoVerifierProtocol::adaptorSignatureRequest(
                nonce, digest, keyA, keyB, QString(324, 'a'));
            QVERIFY(request.valid);
            request.operation = operation;
            request.message["operation"] = operation;
            const auto result = CryptoVerifierProtocol::validateResponse(response(request, true), request);
            QVERIFY(!result.value("valid").toBool());
            QVERIFY(!result.value("cryptographicallyVerified").toBool());
            QVERIFY(!result.value("responseAuthenticated").toBool());
            QVERIFY(!result.value("executable").toBool());
        }
    }

    void bindsAdaptorRequestAndResponse()
    {
        const auto request = CryptoVerifierProtocol::adaptorSignatureRequest(
            nonce, digest, keyA, keyB, QString(324, 'a'));
        QVERIFY2(request.valid, qPrintable(request.error));
        QCOMPARE(request.commitment.size(), 32);
        const QVariantMap result = CryptoVerifierProtocol::validateResponse(
            response(request, true), request);
        QVERIFY(result.value("valid").toBool());
        QVERIFY(result.value("cryptographicallyVerified").toBool());
        QVERIFY(!result.value("responseAuthenticated").toBool());
        QVERIFY(!result.value("executable").toBool());
    }
    void pinnedVerifierRejectsUnsupportedOperation()
    {
        const auto verifier = qEnvironmentVariable("MONZERO_DEX_CRYPTO_VERIFIER");
        if (verifier.isEmpty()) QSKIP("Pinned Rust verifier path was not supplied");
        auto request = CryptoVerifierProtocol::adaptorSignatureRequest(
            nonce, digest, keyA, keyB, QString(324, 'a'));
        request.message["operation"] = "verify_clsag_adaptor";
        QProcess process;
        process.setProcessEnvironment(QProcessEnvironment{});
        process.start(verifier, QStringList{});
        QVERIFY(process.waitForStarted(5000));
        const auto bytes = QJsonDocument(request.message).toJson(QJsonDocument::Compact);
        QCOMPARE(process.write(bytes), qint64(bytes.size()));
        process.closeWriteChannel();
        QVERIFY(process.waitForFinished(5000));
        QCOMPARE(process.exitStatus(), QProcess::NormalExit);
        QCOMPARE(process.exitCode(), 2);
        QVERIFY(process.readAllStandardOutput().isEmpty());
        QVERIFY(process.readAllStandardError().isEmpty());
    }
    void bindsCrossCurveProofBytes()
    {
        const QByteArray firstProof("proof-one");
        const QByteArray secondProof("proof-two");
        const auto first = CryptoVerifierProtocol::crossCurveDleqRequest(
            nonce, keyA, QString(64, '3'), firstProof,
            CryptoVerifierProtocol::dleqProofCommitment(firstProof));
        const auto second = CryptoVerifierProtocol::crossCurveDleqRequest(
            nonce, keyA, QString(64, '3'), secondProof,
            CryptoVerifierProtocol::dleqProofCommitment(secondProof));
        QVERIFY(first.valid); QVERIFY(second.valid);
        QVERIFY(first.commitment != second.commitment);
        QVERIFY(!CryptoVerifierProtocol::crossCurveDleqRequest(
            nonce, keyA, QString(64, '3'), firstProof,
            CryptoVerifierProtocol::dleqProofCommitment(secondProof)).valid);
    }
    void rejectsReplaySubstitutionAndBadSemantics()
    {
        const auto request = CryptoVerifierProtocol::adaptorSignatureRequest(
            nonce, digest, keyA, keyB, QString(324, 'a'));
        QJsonObject changed = QJsonDocument::fromJson(response(request, true)).object();
        changed["requestId"] = QString(64, '9');
        QVERIFY(!CryptoVerifierProtocol::validateResponse(
                     QJsonDocument(changed).toJson(QJsonDocument::Compact), request)
                     .value("valid").toBool());
        QVERIFY(!CryptoVerifierProtocol::validateResponse(
                     response(request, true, "unexpected"), request).value("valid").toBool());
        QVERIFY(!CryptoVerifierProtocol::validateResponse(
                     response(request, false, "Bad Error"), request).value("valid").toBool());
    }
    void rejectsMalformedRequests()
    {
        QVERIFY(!CryptoVerifierProtocol::adaptorSignatureRequest(
            "1", digest, keyA, keyB, QString(324, 'a')).valid);
        QVERIFY(!CryptoVerifierProtocol::adaptorSignatureRequest(
            nonce, digest, keyA, keyA, QString(324, 'a')).valid);
        QVERIFY(!CryptoVerifierProtocol::crossCurveDleqRequest(
            nonce, keyA, QString(64, '3'), {}, QByteArray(32, 'x')).valid);
        const QByteArray oversized(256 * 1024 + 1, 'x');
        QVERIFY(!CryptoVerifierProtocol::crossCurveDleqRequest(
            nonce, keyA, QString(64, '3'), oversized,
            CryptoVerifierProtocol::dleqProofCommitment(oversized)).valid);
    }
    void crossLanguageRequestResponseBinding()
    {
        const QString verifier = qEnvironmentVariable("MONZERO_DEX_CRYPTO_VERIFIER");
        if (verifier.isEmpty()) QSKIP("Pinned Rust verifier path was not supplied");
        const auto request = CryptoVerifierProtocol::adaptorSignatureRequest(
            nonce, digest, keyA, keyB, QString(324, 'a'));
        QVERIFY(request.valid);
        QProcess process;
        process.setProcessEnvironment(QProcessEnvironment{});
        process.setProgram(verifier);
        process.start();
        QVERIFY(process.waitForStarted(5000));
        const QByteArray encoded = QJsonDocument(request.message).toJson(QJsonDocument::Compact);
        QCOMPARE(process.write(encoded), qint64(encoded.size()));
        process.closeWriteChannel();
        QVERIFY(process.waitForFinished(30000));
        QCOMPARE(process.exitStatus(), QProcess::NormalExit);
        QCOMPARE(process.exitCode(), 0);
        QVERIFY(process.readAllStandardError().isEmpty());
        const QVariantMap result = CryptoVerifierProtocol::validateResponse(
            process.readAllStandardOutput(), request);
        QVERIFY2(result.value("valid").toBool(), qPrintable(result.value("error").toString()));
        QVERIFY(!result.value("cryptographicallyVerified").toBool());
        QVERIFY(!result.value("error").toString().isEmpty());
        QVERIFY(!result.value("executable").toBool());
    }
};

QTEST_GUILESS_MAIN(CryptoVerifierProtocolTest)
#include "test_crypto_verifier_protocol.moc"
