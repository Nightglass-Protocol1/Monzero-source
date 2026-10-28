#include "ReadOnlyRpcClient.h"
#include "NodeRpcValidator.h"

#include <QEventLoop>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUuid>
#include <algorithm>

namespace {
const QStringList intrinsicallyReadOnlyMethods{
    "decodepsbt", "estimatesmartfee", "get_balance", "get_block", "get_height",
    "get_info", "get_transfer_by_txid", "getbestblockhash", "getblockchaininfo",
    "getblockhash", "getblockheader", "getmempoolinfo", "getnetworkinfo", "getrawtransaction",
    "gettxout", "testmempoolaccept"};
}

ReadOnlyRpcClient::ReadOnlyRpcClient(QUrl endpoint, QStringList allowedMethods,
                                     int timeoutMilliseconds, int maximumBytes)
    : m_endpoint(std::move(endpoint)), m_allowedMethods(std::move(allowedMethods)),
      m_timeoutMilliseconds(timeoutMilliseconds), m_maximumBytes(maximumBytes)
{
    m_allowedMethods.removeDuplicates();
}

bool ReadOnlyRpcClient::configured() const
{
    return NodeRpcValidator::loopbackEndpoint(m_endpoint) && !m_allowedMethods.isEmpty()
        && std::all_of(m_allowedMethods.cbegin(), m_allowedMethods.cend(),
                       [](const QString &method) {
                           return intrinsicallyReadOnlyMethods.contains(method);
                       })
        && m_timeoutMilliseconds > 0 && m_timeoutMilliseconds <= 30000
        && m_maximumBytes > 0 && m_maximumBytes <= 4 * 1024 * 1024;
}

ReadOnlyRpcClient::Result ReadOnlyRpcClient::call(const QString &method,
                                                  const QJsonValue &parameters,
                                                  const QByteArray &authorization) const
{
    Result output;
    if (!configured()) { output.error = "Invalid RPC client configuration"; return output; }
    if (!m_allowedMethods.contains(method)) { output.error = "RPC method is not read-only allowlisted"; return output; }
    if (authorization.contains('\n') || authorization.contains('\r')) {
        output.error = "Invalid authorization header"; return output;
    }

    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (!parameters.isObject() && !parameters.isArray()) {
        output.error = "RPC parameters must be an object or array"; return output;
    }
    const QJsonObject envelope{{"jsonrpc", "2.0"}, {"id", id}, {"method", method},
                               {"params", parameters}};
    QNetworkRequest request(m_endpoint);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    if (!authorization.isEmpty()) request.setRawHeader("Authorization", authorization);

    QNetworkAccessManager manager;
    // Do not expose node or wallet credentials to a configured proxy.
    manager.setProxy(QNetworkProxy::NoProxy);
    QNetworkReply *reply = manager.post(request, QJsonDocument(envelope).toJson(QJsonDocument::Compact));
    QEventLoop loop;
    QTimer timer;
    QByteArray body;
    bool oversized = false;
    reply->setReadBufferSize(m_maximumBytes + 1);
    const auto consume = [&] {
        if (oversized) return;
        body += reply->read(m_maximumBytes + 1 - body.size());
        if (body.size() > m_maximumBytes) {
            oversized = true;
            reply->abort();
        }
    };
    QObject::connect(reply, &QNetworkReply::readyRead, &loop, consume);
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, reply, &QNetworkReply::abort);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    timer.start(m_timeoutMilliseconds);
    loop.exec();
    timer.stop();
    if (reply->bytesAvailable()) consume();

    output.httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const bool timedOut = reply->error() == QNetworkReply::OperationCanceledError;
    const auto networkError = reply->error();
    reply->deleteLater();
    if (oversized) { output.error = "RPC response exceeds size limit"; return output; }
    if (timedOut) { output.error = "RPC request timed out"; return output; }
    if (networkError != QNetworkReply::NoError) { output.error = "RPC transport failed"; return output; }
    if (output.httpStatus != 200) { output.error = "Unexpected RPC HTTP status"; return output; }

    QJsonValue value;
    QString rpcErrorMessage;
    const QVariantMap parsed = NodeRpcValidator::parseResponseEnvelope(
        body, id, value, output.rpcErrorCode, rpcErrorMessage, m_maximumBytes);
    if (!parsed.value("valid").toBool()) { output.error = parsed.value("error").toString(); return output; }
    if (parsed.value("rpcError").toBool()) {
        output.rpcError = true;
        output.error = "Node returned RPC error " + QString::number(output.rpcErrorCode);
        return output;
    }
    output.ok = true;
    output.payload = value;
    if (value.isObject()) output.value = value.toObject();
    return output;
}
