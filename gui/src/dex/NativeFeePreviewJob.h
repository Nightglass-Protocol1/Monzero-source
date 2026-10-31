#ifndef MONZERO_NATIVE_FEE_PREVIEW_JOB_H
#define MONZERO_NATIVE_FEE_PREVIEW_JOB_H
#include "NativeFeePreviewController.h"
#include <QPointer>
class QProcess;
class QTimer;
class SwapWalletRpcProcess;

// One owning GUI thread. Program must be a locally verified bundled executable,
// never a path supplied by an offer/peer. This class does not verify code signing.
class NativeFeePreviewJob : public QObject {
    Q_OBJECT
public:
    explicit NativeFeePreviewJob(NativeFeePreviewController *controller, QObject *parent = nullptr);
    ~NativeFeePreviewJob() override;
    bool start(const QString &program, const NativeFeePreviewReceiver::Expected &expected, int timeoutMs = 30000);
    // Session is caller-provisioned, never peer data. The native helper checks
    // ownership/mode and locks the file; this class never reads its credentials.
    bool startWithSession(const QString &program, const QString &sessionFile,
        const NativeFeePreviewReceiver::Expected &expected, int timeoutMs = 30000);
    // Application integration: shares the owner's pending-operation accounting
    // with wallet control. Failed/unverified construction requires recovery.
    bool startWithOwner(const QString &program, SwapWalletRpcProcess *owner,
        const NativeFeePreviewReceiver::Expected &expected, int timeoutMs = 30000);
    void cancel();
    bool isActive() const { return !m_done; }
signals:
    void completed(bool accepted);
private:
    bool startImpl(const QString &program, const QString &sessionFile,
        const NativeFeePreviewReceiver::Expected &expected, int timeoutMs, SwapWalletRpcProcess *owner = nullptr);
    void drain();
    void finish(bool success);
    QPointer<NativeFeePreviewController> m_controller;
    QPointer<SwapWalletRpcProcess> m_owner;
    QProcess *m_process;
    QTimer *m_timeout;
    QByteArray m_input, m_output;
    QString m_id;
    QString m_operationToken;
    qint64 m_stderrBytes = 0;
    bool m_done = true;
    bool m_finishing = false;
    bool m_cancelRequested = false;
};
#endif
