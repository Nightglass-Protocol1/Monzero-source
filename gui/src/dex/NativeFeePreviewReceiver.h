#ifndef MONZERO_NATIVE_FEE_PREVIEW_RECEIVER_H
#define MONZERO_NATIVE_FEE_PREVIEW_RECEIVER_H

#include <QByteArray>
#include <QDeadlineTimer>
#include <QString>
#include <QVariantMap>

// Request-scoped display decoder. Not transport authentication or trade authority.
// Must be used on one owning thread, with locally validated request parameters.
class NativeFeePreviewReceiver {
public:
    struct Expected {
        QString wallet, destination, amountAtoms, account, priority, genesis;
    };
    QString begin(const Expected &expected, int timeoutMs = 30000);
    static bool validExpected(const Expected &expected, int timeoutMs = 30000);
    QVariantMap accept(const QByteArray &json);
    // Fetch afresh before display; copied maps do not expire automatically.
    QVariantMap current() const;
    int pendingTimeMs() const { return m_pending ? int(qMax<qint64>(0,m_deadline.remainingTime())) : 0; }
    void cancel();
private:
    Expected m_expected;
    QString m_requestId;
    QDeadlineTimer m_deadline;
    bool m_pending = false;
    QVariantMap m_current;
};
#endif
