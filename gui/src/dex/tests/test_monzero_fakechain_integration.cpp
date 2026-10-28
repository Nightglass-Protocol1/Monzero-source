#include "../ReadOnlyRpcClient.h"
#include "../NodeRpcValidator.h"
#include <QDir>
#include <QJsonDocument>
#include <QProcess>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QtTest>

class MonzeroFakechainIntegrationTest : public QObject
{
    Q_OBJECT
private slots:
    void verifiesIsolatedGenesisAcrossRestart()
    {
        const auto binary = qEnvironmentVariable("MONZERO_DEX_MONZEROD");
        if (binary.isEmpty()) QSKIP("Explicit current Monzero daemon path required");
        QTemporaryDir data(QDir::currentPath() + "/dex-fakechain-XXXXXX");
        QVERIFY(data.isValid());
        const QString genesis = "84f9ebdac8924806f037482ec16fd59b271e954d3e00363dd6c7e4ce9dd659e4";
        QString minedHash;
        for (int attempt = 0; attempt < 2; ++attempt) {
            QTcpServer rpcPort, p2pPort;
            QVERIFY(rpcPort.listen(QHostAddress::LocalHost));
            QVERIFY(p2pPort.listen(QHostAddress::LocalHost));
            const auto rpc = rpcPort.serverPort(), p2p = p2pPort.serverPort();
            rpcPort.close(); p2pPort.close();
            struct Node {
                QProcess process;
                ~Node() {
                    if (process.state() == QProcess::NotRunning) return;
                    process.terminate();
                    if (!process.waitForFinished(15000)) {
                        process.kill(); process.waitForFinished(3000);
                    }
                }
            } node;
            node.process.start(binary, {"--regtest", "--keep-fakechain", "--fixed-difficulty", "1",
                "--offline", "--no-igd", "--hide-my-port", "--no-zmq", "--non-interactive",
                "--data-dir", data.path(), "--rpc-bind-ip", "127.0.0.1",
                "--rpc-bind-port", QString::number(rpc), "--p2p-bind-ip", "127.0.0.1",
                "--p2p-bind-port", QString::number(p2p), "--log-file", data.filePath("daemon.log")});
            QVERIFY(node.process.waitForStarted(5000));
            ReadOnlyRpcClient client(QUrl("http://127.0.0.1:" + QString::number(rpc) + "/json_rpc"),
                                     {"get_info", "get_block"});
            QVERIFY(client.configured());
            ReadOnlyRpcClient::Result info;
            QTRY_VERIFY2_WITH_TIMEOUT((info = client.call("get_info")).ok, qPrintable(info.error), 15000);
            const auto block = client.call("get_block", QJsonObject{{"height", 0}});
            QVERIFY(block.ok);
            QCOMPARE(block.value.value("block_header").toObject().value("hash").toString(), genesis);
            QCOMPARE(info.value.value("nettype").toString(), QString("fakechain"));
            QCOMPARE(info.value.value("height").toInt(-1), attempt == 0 ? 1 : 2);
            QCOMPARE(info.value.value("outgoing_connections_count").toInt(-1), 0);
            QCOMPARE(info.value.value("incoming_connections_count").toInt(-1), 0);
            const auto readiness = NodeRpcValidator::monzero(info.value, genesis, genesis);
            QVERIFY2(readiness.value("ready").toBool(), qPrintable(readiness.value("blockers").toStringList().join(", ")));
            QVERIFY(!NodeRpcValidator::monzero(info.value, genesis, QString(64, '1')).value("ready").toBool());
            QVERIFY(!client.call("generateblocks", QJsonObject{}).ok);
            QVERIFY(!client.call("send_raw_transaction", QJsonObject{}).ok);
            if (attempt == 0) {
                // Existing public functional-test address; no wallet is opened.
                const QJsonObject request{{"jsonrpc", "2.0"}, {"id", "fixture"},
                    {"method", "generateblocks"}, {"params", QJsonObject{
                        {"wallet_address", "FQLhxULkbyx4886T7196doS9GPMzexD9gXpsZJDwVjeRVdFCSoHnv7KPbBeGpzJBzHRCAs9UxqeoyFQMYbqSWYTfJJCEnHf"},
                        {"amount_of_blocks", 1}}}};
                QProcess fixture;
                fixture.start("curl", {"--fail", "--silent", "--show-error", "--noproxy", "*",
                    "--max-time", "30", "--header", "Content-Type: application/json", "--data-binary",
                    QString::fromUtf8(QJsonDocument(request).toJson(QJsonDocument::Compact)),
                    "http://127.0.0.1:" + QString::number(rpc) + "/json_rpc"});
                QVERIFY(fixture.waitForFinished(35000)); QCOMPARE(fixture.exitCode(), 0);
                const auto generated = QJsonDocument::fromJson(fixture.readAllStandardOutput()).object();
                QVERIFY(!generated.contains("error"));
                QCOMPARE(generated.value("result").toObject().value("status").toString(), QString("OK"));
            }
            const auto persistedBlock = client.call("get_block", QJsonObject{{"height", 1}});
            QVERIFY(persistedBlock.ok);
            const auto observedHash = persistedBlock.value.value("block_header").toObject().value("hash").toString();
            QCOMPARE(observedHash.size(), 64);
            if (attempt == 0) minedHash = observedHash;
            else QCOMPARE(observedHash, minedHash);
            node.process.terminate();
            QVERIFY(node.process.waitForFinished(15000));
            QCOMPARE(node.process.exitStatus(), QProcess::NormalExit);
            QCOMPARE(node.process.exitCode(), 0);
        }
    }
};

QTEST_GUILESS_MAIN(MonzeroFakechainIntegrationTest)
#include "test_monzero_fakechain_integration.moc"
