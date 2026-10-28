#ifndef MONZERO_BALANCE_RESERVATION_STORE_H
#define MONZERO_BALANCE_RESERVATION_STORE_H

#include <QJsonArray>
#include <QLockFile>
#include <QString>
#include <memory>

class BalanceReservationStore final
{
public:
    explicit BalanceReservationStore(QString path);
    bool ready() const;
    QString error() const;
    bool reserve(const QString &id, const QString &amountAtomic, const QString &unlockedAtomic);
    bool commit(const QString &id);
    bool release(const QString &id);
    bool finalize(const QString &id, const QString &outcome, const QString &evidenceId);
    QString heldAtomic() const;
    QJsonArray records() const;

private:
    bool load();
    bool save(const QJsonArray &next);
    static bool atomic(const QString &text, quint64 &value, bool allowZero = false);
    static bool validId(const QString &id);
    QString m_path;
    QString m_error;
    QJsonArray m_records;
    std::unique_ptr<QLockFile> m_lock;
};

#endif
