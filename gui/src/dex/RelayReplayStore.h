#ifndef MONZERO_RELAY_REPLAY_STORE_H
#define MONZERO_RELAY_REPLAY_STORE_H

#include "RelayEnvelope.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QLockFile>
#include <QString>
#include <memory>

namespace RelayIngress { class Coordinator; }

class RelayReplayStore final
{
public:
    RelayReplayStore(QString path, QString expectedProtocol,
                     QString expectedNetwork);
    bool ready() const;
    QString error() const;
    bool accept(const RelayEnvelope::Evidence &evidence, qint64 now);
    quint64 lastSequence(const QByteArray &senderPublicKey) const;
    QJsonArray records() const;
    RelayEnvelope::Evidence pendingEnvelope(const QByteArray &senderPublicKey) const;

private:
    friend class RelayIngress::Coordinator;
    bool prepare(const RelayEnvelope::Evidence &evidence, qint64 now,
                 const QJsonObject &artifact);
    QJsonObject pendingArtifact(const QByteArray &sender) const;
    bool pendingRecovery(const RelayEnvelope::Evidence &evidence, qint64 now) const;
    bool complete(const RelayEnvelope::Evidence &evidence);
    bool record(const RelayEnvelope::Evidence &evidence, qint64 now, bool pending,
                const QJsonObject &artifact = {});
    bool load();
    RelayEnvelope::Evidence decodeEnvelope(const QJsonObject &record) const;
    bool save(const QJsonArray &next);

    QString m_path;
    QString m_expectedProtocol;
    QString m_expectedNetwork;
    QString m_error;
    QJsonArray m_records;
    std::unique_ptr<QLockFile> m_lock;
};

#endif
