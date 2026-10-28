#include "RelayPeerConnection.h"
#include "RelayReceipt.h"
#include <QDateTime>

namespace {
constexpr int headerSize = 4;
constexpr int queueLimit = 65536;
}

RelayPeerConnection::RelayPeerConnection(QTcpSocket &socket,
    RelayIngress::Coordinator &ingress, QObject *parent)
    : QObject(parent), m_socket(&socket), m_ingress(ingress)
{
    socket.setReadBufferSize(RelayIngress::Coordinator::maximumMessageSize + headerSize);
    m_deadline.setSingleShot(true);
    m_deadline.setInterval(10000);
    m_rateWindow.start();
    connect(&m_deadline, &QTimer::timeout, this, &RelayPeerConnection::reject);
    connect(&socket, &QTcpSocket::readyRead, this, &RelayPeerConnection::read);
    connect(&socket, &QTcpSocket::disconnected, this, [this] {
        if (!m_pending.isEmpty()) reject();
    });
    // Also consume bytes that arrived before this handler was attached.
    QTimer::singleShot(0, this, &RelayPeerConnection::read);
}

RelayPeerConnection::RelayPeerConnection(QTcpSocket &socket,
    RelayIngress::Coordinator &ingress, const OfferIdentity &receiver, QObject *parent)
    : RelayPeerConnection(socket, ingress, parent)
{
    m_receiver = &receiver;
}

QByteArray RelayPeerConnection::frame(const QByteArray &message)
{
    if (message.isEmpty() || message.size() > RelayIngress::Coordinator::maximumMessageSize)
        return {};
    QByteArray result;
    const quint32 size = quint32(message.size());
    for (int shift = 24; shift >= 0; shift -= 8) result.append(char((size >> shift) & 255));
    return result + message;
}

bool RelayPeerConnection::send(const QByteArray &message)
{
    const auto bytes = frame(message);
    if (m_failed || !m_socket || bytes.isEmpty()
        || m_socket->state() != QAbstractSocket::ConnectedState
        || m_socket->bytesToWrite() > queueLimit - bytes.size()) return false;
    if (m_socket->write(bytes) != bytes.size()) { reject(); return false; }
    return true;
}

void RelayPeerConnection::reject()
{
    if (m_failed) return;
    m_failed = true;
    m_deadline.stop();
    m_pending.clear();
    if (m_socket) m_socket->abort();
    emit rejected();
}

void RelayPeerConnection::read()
{
    if (m_failed || !m_socket) return;
    int processed = 0;
    while (m_socket->bytesAvailable() > 0 && processed < 8) {
        if (m_pending.isEmpty()) m_deadline.start();
        int target = headerSize;
        if (m_pending.size() >= headerSize) {
            quint32 size = 0;
            for (int i = 0; i < headerSize; ++i)
                size = (size << 8) | static_cast<unsigned char>(m_pending.at(i));
            if (!size || size > quint32(RelayIngress::Coordinator::maximumMessageSize)) {
                reject(); return;
            }
            target += int(size);
        }
        m_pending += m_socket->read(target - m_pending.size());
        if (m_pending.size() < target) return;
        if (target == headerSize) {
            // Validate the length even when no body bytes have arrived yet.
            quint32 size = 0;
            for (const char byte : m_pending) size = (size << 8) | static_cast<unsigned char>(byte);
            if (!size || size > quint32(RelayIngress::Coordinator::maximumMessageSize)) {
                reject(); return;
            }
            continue;
        }
        m_deadline.stop();
        if (m_rateWindow.elapsed() >= 1000) { m_rateWindow.restart(); m_messages = 0; }
        if (++m_messages > 64) { reject(); return; }
        const auto now = QDateTime::currentSecsSinceEpoch();
        const auto message = m_pending.mid(headerSize);
        const auto envelope = m_receiver ? m_ingress.inspectMessage(message, now)
                                         : RelayEnvelope::Evidence{};
        const auto result = m_ingress.ingestMessage(message, now);
        m_pending.clear();
        if (!result.accepted()) { reject(); return; }
        // Persist first. Failed receipt delivery must never roll back acceptance.
        // Closing on backpressure lets an exact retry obtain a fresh receipt.
        if (m_receiver && !send(RelayReceipt::create(result, envelope, *m_receiver, now))) {
            reject(); return;
        }
        ++processed;
        QPointer<RelayPeerConnection> guard(this);
        emit applied(result.details());
        if (!guard) return;
        if (m_failed || !m_socket) return;
    }
    if (m_socket->bytesAvailable() > 0 && !m_scheduled) {
        m_scheduled = true;
        QTimer::singleShot(0, this, [this] { m_scheduled = false; read(); });
    }
}
