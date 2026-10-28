#include "../ReadOnlyRpcClient.h"
#include "../NodeRpcValidator.h"
#include "../BitcoinFeeEvidence.h"
#include "../BitcoinPsbtEvidence.h"
#include "../BitcoinPsbtRpcVerifier.h"
#include "../BitcoinSettlementTemplate.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QtTest>

class BitcoinRegtestIntegrationTest : public QObject
{
    Q_OBJECT
private slots:
    void validatesRealIsolatedNode()
    {
        const auto binaryDirectory = qEnvironmentVariable("MONZERO_DEX_BITCOIN_BIN");
        if (binaryDirectory.isEmpty()) QSKIP("Explicit verified Bitcoin binary directory required");
        QTemporaryDir data(QDir::currentPath() + "/dex-regtest-XXXXXX");
        QVERIFY(data.isValid());
        QTcpServer portReservation;
        QVERIFY(portReservation.listen(QHostAddress::LocalHost));
        const auto port = portReservation.serverPort();
        portReservation.close();
        const QStringList common{"-regtest", "-datadir=" + data.path(),
                                 "-rpcport=" + QString::number(port)};
        struct Node {
            QProcess process;
            ~Node() {
                if (process.state() == QProcess::NotRunning) return;
                process.terminate();
                if (!process.waitForFinished(10000)) {
                    process.kill(); process.waitForFinished(3000);
                }
            }
        } node;
        node.process.start(binaryDirectory + "/bitcoind", common + QStringList{
            "-server=1", "-listen=0", "-connect=0", "-dnsseed=0", "-discover=0",
            "-rpcbind=127.0.0.1", "-rpcallowip=127.0.0.1", "-dbcache=64",
            "-maxmempool=10", "-printtoconsole=0"});
        QVERIFY(node.process.waitForStarted(5000));
        const auto cookiePath = data.filePath("regtest/.cookie");
        QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(cookiePath), 10000);
        QFile cookie(cookiePath);
        QVERIFY(cookie.open(QIODevice::ReadOnly));
        const auto authorization = "Basic " + cookie.readAll().trimmed().toBase64();
        cookie.close();
        ReadOnlyRpcClient client(QUrl("http://127.0.0.1:" + QString::number(port) + "/"),
            {"getblockchaininfo", "getblockhash", "getblockheader", "getnetworkinfo", "estimatesmartfee"});
        QVERIFY(client.configured());
        ReadOnlyRpcClient::Result info;
        QTRY_VERIFY2_WITH_TIMEOUT((info = client.call("getblockchaininfo", QJsonArray{}, authorization)).ok,
                                 qPrintable(info.error), 10000);
        const QString expectedGenesis = "0f9188f13cb7b2c71f2a335e3a4fc328bf5beb436012afca590b1a11466e2206";
        const auto genesis = client.call("getblockhash", QJsonArray{0}, authorization);
        QVERIFY(genesis.ok); QCOMPARE(genesis.payload.toString(), expectedGenesis);
        QCOMPARE(info.value.value("blocks").toInt(-1), 0);
        QVERIFY(!NodeRpcValidator::bitcoin(info.value, expectedGenesis, expectedGenesis).value("ready").toBool());
        QVERIFY(!client.call("getblockchaininfo", QJsonArray{}, "Basic d3Jvbmc6d3Jvbmc=").ok);
        QVERIFY(!client.call("stop", QJsonArray{}, authorization).ok);

