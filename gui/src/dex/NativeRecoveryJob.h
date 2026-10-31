#ifndef MONZERO_NATIVE_RECOVERY_JOB_H
#define MONZERO_NATIVE_RECOVERY_JOB_H
#include "NativeRecoveryReceiver.h"
#include <QObject>
#include <QPointer>
class QProcess;
class QTimer;
class SwapWalletRpcProcess;

// Local native startup supplies the verified helper and stable private journal.
// Not a QML-invokable launcher and never a source of retry authority.
class NativeRecoveryJob : public QObject {
    Q_OBJECT
public:
    explicit NativeRecoveryJob(QObject *parent = nullptr);
    ~NativeRecoveryJob() override;
    bool start(const QString &program, const QString &digest, const QString &journalDirectory,
        SwapWalletRpcProcess *owner, const NativeRecoveryReceiver::Expected &expected,
        int timeoutMs = 30000);
    void cancel();
    bool isActive() const { return !m_done; }
    QVariantMap result() const { return m_receiver.current(); }
signals:
    void completed(bool verified);
private:
    void drain();
    void finish(bool success);
    NativeRecoveryReceiver m_receiver;
    QPointer<SwapWalletRpcProcess> m_owner;
    QProcess *m_process;
    QTimer *m_timer;
    QByteArray m_input, m_output;
    QString m_token;
    qint64 m_stderrBytes = 0;
    bool m_done = true;
};
#endif
