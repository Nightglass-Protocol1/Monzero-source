#ifndef MONZERO_RELAY_OUTBOX_STORE_H
#define MONZERO_RELAY_OUTBOX_STORE_H

#include "RelayIngress.h"

// Append before sending. Signed delivery receipts are retained with history.
// The codec must outlive this store; retain the file with its signing identity.
class RelayOutboxStore final
{
public:
    enum class Mode { CreateNew, OpenExisting };
    RelayOutboxStore(QString path, QByteArray sender,
                     const RelayIngress::Coordinator &codec, Mode mode);
    bool ready() const { return m_error.isEmpty(); }
    QString error() const { return m_error; }
    quint64 nextSequence() const;
    bool append(const QByteArray &message, qint64 now);
    QByteArray message(quint64 sequence) const;
    bool acknowledge(quint64 sequence, const QByteArray &receiver,
                     const QByteArray &receipt, qint64 now);
    bool acknowledged(quint64 sequence, const QByteArray &receiver, qint64 now) const;
private:
    RelayEnvelope::Evidence historicalEnvelope(quint64 sequence) const;
    bool save(const QJsonArray &records);
    QString m_path, m_error;
    QByteArray m_sender;
    const RelayIngress::Coordinator &m_codec;
    std::unique_ptr<QLockFile> m_lock;
    QJsonArray m_records;
};
#endif
