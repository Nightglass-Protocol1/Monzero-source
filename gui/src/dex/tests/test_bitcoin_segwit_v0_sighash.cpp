#include "../BitcoinSegwitV0Sighash.h"

#include <QJsonArray>
#include <QtTest>

namespace {
QJsonObject input(const QString &txid, int vout, quint32 sequence)
{
    return {{"txid", txid}, {"vout", vout},
            {"scriptSig", QJsonObject{{"hex", ""}}},
            {"sequence", static_cast<double>(sequence)}};
}

QJsonObject output(double value, const QString &script)
{
    return {{"value", value}, {"scriptPubKey", QJsonObject{{"hex", script}}}};
}
}

class BitcoinSegwitV0SighashTest : public QObject
{
    Q_OBJECT
private slots:
    void matchesOfficialBip143NativeP2wpkhVector()
    {
        // BIP143's native P2WPKH SIGHASH_ALL vector. Transaction IDs are in
        // display order here; the calculator performs wire-order reversal.
        const QJsonObject tx{
            {"version", 1}, {"locktime", 17},
            {"vin", QJsonArray{
                input("9f96ade4b41d5433f4eda31e1738ec2b36f6e7d1420d94a6af99801a88f7f7ff",
                      0, 0xffffffeeU),
                input("8ac60eb9575db5b2d987e29f301b5b819ea83a5c6579d282d189cc04b8e151ef",
                      1, 0xffffffffU)}},
            {"vout", QJsonArray{
                output(1.12340000, "76a9148280b37df378db99f66f85c95a783a76ac7a6d5988ac"),
                output(2.23450000, "76a9143bde42dbee7e4dbe6a21b2d50ce2f0167faa815988ac")}}};
        const QVariantMap result = BitcoinSegwitV0Sighash::computeAll(
            tx, 1, "76a9141d0f172a0ecb48aee1be1f2687d2963ae33f71a188ac", "600000000");
        QVERIFY2(result.value("valid").toBool(), qPrintable(result.value("error").toString()));
        QCOMPARE(result.value("digestHex").toString(),
                 QString("c37af31116d1b27caf68aae9e3ac82f1477929014d5b917657d0eb49478cb670"));
        QVERIFY(result.value("sighashVerified").toBool());
        QVERIFY(!result.value("executable").toBool());
    }

    void rejectsMalformedAndAmbiguousInputs()
    {
        QJsonObject tx{{"version", 2}, {"locktime", 0},
            {"vin", QJsonArray{input(QString(64, 'a'), 0, 1)}},
            {"vout", QJsonArray{output(0.00001000, "0014" + QString(40, 'b'))}}};
        QVERIFY(!BitcoinSegwitV0Sighash::computeAll(tx, 1, "51", "2000")
                     .value("valid").toBool());
        QVERIFY(!BitcoinSegwitV0Sighash::computeAll(tx, 0, "5", "2000")
                     .value("valid").toBool());
        QVERIFY(!BitcoinSegwitV0Sighash::computeAll(tx, 0, "51", "02000")
                     .value("valid").toBool());
        QVERIFY(!BitcoinSegwitV0Sighash::computeAll(tx, 0, "51", "2100000000000001")
                     .value("valid").toBool());
        QJsonArray vin = tx.value("vin").toArray();
        QJsonObject signedInput = vin[0].toObject();
        signedInput["txinwitness"] = QJsonArray{"00"};
        vin[0] = signedInput;
        QJsonObject signedTx = tx; signedTx["vin"] = vin;
        QVERIFY(!BitcoinSegwitV0Sighash::computeAll(signedTx, 0, "51", "2000")
                     .value("valid").toBool());
        QJsonObject changed = vin[0].toObject(); changed["txid"] = QString(63, 'a');
        vin[0] = changed; tx["vin"] = vin;
        QVERIFY(!BitcoinSegwitV0Sighash::computeAll(tx, 0, "51", "2000")
                     .value("valid").toBool());
    }
};

QTEST_GUILESS_MAIN(BitcoinSegwitV0SighashTest)
#include "test_bitcoin_segwit_v0_sighash.moc"
