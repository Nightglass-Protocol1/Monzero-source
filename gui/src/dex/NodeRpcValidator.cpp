#include "NodeRpcValidator.h"

#include <QJsonDocument>
#include <QJsonValue>
#include <QRegularExpression>
#include <cmath>
#include <limits>
#include <cmath>

namespace {
QVariantMap response(const QStringList &blockers)
{
    return {{"ready", blockers.isEmpty()}, {"blockers", blockers}};
}

bool exactInteger(const QJsonValue &value)
{
    if (!value.isDouble()) return false;
    const double number = value.toDouble();
    return std::isfinite(number) && number >= 0 && number == std::floor(number)
        && number <= 9007199254740991.0;
}

bool validGenesis(const QString &value)
{
    static const QRegularExpression pattern("^[0-9a-f]{64}$");
    return pattern.match(value).hasMatch();
}
}

namespace NodeRpcValidator {
bool loopbackEndpoint(const QUrl &endpoint)
{
    if (!endpoint.isValid() || endpoint.scheme() != "http" || endpoint.port() <= 0
        || !endpoint.userInfo().isEmpty() || endpoint.hasQuery() || endpoint.hasFragment()
        || (!endpoint.path().isEmpty() && endpoint.path() != "/"
            && endpoint.path() != "/json_rpc")) return false;
    const QString host = endpoint.host().toLower();
    return host == "127.0.0.1" || host == "::1";
}

QVariantMap parseResponseEnvelope(const QByteArray &body, const QString &expectedId,
                                  QJsonValue &result, int &rpcErrorCode,
                                  QString &rpcErrorMessage, int maximumBytes)
{
    result = QJsonValue(QJsonValue::Undefined);
    rpcErrorCode = 0;
    rpcErrorMessage.clear();
    if (maximumBytes < 1 || body.isEmpty() || body.size() > maximumBytes)
        return {{"valid", false}, {"error", "Empty or oversized RPC response"}};
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(body, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        return {{"valid", false}, {"error", "Malformed RPC JSON"}};
    const QJsonObject root = document.object();
    if (root.value("jsonrpc") != QJsonValue("2.0") || root.value("id") != QJsonValue(expectedId))
        return {{"valid", false}, {"error", "Mismatched RPC version or response id"}};
    if (!root.value("error").isNull() && !root.value("error").isUndefined()) {
        if (!root.value("error").isObject())
            return {{"valid", false}, {"error", "Malformed RPC error"}};
        const QJsonObject rpcError = root.value("error").toObject();
        const QJsonValue code = rpcError.value("code");
        const QJsonValue message = rpcError.value("message");
        if (rpcError.size() < 2 || !code.isDouble()
            || code.toDouble() != std::floor(code.toDouble())
            || code.toDouble() < std::numeric_limits<int>::min()
            || code.toDouble() > std::numeric_limits<int>::max()
            || !message.isString() || message.toString().isEmpty()
            || message.toString().size() > 1024)
            return {{"valid", false}, {"error", "Malformed RPC error"}};
        rpcErrorCode = code.toInt();
        rpcErrorMessage = message.toString();
        return {{"valid", true}, {"rpcError", true}};
    }
    if (!root.contains("result"))
        return {{"valid", false}, {"error", "Missing RPC result"}};
    result = root.value("result");
    return {{"valid", true}};
}

QVariantMap parseValueEnvelope(const QByteArray &body, const QString &expectedId,
                               QJsonValue &result, int maximumBytes)
{
    int rpcErrorCode = 0;
    QString rpcErrorMessage;
    const QVariantMap parsed = parseResponseEnvelope(
        body, expectedId, result, rpcErrorCode, rpcErrorMessage, maximumBytes);
    if (!parsed.value("valid").toBool()) return parsed;
    if (parsed.value("rpcError").toBool())
        return {{"valid", false}, {"error", "Node returned an RPC error"}};
    return parsed;
}

QVariantMap parseEnvelope(const QByteArray &body, const QString &expectedId,
                          QJsonObject &result, int maximumBytes)
{
    result = {};
    QJsonValue value;
    const QVariantMap parsed = parseValueEnvelope(body, expectedId, value, maximumBytes);
    if (!parsed.value("valid").toBool()) return parsed;
    if (!value.isObject())
        return {{"valid", false}, {"error", "Missing RPC result object"}};
    result = value.toObject();
    return {{"valid", true}};
}

QVariantMap bitcoin(const QJsonObject &info, const QString &genesis,
                    const QString &expectedGenesis)
{
    QStringList blockers;
    if (!validGenesis(expectedGenesis)) blockers << "Missing independently pinned BTC genesis";
    else if (genesis != expectedGenesis) blockers << "BTC genesis mismatch";
    if (info.value("chain") != QJsonValue("regtest")) blockers << "Bitcoin must use regtest";
    if (info.value("initialblockdownload") != QJsonValue(false)) blockers << "Bitcoin synchronization unverified";
    const auto blocks = info.value("blocks"), headers = info.value("headers");
    if (!exactInteger(blocks) || !exactInteger(headers)) blockers << "Malformed Bitcoin heights";
    else if (headers.toDouble() != blocks.toDouble())
        blockers << "Bitcoin block and header heights differ";
    return response(blockers);
}

QVariantMap monzero(const QJsonObject &info, const QString &genesis,
                    const QString &expectedGenesis)
{
    QStringList blockers;
    if (!validGenesis(expectedGenesis)) blockers << "Missing independently pinned XMZ genesis";
    else if (genesis != expectedGenesis) blockers << "XMZ genesis mismatch";
    if (info.value("nettype") != QJsonValue("fakechain")) blockers << "Monzero must use fakechain";
    if (info.value("status") != QJsonValue("OK") || info.value("synchronized") != QJsonValue(true))
        blockers << "Monzero synchronization unverified";
    if (info.value("untrusted") != QJsonValue(false)) blockers << "Untrusted Monzero response";
    const auto height = info.value("height"), target = info.value("target_height");
    if (!exactInteger(height) || !exactInteger(target)) blockers << "Malformed Monzero heights";
    else if (target.toDouble() > height.toDouble()) blockers << "Monzero node behind target";
    return response(blockers);
}
}
