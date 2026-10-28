#include "../NodeRpcValidator.h"
#include <QtTest>

class NodeRpcTest : public QObject {
    Q_OBJECT
private slots:
    void endpoints() {
        QVERIFY(NodeRpcValidator::loopbackEndpoint(QUrl("http://127.0.0.1:6175")));
        QVERIFY(NodeRpcValidator::loopbackEndpoint(QUrl("http://127.0.0.1:6175/")));
        QVERIFY(NodeRpcValidator::loopbackEndpoint(QUrl("http://127.0.0.1:6175/json_rpc")));
        QVERIFY(NodeRpcValidator::loopbackEndpoint(QUrl("http://[::1]:6175")));
        QVERIFY(!NodeRpcValidator::loopbackEndpoint(QUrl("http://192.168.0.12:6175")));
        QVERIFY(!NodeRpcValidator::loopbackEndpoint(QUrl("https://127.0.0.1:6175")));
        QVERIFY(!NodeRpcValidator::loopbackEndpoint(QUrl("http://127.0.0.1")));
        QVERIFY(!NodeRpcValidator::loopbackEndpoint(QUrl("http://127.0.0.1:6175/admin")));
        QVERIFY(!NodeRpcValidator::loopbackEndpoint(QUrl("http://user:pass@127.0.0.1:6175")));
    }
    void envelopes() {
        QJsonObject result;
        QVERIFY(NodeRpcValidator::parseEnvelope(
            R"({"jsonrpc":"2.0","id":"a","result":{"status":"OK"}})", "a", result).value("valid").toBool());
        QCOMPARE(result.value("status").toString(), QString("OK"));
        QVERIFY(!NodeRpcValidator::parseEnvelope(R"({"jsonrpc":"2.0","id":"b","result":{}})", "a", result).value("valid").toBool());
        QVERIFY(!NodeRpcValidator::parseEnvelope(R"({"jsonrpc":"2.0","id":"a","error":{"code":-1}})", "a", result).value("valid").toBool());
        QVERIFY(!NodeRpcValidator::parseEnvelope("not-json", "a", result).value("valid").toBool());
        QVERIFY(!NodeRpcValidator::parseEnvelope(QByteArray(32, 'x'), "a", result, 16).value("valid").toBool());
        QJsonValue scalar;
        QVERIFY(NodeRpcValidator::parseValueEnvelope(
            R"({"jsonrpc":"2.0","id":"a","result":"tip"})", "a", scalar)
                    .value("valid").toBool());
        QCOMPARE(scalar.toString(), QString("tip"));
        QVERIFY(NodeRpcValidator::parseValueEnvelope(
            R"({"jsonrpc":"2.0","id":"a","result":null})", "a", scalar)
                    .value("valid").toBool());
        QVERIFY(scalar.isNull());
        QVERIFY(!NodeRpcValidator::parseEnvelope(
            R"({"jsonrpc":"2.0","id":"a","result":"tip"})", "a", result)
                     .value("valid").toBool());
    }
    void chainIdentity() {
        const QString btcGenesis = "0f9188f13cb7b2c71f2a335e3a4fc328bf5beb436012afca590b1a11466e2206";
        QJsonObject btc{{"chain", "regtest"}, {"initialblockdownload", false}, {"blocks", 101}, {"headers", 101}};
        QVERIFY(NodeRpcValidator::bitcoin(btc, btcGenesis, btcGenesis).value("ready").toBool());
        btc["blocks"] = 102;
        QVERIFY(!NodeRpcValidator::bitcoin(btc, btcGenesis, btcGenesis).value("ready").toBool());
        btc["blocks"] = 100;
        QVERIFY(!NodeRpcValidator::bitcoin(btc, btcGenesis, btcGenesis).value("ready").toBool());
        btc["blocks"] = 101;
        btc["chain"] = "main";
        QVERIFY(!NodeRpcValidator::bitcoin(btc, btcGenesis, btcGenesis).value("ready").toBool());
        const QString xmzGenesis = "84f9ebdac8924806f037482ec16fd59b271e954d3e00363dd6c7e4ce9dd659e4";
        QJsonObject xmz{{"nettype", "fakechain"}, {"status", "OK"}, {"synchronized", true},
                        {"untrusted", false}, {"height", 121}, {"target_height", 0}};
        QVERIFY(NodeRpcValidator::monzero(xmz, xmzGenesis, xmzGenesis).value("ready").toBool());
        xmz["untrusted"] = true;
        QVERIFY(!NodeRpcValidator::monzero(xmz, xmzGenesis, xmzGenesis).value("ready").toBool());
    }
};
QTEST_GUILESS_MAIN(NodeRpcTest)
#include "test_node_rpc.moc"
