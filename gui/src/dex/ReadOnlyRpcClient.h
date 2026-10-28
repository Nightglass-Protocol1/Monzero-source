#ifndef MONZERO_READ_ONLY_RPC_CLIENT_H
#define MONZERO_READ_ONLY_RPC_CLIENT_H

#include <QJsonObject>
#include <QJsonValue>
#include <QStringList>
#include <QUrl>

class ReadOnlyRpcClient final
{
public:
    struct Result {
        bool ok = false;
        QJsonObject value;
        QJsonValue payload;
        QString error;
        int httpStatus = 0;
        bool rpcError = false;
        int rpcErrorCode = 0;
    };

    ReadOnlyRpcClient(QUrl endpoint, QStringList allowedMethods,
                      int timeoutMilliseconds = 3000, int maximumBytes = 1024 * 1024);

    // Blocking by design: invoke from an adapter worker thread, never the GUI thread.
    Result call(const QString &method, const QJsonValue &parameters = QJsonObject{},
                const QByteArray &authorization = {}) const;
    bool configured() const;

private:
    QUrl m_endpoint;
    QStringList m_allowedMethods;
    int m_timeoutMilliseconds;
    int m_maximumBytes;
};

#endif
