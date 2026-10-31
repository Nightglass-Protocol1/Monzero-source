#ifndef MONZERO_NATIVE_FEE_PREVIEW_CONTROLLER_H
#define MONZERO_NATIVE_FEE_PREVIEW_CONTROLLER_H
#include "NativeFeePreviewReceiver.h"
#include <QObject>
class QTimer;

// GUI-thread lifecycle only. Does not launch or authenticate a backend.
class NativeFeePreviewController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap preview READ preview NOTIFY previewChanged)
public:
    explicit NativeFeePreviewController(QObject *parent = nullptr);
    QString begin(const NativeFeePreviewReceiver::Expected &expected, int timeoutMs = 30000);
    // requestId must be captured by the owning job, not read from untrusted JSON.
    QVariantMap accept(const QString &requestId, const QByteArray &json);
    QVariantMap preview() const { return m_receiver.current(); }
    void cancel();
    bool cancelRequest(const QString &requestId);
    int pendingTimeMs(const QString &requestId) const {
        return !requestId.isEmpty() && requestId == m_activeRequestId ? m_receiver.pendingTimeMs() : 0;
    }
signals:
    void previewChanged();
private:
    NativeFeePreviewReceiver m_receiver;
    QString m_activeRequestId;
    QTimer *m_expiry;
};
#endif
