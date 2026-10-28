#ifndef MONZERO_CONFIRMATION_TRACKER_H
#define MONZERO_CONFIRMATION_TRACKER_H

#include <QJsonArray>
#include <QLockFile>
#include <QVariantMap>
#include <memory>

class ConfirmationTracker final
{
public:
    explicit ConfirmationTracker(QString path);
    bool ready() const;
    QString error() const;
    QVariantMap observe(const QString &chain, const QString &transactionId, bool found,
                        const QString &blockHash, const QString &blockHeight,
                        const QString &confirmations, const QString &nodeHeight,
                        int requiredConfirmations);
    QVariantMap current(const QString &chain, const QString &transactionId) const;
    QJsonArray records() const;

private:
    bool load();
    bool save(const QJsonArray &next);
    static bool unsignedInteger(const QString &text, quint64 &value);
    static bool validChain(const QString &chain);
    static bool validHash(const QString &hash);
    QString m_path;
    QString m_error;
    QJsonArray m_records;
    std::unique_ptr<QLockFile> m_lock;
};

#endif
