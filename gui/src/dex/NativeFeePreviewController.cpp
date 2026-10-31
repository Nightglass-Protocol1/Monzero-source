#include "NativeFeePreviewController.h"
#include <QTimer>

NativeFeePreviewController::NativeFeePreviewController(QObject *parent)
    : QObject(parent), m_expiry(new QTimer(this)) {
    m_expiry->setSingleShot(true);
    m_expiry->setTimerType(Qt::PreciseTimer);
    connect(m_expiry, &QTimer::timeout, this, [this] {
        m_receiver.cancel();
        m_activeRequestId.clear();
        emit previewChanged();
    });
}

QString NativeFeePreviewController::begin(const NativeFeePreviewReceiver::Expected &expected, int timeoutMs) {
    m_expiry->stop();
    const auto id = m_receiver.begin(expected, timeoutMs);
    m_activeRequestId = id;
    if (!id.isEmpty()) m_expiry->start(timeoutMs);
    emit previewChanged();
    return id;
}

QVariantMap NativeFeePreviewController::accept(const QString &requestId, const QByteArray &json) {
    if (requestId.isEmpty() || requestId != m_activeRequestId)
        return {{"valid",false},{"executable",false},{"transportAuthenticated",false},
            {"error","Obsolete fee preview callback"}};
    const auto result = m_receiver.accept(json);
    // Receipt never restarts the original request deadline.
    if (!result.value("valid").toBool()) {
        m_expiry->stop();
        m_activeRequestId.clear();
    }
    emit previewChanged();
    return result;
}

void NativeFeePreviewController::cancel() {
    m_activeRequestId.clear();
    m_expiry->stop();
    m_receiver.cancel();
    emit previewChanged();
}

bool NativeFeePreviewController::cancelRequest(const QString &requestId) {
    if (requestId.isEmpty() || requestId != m_activeRequestId) return false;
    cancel();
    return true;
}
