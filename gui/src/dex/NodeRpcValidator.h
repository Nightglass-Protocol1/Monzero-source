#ifndef MONZERO_NODE_RPC_VALIDATOR_H
#define MONZERO_NODE_RPC_VALIDATOR_H

#include <QByteArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QUrl>
#include <QVariantMap>

namespace NodeRpcValidator {
bool loopbackEndpoint(const QUrl &endpoint);
QVariantMap parseResponseEnvelope(const QByteArray &body, const QString &expectedId,
                                  QJsonValue &result, int &rpcErrorCode,
                                  QString &rpcErrorMessage,
                                  int maximumBytes = 1024 * 1024);
QVariantMap parseValueEnvelope(const QByteArray &body, const QString &expectedId,
                               QJsonValue &result, int maximumBytes = 1024 * 1024);
QVariantMap parseEnvelope(const QByteArray &body, const QString &expectedId,
                          QJsonObject &result, int maximumBytes = 1024 * 1024);
QVariantMap bitcoin(const QJsonObject &info, const QString &genesis,
                    const QString &expectedGenesis);
QVariantMap monzero(const QJsonObject &info, const QString &genesis,
                    const QString &expectedGenesis);
}

#endif
