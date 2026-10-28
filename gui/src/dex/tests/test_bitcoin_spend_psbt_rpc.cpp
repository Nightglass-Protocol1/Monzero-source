#include "../BitcoinSettlementTemplate.h"
#include "../BitcoinSpendPsbtRpcVerifier.h"

#include <QJsonArray>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QTcpServer>
#include <QTcpSocket>
#include <QtTest>
#include <functional>
#include <memory>

namespace {
const QString parentTxid = QString(64, 'b');
const QString bestBlock = QString(64, 'e');
const QString keyA = "0279be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798";
const QString keyB = "02c6047f9441ed7d6d3045406e95c07cd85c778e4b8cef3ca7abac09b95c709ee5";
const QString witnessScript = "21" + keyA + "ad21" + keyB + "ac";
const QString parentScript = "0020" + QString::fromLatin1(QCryptographicHash::hash(
    QByteArray::fromHex(witnessScript.toLatin1()), QCryptographicHash::Sha256).toHex());
const QString recipientScript = "0014" + QString(40, 'c');

QJsonObject transaction()
{
    return {{"version", 2}, {"locktime", 0}, {"vsize", 150},
        {"vin", QJsonArray{QJsonObject{{"txid", parentTxid}, {"vout", 0},
            {"scriptSig", QJsonObject{{"asm", ""}, {"hex", ""}}},
            {"sequence", 144}}}},
        {"vout", QJsonArray{QJsonObject{{"value", 0.00009700}, {"n", 0},
            {"scriptPubKey", QJsonObject{{"address", "refund"},
                {"hex", recipientScript}, {"type", "witness_v0_keyhash"}}}}}}};
}

QJsonObject decodedPsbt()
{
    return {{"tx", transaction()}, {"psbt_version", 0},
        {"inputs", QJsonArray{QJsonObject{
            {"witness_utxo", QJsonObject{{"amount", 0.00010000},
                {"scriptPubKey", QJsonObject{{"hex", parentScript},
                    {"type", "witness_v0_scripthash"}}}}}, {"sighash", "ALL"}}}},
        {"outputs", QJsonArray{QJsonObject{}}}, {"fee", 0.00000300}};
}

QJsonObject txout()
{
    return {{"bestblock", bestBlock}, {"confirmations", 12},
        {"value", 0.00010000}, {"coinbase", false},
        {"scriptPubKey", QJsonObject{{"hex", parentScript},
            {"type", "witness_v0_scripthash"}}}};
}

QByteArray psbtBytes() { return QByteArray("psbt\xff\x01\x00", 7).toBase64(); }
}

class BitcoinSpendPsbtRpcTest : public QObject
{
    Q_OBJECT
    QTcpServer server;

    QUrl endpoint() const
    {
        return QUrl(QString("http://127.0.0.1:%1/").arg(server.serverPort()));
    }

    void serve(const std::function<QJsonValue(const QJsonObject &)> &builder)
    {
        connect(&server, &QTcpServer::newConnection, &server, [this, builder] {
            QTcpSocket *socket = server.nextPendingConnection();
            connect(socket, &QTcpSocket::readyRead, socket, [socket, builder] {
                QByteArray request = socket->property("requestData").toByteArray();
                request += socket->readAll(); socket->setProperty("requestData", request);
                const int split = request.indexOf("\r\n\r\n");
                if (split < 0) return;
                const auto match = QRegularExpression("Content-Length: ([0-9]+)",
                    QRegularExpression::CaseInsensitiveOption)
                    .match(QString::fromLatin1(request.left(split)));
                if (!match.hasMatch() || request.size() - split - 4 < match.captured(1).toInt())
                    return;
                const QJsonObject sent = QJsonDocument::fromJson(request.mid(split + 4)).object();
                const QJsonObject response{{"jsonrpc", "2.0"}, {"id", sent.value("id")},
                    {"result", builder(sent)}};
                const QByteArray body = QJsonDocument(response).toJson(QJsonDocument::Compact);
                socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: "
                    + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
                socket->disconnectFromHost();
            });
        });
    }

    ReadOnlyRpcClient client() const
    {
        return ReadOnlyRpcClient(endpoint(),
            {"decodepsbt", "estimatesmartfee", "getbestblockhash", "gettxout"});
    }

    QVariantMap validate() const
    {
        return BitcoinSpendPsbtRpcVerifier::validate(client(), psbtBytes(), {},
            BitcoinSettlementTemplate::commitment(transaction()), parentTxid, "0", "10000",
            parentScript, keyA, keyB, "refund", recipientScript, "144", "500", 6);
    }

private slots:
    void init() { QVERIFY(server.listen(QHostAddress::LocalHost)); }
    void cleanup() { server.close(); server.disconnect(); }

    void validatesStableSpendEvidence()
    {
        serve([](const QJsonObject &request) -> QJsonValue {
            const QString method = request.value("method").toString();
            if (method == "getbestblockhash") return bestBlock;
            if (method == "decodepsbt") return decodedPsbt();
            if (method == "gettxout") return txout();
            if (method == "estimatesmartfee")
                return QJsonObject{{"feerate", 0.00001000}, {"blocks", 6}};
            return QJsonValue(QJsonValue::Null);
        });
        const QVariantMap result = validate();
        QVERIFY(result.value("valid").toBool());
        QVERIFY(result.value("rpcVerified").toBool());
        QVERIFY(result.value("feeEstimateVerified").toBool());
        QCOMPARE(result.value("bestBlock").toString(), bestBlock);
        QVERIFY(!result.value("executable").toBool());
    }

    void rejectsTipChangeAndSpentParent()
    {
        auto calls = std::make_shared<int>(0);
        serve([calls](const QJsonObject &request) -> QJsonValue {
            const QString method = request.value("method").toString();
            if (method == "getbestblockhash")
                return (++*calls == 1) ? bestBlock : QString(64, 'f');
            if (method == "decodepsbt") return decodedPsbt();
            return txout();
        });
        QVERIFY(!validate().value("valid").toBool());

        server.close(); server.disconnect(); QVERIFY(server.listen(QHostAddress::LocalHost));
        serve([](const QJsonObject &request) -> QJsonValue {
            if (request.value("method") == QJsonValue("getbestblockhash")) return bestBlock;
            if (request.value("method") == QJsonValue("decodepsbt")) return decodedPsbt();
            return QJsonValue(QJsonValue::Null);
        });
        QVERIFY(!validate().value("valid").toBool());
    }
};

QTEST_GUILESS_MAIN(BitcoinSpendPsbtRpcTest)
#include "test_bitcoin_spend_psbt_rpc.moc"
