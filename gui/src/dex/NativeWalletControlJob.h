#ifndef MONZERO_NATIVE_WALLET_CONTROL_JOB_H
#define MONZERO_NATIVE_WALLET_CONTROL_JOB_H
#include "SwapWalletRpcProcess.h"
#include <QPointer>
#include <QVariantMap>
#include <QDeadlineTimer>
class QProcess;
class QTimer;

// Native callers only. Program/session must be locally provisioned and verified.
// Caller retains responsibility for its password copies and exclusive RPC access.
class NativeWalletControlJob : public QObject {
    Q_OBJECT
public:
    explicit NativeWalletControlJob(SwapWalletRpcProcess *owner, QObject *parent = nullptr);
    ~NativeWalletControlJob() override;
    bool start(const QString &program, const QString &session, const QString &operation,
        const QString &walletId, QByteArray password, const QString &expectedAddress, int timeoutMs = 30000);
    void cancel();
    bool isActive() const { return m_used && !m_done; }
    static bool validRequest(const QString &operation, const QString &walletId,
        const QByteArray &password, const QString &expectedAddress);
    QVariantMap result() const { return m_result; }
signals:
    void completed(bool verified);
private:
    void drain();
    void finish(bool success);
    QPointer<SwapWalletRpcProcess> m_owner;
    QProcess *m_process;
    QTimer *m_timeout;
    QByteArray m_input, m_output;
    QString m_token, m_request, m_operation, m_expected;
    QVariantMap m_result;
    QDeadlineTimer m_deadline;
    qint64 m_errors = 0;
    bool m_used = false, m_done = true;
};
#endif
