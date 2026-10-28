#include "../ReadOnlyRpcClient.h"
#include <QJsonDocument>
#include <QTcpServer>
#include <QTcpSocket>
#include <QtTest>

class RpcClientTest : public QObject {
    Q_OBJECT
private:
    QTcpServer server;
    QUrl endpoint() const { return QUrl(QString("http://127.0.0.1:%1/json_rpc").arg(server.serverPort())); }
    void reply(const std::function<QJsonObject(const QJsonObject &)> &builder) {
        connect(&server, &QTcpServer::newConnection, &server, [this, builder] {
            auto *socket = server.nextPendingConnection();
            connect(socket, &QTcpSocket::readyRead, socket, [socket, builder] {
                QByteArray request = socket->property("requestData").toByteArray();
                request += socket->readAll();
                socket->setProperty("requestData", request);
                const int split = request.indexOf("\r\n\r\n");
                if (split < 0) return;
                const QRegularExpression lengthPattern("Content-Length: ([0-9]+)",
                    QRegularExpression::CaseInsensitiveOption);
                const auto lengthMatch = lengthPattern.match(QString::fromLatin1(request.left(split)));
                if (!lengthMatch.hasMatch() || request.size() - split - 4 < lengthMatch.captured(1).toInt()) return;
                const QJsonObject sent = QJsonDocument::fromJson(request.mid(split + 4)).object();
                const QByteArray body = QJsonDocument(builder(sent)).toJson(QJsonDocument::Compact);
                socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: "
                              + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
                socket->disconnectFromHost();
            });
        });
    }
private slots:
    void init() { QVERIFY(server.listen(QHostAddress::LocalHost)); }
    void cleanup() { server.close(); server.disconnect(); }
    void successfulBoundResponse() {
        reply([](const QJsonObject &request) { return QJsonObject{{"jsonrpc", "2.0"},
            {"id", request.value("id")}, {"result", QJsonObject{{"height", 121}}}}; });
        ReadOnlyRpcClient client(endpoint(), {"get_info"});
        const auto result = client.call("get_info");
        QVERIFY(result.ok);
        QCOMPARE(result.value.value("height").toInt(), 121);
        QVERIFY(result.payload.isObject());
    }
    void acceptsAllowlistedScalarResult() {
        reply([](const QJsonObject &request) { return QJsonObject{{"jsonrpc", "2.0"},
            {"id", request.value("id")}, {"result", QString(64, 'a')}}; });
        ReadOnlyRpcClient client(endpoint(), {"getbestblockhash"});
        const auto result = client.call("getbestblockhash", QJsonArray{});
        QVERIFY(result.ok);
        QCOMPARE(result.payload.toString(), QString(64, 'a'));
        QVERIFY(result.value.isEmpty());
    }
    void abortsOversizedResponseBeforeBodyCompletes() {
        connect(&server, &QTcpServer::newConnection, &server, [this] {
            auto *socket = server.nextPendingConnection();
            connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            connect(socket, &QTcpSocket::readyRead, socket, [socket] {
                socket->readAll();
                if (socket->property("sent").toBool()) return;
                socket->setProperty("sent", true);
                socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
                              "Content-Length: 1000000\r\n\r\n" + QByteArray(257, ' '));
                // Deliberately never complete the response or close the socket.
            });
        });
        ReadOnlyRpcClient client(endpoint(), {"get_info"}, 3000, 256);
        QElapsedTimer elapsed; elapsed.start();
        const auto result = client.call("get_info");
        QVERIFY(!result.ok);
        QCOMPARE(result.error, QString("RPC response exceeds size limit"));
        QVERIFY(elapsed.elapsed() < 2000);
    }
    void acceptsExactBodyLimitAndRejectsOneByteLess() {
        const auto builder = [](const QJsonObject &request) {
            return QJsonObject{{"jsonrpc", "2.0"}, {"id", request.value("id")},
                               {"result", QString(128, 'a')}};
        };
        reply(builder);
        const auto length = QJsonDocument(builder(QJsonObject{{"id", QString(36, '1')}}))
                                .toJson(QJsonDocument::Compact).size();
        ReadOnlyRpcClient exact(endpoint(), {"get_info"}, 3000, length);
        QVERIFY(exact.call("get_info").ok);
        ReadOnlyRpcClient shortLimit(endpoint(), {"get_info"}, 3000, length - 1);
        const auto result = shortLimit.call("get_info");
        QVERIFY(!result.ok);
        QCOMPARE(result.error, QString("RPC response exceeds size limit"));
    }
    void boundsChunkedResponseWithoutContentLength() {
        connect(&server, &QTcpServer::newConnection, &server, [this] {
            auto *socket = server.nextPendingConnection();
            connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            connect(socket, &QTcpSocket::readyRead, socket, [socket] {
                socket->readAll();
                if (socket->property("sent").toBool()) return;
                socket->setProperty("sent", true);
                socket->write("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"
                              "101\r\n" + QByteArray(257, ' ') + "\r\n");
                // No terminal chunk: the body-size guard must abort first.
            });
        });
        ReadOnlyRpcClient client(endpoint(), {"get_info"}, 3000, 256);
        const auto result = client.call("get_info");
        QVERIFY(!result.ok);
        QCOMPARE(result.error, QString("RPC response exceeds size limit"));
    }
    void tricklingResponseCannotExtendTotalDeadline() {
        connect(&server, &QTcpServer::newConnection, &server, [this] {
            auto *socket = server.nextPendingConnection();
            connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            connect(socket, &QTcpSocket::readyRead, socket, [socket] {
                socket->readAll();
                if (socket->property("sent").toBool()) return;
                socket->setProperty("sent", true);
                socket->write("HTTP/1.1 200 OK\r\nContent-Length: 10000\r\n\r\n");
                auto *timer = new QTimer(socket);
                connect(timer, &QTimer::timeout, socket, [socket] { socket->write(" "); });
                timer->start(20);
            });
        });
        ReadOnlyRpcClient client(endpoint(), {"get_info"}, 200, 1024);
        QElapsedTimer elapsed; elapsed.start();
        const auto result = client.call("get_info");
        QVERIFY(!result.ok);
        QCOMPARE(result.error, QString("RPC request timed out"));
        QVERIFY(elapsed.elapsed() < 1500);
    }
    void rejectsUnsafeConfigurationAndMethods() {
        ReadOnlyRpcClient external(QUrl("http://192.168.0.12:6175/json_rpc"), {"get_info"});
        QVERIFY(!external.configured());
        ReadOnlyRpcClient falselyAllowlisted(endpoint(), {"transfer"});
        QVERIFY(!falselyAllowlisted.configured());
        QVERIFY(!falselyAllowlisted.call("transfer").ok);
        ReadOnlyRpcClient client(endpoint(), {"get_info"});
        QVERIFY(!client.call("transfer").ok);
        QVERIFY(!client.call("get_info", {}, "Basic bad\nheader").ok);
        QVERIFY(!client.call("get_info", QJsonValue("bad parameters")).ok);
    }
    void rejectsMismatchedResponse() {
        reply([](const QJsonObject &) { return QJsonObject{{"jsonrpc", "2.0"}, {"id", "wrong"},
            {"result", QJsonObject{}}}; });
        ReadOnlyRpcClient client(endpoint(), {"get_info"});
        QVERIFY(!client.call("get_info").ok);
    }
    void exposesOnlyValidatedRpcErrors() {
        reply([](const QJsonObject &request) { return QJsonObject{{"jsonrpc", "2.0"},
            {"id", request.value("id")},
            {"error", QJsonObject{{"code", -5}, {"message", "not found"}}}}; });
        ReadOnlyRpcClient client(endpoint(), {"getrawtransaction"});
        const auto result = client.call("getrawtransaction", QJsonArray{"txid", false});
        QVERIFY(!result.ok);
        QVERIFY(result.rpcError);
        QCOMPARE(result.rpcErrorCode, -5);
    }
};
QTEST_GUILESS_MAIN(RpcClientTest)
#include "test_rpc_client.moc"
