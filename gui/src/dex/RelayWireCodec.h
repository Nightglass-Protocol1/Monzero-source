#ifndef MONZERO_RELAY_WIRE_CODEC_H
#define MONZERO_RELAY_WIRE_CODEC_H

#include "RelayEnvelope.h"
#include <QVector>

namespace RelayWireCodec {
constexpr int maximumFrameSize = 1024;

QByteArray encode(const RelayEnvelope::Evidence &evidence);
RelayEnvelope::Evidence decode(const QByteArray &frame,
    const QString &expectedProtocol, const QString &expectedNetwork,
    qint64 now);

// One decoder per connection. Failure is terminal; discard the connection.
// Outputs are authenticated wrappers, not replay-checked market operations.
class StreamDecoder
{
public:
    static constexpr int maximumChunkSize = 16 * maximumFrameSize;
    StreamDecoder(const QString &protocol, const QString &network)
        : m_protocol(protocol), m_network(network) {}
    bool feed(const QByteArray &chunk, qint64 now,
              QVector<RelayEnvelope::Evidence> &output);
    bool finish();
    bool failed() const { return m_failed; }
    int bufferedBytes() const { return m_pending.size(); }
private:
    bool fail();
    QString m_protocol, m_network;
    QByteArray m_pending;
    bool m_failed = false;
    bool m_finished = false;
};
}

#endif