        // Only the isolated fixture setup may mine; the adapter has no write methods.
        QProcess mine;
        mine.start(binaryDirectory + "/bitcoin-cli", common + QStringList{
            "generatetodescriptor", "1", "raw(51)"});
        QVERIFY(mine.waitForFinished(10000));
        QCOMPARE(mine.exitStatus(), QProcess::NormalExit); QCOMPARE(mine.exitCode(), 0);
        info = client.call("getblockchaininfo", QJsonArray{}, authorization);
        QVERIFY(info.ok); QCOMPARE(info.value.value("blocks").toInt(-1), 1);
        QVERIFY(NodeRpcValidator::bitcoin(info.value, expectedGenesis, expectedGenesis).value("ready").toBool());
        QVERIFY(!NodeRpcValidator::bitcoin(info.value, expectedGenesis, QString(64, '1')).value("ready").toBool());
        const auto estimate = client.call("estimatesmartfee", QJsonArray{6, "conservative"}, authorization);
        QVERIFY(estimate.ok); QVERIFY(estimate.payload.isObject());
        // A fresh regtest node has no fee history. A successfully decoded RPC
        // response must not be promoted to a verified network fee estimate.
        QVERIFY(!estimate.value.contains("feerate"));
        QVERIFY(!estimate.value.value("errors").toArray().isEmpty());
        const auto fee = BitcoinFeeEvidence::validate(estimate.value, 6, 200, "200", "1000");
        QVERIFY(!fee.value("valid").toBool());
        QVERIFY(!fee.value("feeEstimateVerified").toBool());
        QVERIFY(!fee.value("executable").toBool());
        const auto peers = client.call("getnetworkinfo", QJsonArray{}, authorization);
        QVERIFY(peers.ok); QCOMPARE(peers.value.value("connections").toInt(-1), 0);
        const auto tip = info.value.value("bestblockhash").toString();
        QVERIFY(tip != expectedGenesis);
        auto header = client.call("getblockheader", QJsonArray{tip, true}, authorization);
        QVERIFY(header.ok); QCOMPARE(header.value.value("confirmations").toInt(-2), 1);
        QVERIFY(!client.call("invalidateblock", QJsonArray{tip}, authorization).ok);
        QProcess disconnectBlock;
        disconnectBlock.start(binaryDirectory + "/bitcoin-cli", common + QStringList{"invalidateblock", tip});
        QVERIFY(disconnectBlock.waitForFinished(10000)); QCOMPARE(disconnectBlock.exitCode(), 0);
        info = client.call("getblockchaininfo", QJsonArray{}, authorization);
        QVERIFY(info.ok); QCOMPARE(info.value.value("blocks").toInt(-1), 0);
        QCOMPARE(info.value.value("bestblockhash").toString(), expectedGenesis);
        header = client.call("getblockheader", QJsonArray{tip, true}, authorization);
        QVERIFY(header.ok); QCOMPARE(header.value.value("confirmations").toInt(-2), -1);
        // The detached block remains queryable: RPC success is not confirmation.
        QVERIFY(!client.call("reconsiderblock", QJsonArray{tip}, authorization).ok);
        QProcess reconnectBlock;
        reconnectBlock.start(binaryDirectory + "/bitcoin-cli", common + QStringList{"reconsiderblock", tip});
        QVERIFY(reconnectBlock.waitForFinished(10000)); QCOMPARE(reconnectBlock.exitCode(), 0);
        info = client.call("getblockchaininfo", QJsonArray{}, authorization);
        QVERIFY(info.ok); QCOMPARE(info.value.value("bestblockhash").toString(), tip);
        header = client.call("getblockheader", QJsonArray{tip, true}, authorization);
        QVERIFY(header.ok); QCOMPARE(header.value.value("confirmations").toInt(-2), 1);
        const auto cli = [&](const QStringList &arguments) -> QJsonValue {
            QProcess command;
            command.start(binaryDirectory + "/bitcoin-cli", common + arguments);
            if (!command.waitForFinished(15000) || command.exitStatus() != QProcess::NormalExit
                || command.exitCode() != 0) return QJsonValue(QJsonValue::Undefined);
            const auto output = command.readAllStandardOutput().trimmed();
            // bitcoin-cli prints strings without JSON quotes.
            QJsonParseError error;
            const auto parsed = QJsonDocument::fromJson("[" + output + "]", &error);
            return error.error == QJsonParseError::NoError && parsed.array().size() == 1
                ? parsed.array()[0] : QJsonValue(QString::fromUtf8(output));
        };
        QVERIFY(cli({"createwallet", "dex-disposable"}).isObject());
        const auto address = cli({"getnewaddress", "", "bech32"}).toString();
        QVERIFY(address.startsWith("bcrt1q"));
        QCOMPARE(cli({"generatetoaddress", "101", address}).toArray().size(), 101);
        const auto fundingTxid = cli({"-named", "sendtoaddress", "address=" + address,
                                     "amount=1", "fee_rate=1"}).toString();
        QCOMPARE(fundingTxid.size(), 64);
        const auto funding = cli({"getrawtransaction", fundingTxid, "true"}).toObject();
        int fundingVout = -1;
        for (const auto &value : funding.value("vout").toArray()) {
            const auto output = value.toObject();
            if (output.value("value").toDouble() == 1.0
                && output.value("scriptPubKey").toObject().value("address").toString() == address)
                fundingVout = output.value("n").toInt(-1);
        }
        QVERIFY(fundingVout >= 0);
        QCOMPARE(cli({"generatetoaddress", "6", address}).toArray().size(), 6);
        const QString keyA = "0279be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798";
        const QString keyB = "02c6047f9441ed7d6d3045406e95c07cd85c778e4b8cef3ca7abac09b95c709ee5";
        const auto descriptor = cli({"getdescriptorinfo", "wsh(and_v(v:pk(" + keyA + "),pk(" + keyB + ")))"})
                                    .toObject().value("descriptor").toString();
        QVERIFY(!descriptor.isEmpty());
        const auto sharedAddresses = cli({"deriveaddresses", descriptor}).toArray();
        QCOMPARE(sharedAddresses.size(), 1);
        const auto sharedAddress = sharedAddresses.at(0).toString();
        QVERIFY(!sharedAddress.isEmpty());
        const QString script = "0020" + QString::fromLatin1(QCryptographicHash::hash(
            QByteArray::fromHex(("21" + keyA + "ad21" + keyB + "ac").toLatin1()), QCryptographicHash::Sha256).toHex());
        const auto compact = [](const QJsonArray &array) { return QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact)); };
        const auto unsignedPsbt = cli({"createpsbt",
            compact(QJsonArray{QJsonObject{{"txid", fundingTxid}, {"vout", fundingVout}, {"sequence", 4294967295.0}}}),
            compact(QJsonArray{QJsonObject{{sharedAddress, 0.00010000}}, QJsonObject{{address, 0.99989800}}}), "0", "false"}).toString();
        QVERIFY(!unsignedPsbt.isEmpty());
        const auto updated = cli({"utxoupdatepsbt", unsignedPsbt}).toString();
        QVERIFY(!updated.isEmpty());
        ReadOnlyRpcClient psbtClient(QUrl("http://127.0.0.1:" + QString::number(port) + "/"),
                                    {"decodepsbt", "gettxout", "getbestblockhash"});
        const auto decoded = psbtClient.call("decodepsbt", QJsonArray{updated}, authorization);
        QVERIFY(decoded.ok);
        const auto best = psbtClient.call("getbestblockhash", QJsonArray{}, authorization);
        QVERIFY(best.ok);
        const auto observed = psbtClient.call("gettxout", QJsonArray{fundingTxid, fundingVout, true}, authorization);
        QVERIFY(observed.ok); QVERIFY(observed.payload.isObject());
        auto prevout = observed.value;
        prevout["txid"] = fundingTxid; prevout["vout"] = fundingVout;
        const auto commitment = BitcoinSettlementTemplate::commitment(decoded.value.value("tx").toObject());
        const auto lock = BitcoinPsbtEvidence::validateLock(decoded.value, QJsonArray{prevout},
            commitment, sharedAddress, script, keyA, keyB, "10000", "1000", best.payload.toString(), 6);
        QVERIFY2(lock.value("valid").toBool(), qPrintable(lock.value("error").toString()));
        QVERIFY(lock.value("provenanceVerified").toBool());
        QVERIFY(lock.value("scriptVerified").toBool());
        QCOMPARE(lock.value("feeAtomic").toString(), QString("200"));
        QVERIFY(!lock.value("executable").toBool());
        QVERIFY(!BitcoinPsbtEvidence::validateLock(decoded.value, QJsonArray{prevout},
            commitment, sharedAddress, script, keyA, keyB, "9999", "1000", best.payload.toString(), 6)
            .value("valid").toBool());
        QVERIFY(!BitcoinPsbtEvidence::validateLock(decoded.value, QJsonArray{prevout},
            commitment, sharedAddress, script, keyA, keyB, "10000", "1000", best.payload.toString(), 7)
            .value("valid").toBool());
        auto substituted = prevout;
        substituted["value"] = 2.0;
        QVERIFY(!BitcoinPsbtEvidence::validateLock(decoded.value, QJsonArray{substituted},
            commitment, sharedAddress, script, keyA, keyB, "10000", "1000", best.payload.toString(), 6)
            .value("valid").toBool());
        // Spend the same fixture input back to its wallet, never to the DEX lock.
        const auto competingRaw = cli({"createrawtransaction",
            compact(QJsonArray{QJsonObject{{"txid", fundingTxid}, {"vout", fundingVout}}}),
            compact(QJsonArray{QJsonObject{{address, 0.99999000}}})}).toString();
        QVERIFY(!competingRaw.isEmpty());
        const auto signedCompeting = cli({"signrawtransactionwithwallet", competingRaw}).toObject();
        QVERIFY(signedCompeting.value("complete").toBool());
        const auto competingTxid = cli({"sendrawtransaction", signedCompeting.value("hex").toString()}).toString();
        QCOMPARE(competingTxid.size(), 64);
        ReadOnlyRpcClient lockClient(QUrl("http://127.0.0.1:" + QString::number(port) + "/"),
            {"getbestblockhash", "decodepsbt", "gettxout", "estimatesmartfee"});
        for (bool confirmed : {false, true}) {
            if (confirmed) QCOMPARE(cli({"generatetoaddress", "1", address}).toArray().size(), 1);
            const auto missing = psbtClient.call("gettxout", QJsonArray{fundingTxid, fundingVout, true}, authorization);
            QVERIFY(missing.ok); QVERIFY(missing.payload.isNull());
            const auto stale = BitcoinPsbtRpcVerifier::validateLock(lockClient, updated.toLatin1(),
                authorization, commitment, sharedAddress, script, keyA, keyB, "10000", "1000", 6);
            QVERIFY(!stale.value("valid").toBool());
            QVERIFY(!stale.value("rpcVerified").toBool());
            QVERIFY(!stale.value("executable").toBool());
            QCOMPARE(stale.value("error").toString(), QString("Bitcoin PSBT input is missing or spent"));
        }
        QProcess stop;
        stop.start(binaryDirectory + "/bitcoin-cli", common + QStringList{"stop"});
        QVERIFY(stop.waitForFinished(10000)); QCOMPARE(stop.exitCode(), 0);
        QVERIFY(node.process.waitForFinished(10000)); QCOMPARE(node.process.exitCode(), 0);
    }
};

QTEST_GUILESS_MAIN(BitcoinRegtestIntegrationTest)
#include "test_bitcoin_regtest_integration.moc"
