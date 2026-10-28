#include "../BitcoinSharedOutputScript.h"

#include <QByteArray>
#include <QCryptographicHash>
#include <QtTest>

namespace {
const QString keyA = "0279be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798";
const QString keyB = "02c6047f9441ed7d6d3045406e95c07cd85c778e4b8cef3ca7abac09b95c709ee5";

QString witness(const QString &a = keyA, const QString &b = keyB)
{
    return "21" + a + "ad21" + b + "ac";
}

QString p2wsh(const QString &script)
{
    return "0020" + QString::fromLatin1(QCryptographicHash::hash(
        QByteArray::fromHex(script.toLatin1()), QCryptographicHash::Sha256).toHex());
}
}

class BitcoinSharedOutputScriptTest : public QObject
{
    Q_OBJECT
private slots:
    void validatesReferenceDescriptorShape()
    {
        const QVariantMap result = BitcoinSharedOutputScript::validate(
            witness(), p2wsh(witness()), keyA, keyB);
        QVERIFY2(result.value("valid").toBool(),
                 qPrintable(result.value("error").toString()));
        QVERIFY(result.value("scriptVerified").toBool());
        QVERIFY(!result.value("executable").toBool());
    }

    void rejectsKeyOrderAndScriptMutation()
    {
        QVERIFY(!BitcoinSharedOutputScript::validate(
            witness(), p2wsh(witness()), keyB, keyA).value("valid").toBool());
        QString changed = witness(); changed.replace(68, 2, "ac");
        QVERIFY(!BitcoinSharedOutputScript::validate(
            changed, p2wsh(changed), keyA, keyB).value("valid").toBool());
        QVERIFY(!BitcoinSharedOutputScript::validate(
            witness(), "0020" + QString(64, '0'), keyA, keyB).value("valid").toBool());
    }

    void rejectsInvalidCurveKeys()
    {
        const QString invalid = "02" + QString(64, 'f');
        QVERIFY(!BitcoinSharedOutputScript::validate(
            witness(invalid, keyB), p2wsh(witness(invalid, keyB)), invalid, keyB)
                     .value("valid").toBool());
        QVERIFY(!BitcoinSharedOutputScript::validate(
            witness(keyA, keyA), p2wsh(witness(keyA, keyA)), keyA, keyA)
                     .value("valid").toBool());
    }
};

QTEST_GUILESS_MAIN(BitcoinSharedOutputScriptTest)
#include "test_bitcoin_shared_output_script.moc"
