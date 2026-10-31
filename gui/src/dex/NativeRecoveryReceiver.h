#ifndef MONZERO_NATIVE_RECOVERY_RECEIVER_H
#define MONZERO_NATIVE_RECOVERY_RECEIVER_H
#include <QByteArray>
#include <QDeadlineTimer>
#include <QString>
#include <QVariantMap>

// Display evidence only, not transport authentication or retry authority.
// Expected values must come from trusted local trade state, never a peer reply.
class NativeRecoveryReceiver {
public:
    struct Expected { QString genesis, wallet, lockTxid, txid, minimumConfirmations; };
    QString begin(const Expected &expected, int timeoutMs = 30000);
    QVariantMap accept(const QByteArray &json);
    QVariantMap current() const;
    void cancel();
private:
    Expected m_expected;
    QString m_id;
    QDeadlineTimer m_deadline;
    bool m_pending = false;
    QVariantMap m_current;
};
#endif
