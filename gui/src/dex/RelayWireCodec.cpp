#include "RelayWireCodec.h"

#include <QDataStream>

namespace {
const QByteArray magic("MONZERO-DEX-RELAY-WIRE-V1\n");
constexpr quint32 maximumTextField = 64;

void appendField(QByteArray &output, const QByteArray &value)
{
    QDataStream stream(&output, QIODevice::Append);
    stream.setByteOrder(QDataStream::BigEndian);
    stream << quint32(value.size());
    if (stream.status() == QDataStream::Ok) output += value;
}

bool readField(QDataStream &stream, QIODevice &device, quint32 maximum,
               QByteArray &value)
{
    quint32 size = 0;
    stream >> size;
    if (stream.status() != QDataStream::Ok || size > maximum
        || size > quint32(device.bytesAvailable()))
        return false;
    value = device.read(size);
    return value.size() == int(size);
}
}

namespace RelayWireCodec {
bool StreamDecoder::fail()
{
    m_pending.clear();
    m_failed = true;
    return false;
}

bool StreamDecoder::finish()
{
    if (m_failed || !m_pending.isEmpty()) return fail();
    m_finished = true;
    return true;
}

bool StreamDecoder::feed(const QByteArray &chunk, qint64 now,
                        QVector<RelayEnvelope::Evidence> &output)
{
    output.clear();
    if (m_failed || m_finished || chunk.size() > maximumChunkSize) return fail();
    QVector<RelayEnvelope::Evidence> decoded;
    const int headerSize = magic.size() + int(sizeof(quint32));
    for (const char byte : chunk) {
        m_pending.append(byte);
        if (m_pending.size() <= magic.size()) {
            if (byte != magic.at(m_pending.size() - 1)) return fail();
            continue;
        }
        if (m_pending.size() < headerSize) continue;
        quint32 bodySize = 0;
        for (int i = magic.size(); i < headerSize; ++i)
            bodySize = (bodySize << 8) | static_cast<unsigned char>(m_pending.at(i));
        if (bodySize == 0 || bodySize > quint32(maximumFrameSize - headerSize))
            return fail();
        if (m_pending.size() < headerSize + int(bodySize)) continue;
        const auto evidence = decode(m_pending, m_protocol, m_network, now);
        if (!evidence.accepted()) return fail();
        decoded.append(evidence);
        m_pending.clear();
    }
    output = decoded;
    return true;
}

QByteArray encode(const RelayEnvelope::Evidence &evidence)
{
    if (!evidence.accepted()) return {};
    const auto terms = evidence.terms();
    QByteArray body;
    appendField(body, terms.protocol.toUtf8());
    appendField(body, terms.network.toUtf8());
    appendField(body, terms.type.toUtf8());
    {
        QDataStream stream(&body, QIODevice::Append);
        stream.setByteOrder(QDataStream::BigEndian);
        stream << terms.sequence << terms.createdAt << terms.expiresAt;
    }
    body += terms.artifact;
    body += evidence.senderPublicKey();
    body += evidence.signature();

    const qint64 totalSize = magic.size() + qint64(sizeof(quint32)) + body.size();
    if (body.isEmpty() || totalSize > maximumFrameSize) return {};
    QByteArray frame = magic;
    QDataStream stream(&frame, QIODevice::Append);
    stream.setByteOrder(QDataStream::BigEndian);
    stream << quint32(body.size());
    frame += body;
    return frame;
}

RelayEnvelope::Evidence decode(const QByteArray &frame,
    const QString &expectedProtocol, const QString &expectedNetwork,
    qint64 now)
{
    if (frame.size() <= magic.size() + int(sizeof(quint32))
        || frame.size() > maximumFrameSize || !frame.startsWith(magic))
        return {};

    QByteArray framedBody = frame.mid(magic.size());
    QDataStream frameStream(&framedBody, QIODevice::ReadOnly);
    frameStream.setByteOrder(QDataStream::BigEndian);
    quint32 bodySize = 0;
    frameStream >> bodySize;
    if (frameStream.status() != QDataStream::Ok
        || bodySize != quint32(frameStream.device()->bytesAvailable()))
        return {};
    QByteArray body = frameStream.device()->read(bodySize);
    QDataStream stream(&body, QIODevice::ReadOnly);
    stream.setByteOrder(QDataStream::BigEndian);

    QByteArray protocol, network, type;
    if (!readField(stream, *stream.device(), maximumTextField, protocol)
        || !readField(stream, *stream.device(), maximumTextField, network)
        || !readField(stream, *stream.device(), maximumTextField, type))
        return {};

    RelayEnvelope::Terms terms;
    terms.protocol = QString::fromUtf8(protocol);
    terms.network = QString::fromUtf8(network);
    terms.type = QString::fromUtf8(type);
    stream >> terms.sequence >> terms.createdAt >> terms.expiresAt;
    if (stream.status() != QDataStream::Ok
        || stream.device()->bytesAvailable() != 32 + 32 + 64)
        return {};
    terms.artifact = stream.device()->read(32);
    const QByteArray sender = stream.device()->read(32);
    const QByteArray signature = stream.device()->read(64);
    if (stream.device()->bytesAvailable() != 0) return {};

    const auto evidence = RelayEnvelope::verify(terms, sender, signature,
        expectedProtocol, expectedNetwork, now);
    if (!evidence.accepted() || encode(evidence) != frame) return {};
    return evidence;
}
}
