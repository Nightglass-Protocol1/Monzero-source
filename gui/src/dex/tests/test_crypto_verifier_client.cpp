#include "../CryptoVerifierClient.h"

#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

namespace {
const QString expectedHash =
    "eda07a44e9ce9dd2d5a3a3265081e266ea4da73350921a9c64c7f5a81ee6d9a5";
const QString nonce(64, '1'), digest(64, '2');
const QString keyA = "0279be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798";
const QString keyB = "02c6047f9441ed7d6d3045406e95c07cd85c778e4b8cef3ca7abac09b95c709ee5";
}

class CryptoVerifierClientTest : public QObject
{
    Q_OBJECT
private slots:
    void rejectsMutatedRequestBeforeConfigurationLookup()
    {
        CryptoVerifierClient client("/does/not/exist", expectedHash);
        auto request = CryptoVerifierProtocol::adaptorSignatureRequest(
            nonce, digest, keyA, keyB, QString(324, 'a'));
        request.message["digestHex"] = QString(64, '7');
        const auto result = client.verify(request);
        QCOMPARE(result.value("error").toString(), QString("Inconsistent crypto-verifier request"));
        QVERIFY(!result.value("valid").toBool());
        QVERIFY(!result.value("responseAuthenticated").toBool());
        QVERIFY(!result.value("cryptographicallyVerified").toBool());
        QVERIFY(!result.value("executable").toBool());
    }

    void authenticatesPinnedSandboxedChild()
    {
        const QString verifier = qEnvironmentVariable("MONZERO_DEX_CRYPTO_VERIFIER");
        if (verifier.isEmpty()) QSKIP("Pinned Rust verifier path was not supplied");
        CryptoVerifierClient client(verifier, expectedHash);
        QVERIFY(client.configured());
        const auto request = CryptoVerifierProtocol::adaptorSignatureRequest(
            nonce, digest, keyA, keyB, QString(324, 'a'));
        const QVariantMap result = client.verify(request);
        QVERIFY2(result.value("valid").toBool(), qPrintable(result.value("error").toString()));
        QVERIFY(result.value("responseAuthenticated").toBool());
        QVERIFY(result.value("sandboxed").toBool());
        QVERIFY(!result.value("cryptographicallyVerified").toBool());
        QVERIFY(!result.value("executable").toBool());
    }
    void rejectsWrongHashAndUnsafeConfiguration()
    {
        const QString verifier = qEnvironmentVariable("MONZERO_DEX_CRYPTO_VERIFIER");
        if (verifier.isEmpty()) QSKIP("Pinned Rust verifier path was not supplied");
        CryptoVerifierClient wrongHash(verifier, QString(64, '0'));
        QVERIFY(!wrongHash.configured());
        CryptoVerifierClient missing("/does/not/exist", expectedHash);
        QVERIFY(!missing.configured());
        CryptoVerifierClient badTimeout(verifier, expectedHash, 0);
        QVERIFY(!badTimeout.configured());
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString writableCopy = directory.filePath("verifier");
        QVERIFY(QFile::copy(verifier, writableCopy));
        QVERIFY(QFile::setPermissions(writableCopy, QFileDevice::ReadOwner
            | QFileDevice::WriteOwner | QFileDevice::ExeOwner));
        CryptoVerifierClient unsafePermissions(writableCopy, expectedHash);
        QVERIFY(!unsafePermissions.configured());
    }
};

QTEST_GUILESS_MAIN(CryptoVerifierClientTest)
#include "test_crypto_verifier_client.moc"
