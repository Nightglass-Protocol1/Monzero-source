#include "../BitcoinEcdsaSignatureVerifier.h"

#include <QtTest>

class BitcoinEcdsaSignatureVerifierTest : public QObject
{
    Q_OBJECT
private slots:
    void matchesOfficialBip143SignatureVector()
    {
        const QVariantMap result = BitcoinEcdsaSignatureVerifier::verifyAll(
            "c37af31116d1b27caf68aae9e3ac82f1477929014d5b917657d0eb49478cb670",
            "025476c2e83188368da1ff3e292e7acafcdb3566bb0ad253f62fc70f07aeee6357",
            "304402203609e17b84f6a7d30c80bfa610b5b4542f32a8a0d5447a12fb1366d7f01cc44a"
            "0220573a954c4518331561406f90300e8f3358f51928d43c212a8caed02de67eebee01");
        QVERIFY2(result.value("valid").toBool(), qPrintable(result.value("error").toString()));
        QVERIFY(result.value("signatureVerified").toBool());
        QVERIFY(!result.value("executable").toBool());
    }

    void rejectsMutationWrongTypeAndMalformedDer()
    {
        const QString digest =
            "c37af31116d1b27caf68aae9e3ac82f1477929014d5b917657d0eb49478cb670";
        const QString key =
            "025476c2e83188368da1ff3e292e7acafcdb3566bb0ad253f62fc70f07aeee6357";
        const QString signature =
            "304402203609e17b84f6a7d30c80bfa610b5b4542f32a8a0d5447a12fb1366d7f01cc44a"
            "0220573a954c4518331561406f90300e8f3358f51928d43c212a8caed02de67eebee01";
        QString changed = digest; changed[0] = 'd';
        QVERIFY(!BitcoinEcdsaSignatureVerifier::verifyAll(changed, key, signature)
                     .value("valid").toBool());
        changed = signature; changed.chop(2); changed += "02";
        QVERIFY(!BitcoinEcdsaSignatureVerifier::verifyAll(digest, key, changed)
                     .value("valid").toBool());
        changed = signature; changed.insert(4, "00");
        QVERIFY(!BitcoinEcdsaSignatureVerifier::verifyAll(digest, key, changed)
                     .value("valid").toBool());
        const QString highS =
            "304502203609e17b84f6a7d30c80bfa610b5b4542f32a8a0d5447a12fb1366d7f01cc44a"
            "022100a8c56ab3bae7ccea9ebf906fcff170cb61b9c3bddb0c7f1133238e5ee9b7555301";
        QVERIFY(!BitcoinEcdsaSignatureVerifier::verifyAll(digest, key, highS)
                     .value("valid").toBool());
        QVERIFY(!BitcoinEcdsaSignatureVerifier::verifyAll(digest,
                     "04" + key.mid(2), signature).value("valid").toBool());
    }
};

QTEST_GUILESS_MAIN(BitcoinEcdsaSignatureVerifierTest)
#include "test_bitcoin_ecdsa_signature.moc"
