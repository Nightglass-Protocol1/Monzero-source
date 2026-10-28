#include "RelaySender.h"
#include "RelayReceipt.h"
#include "RelayPeerConnection.h"
#include <QDateTime>

RelaySender::RelaySender(QTcpSocket &socket, RelayOutboxStore &outbox,
    const RelayIngress::Coordinator &codec, QByteArray receiver, QObject *parent)
    : QObject(parent), m_socket(&socket), m_outbox(outbox), m_codec(codec),
      m_receiver(std::move(receiver))
{
    socket.setReadBufferSize(2052);
    m_deadline.setSingleShot(true);
    m_deadline.setInterval(10000);
    connect(&m_deadline, &QTimer::timeout, this, &RelaySender::reject);
    connect(&socket, &QTcpSocket::readyRead, this, &RelaySender::read);
    connect(&socket, &QTcpSocket::disconnected, this, [this] {
        if (busy() || !m_pending.isEmpty()) reject();
    });
    QTimer::singleShot(0, this, &RelaySender::read);
}

bool RelaySender::send(quint64 sequence)
{
    if (m_failed || busy() || !m_socket || !m_outbox.ready()
        || m_receiver.size() != 32 || m_receiver == QByteArray(32, '\0')
        || m_socket->state() != QAbstractSocket::ConnectedState
        || m_socket->bytesToWrite() != 0) return false;
    const auto message = m_outbox.message(sequence);
    const auto envelope = m_codec.inspectMessage(message, QDateTime::currentSecsSinceEpoch());
    if (!envelope.accepted() || envelope.terms().sequence != sequence) return false;
    const auto framed = RelayPeerConnection::frame(message);
    m_expected = envelope;
    m_deadline.start();
    if (m_socket->write(framed) != framed.size()) { reject(); return false; }
    return true;
}

void RelaySender::reject()
{
    if (m_failed) return;
    m_failed = true;
    m_deadline.stop();
    m_pending.clear();
    m_expected = {};
    if (m_socket) m_socket->abort();
    emit rejected();
}

void RelaySender::read()
{
    if (m_failed || !m_socket || !m_socket->bytesAvailable()) return;
    if (!busy()) { reject(); return; }
    // Fixed total deadline from send; incoming fragments cannot extend it.
    while (m_socket->bytesAvailable()) {
        int target = 4;
        if (m_pending.size() >= 4) {
            quint32 size = 0;
            for (int i = 0; i < 4; ++i)
                size = (size << 8) | static_cast<unsigned char>(m_pending.at(i));
            if (!size || size > 2048) { reject(); return; }
            target += int(size);
        }
        m_pending += m_socket->read(target - m_pending.size());
        if (m_pending.size() < target) return;
        if (target == 4) {
            quint32 size = 0;
            for (char byte : m_pending) size = (size << 8) | static_cast<unsigned char>(byte);
            if (!size || size > 2048) { reject(); return; }
            continue;
        }
        const auto receipt = m_pending.mid(4);
        if (!RelayReceipt::verify(receipt, m_expected, m_receiver,
                                  QDateTime::currentSecsSinceEpoch())
            || m_socket->bytesAvailable()) { reject(); return; }
        const auto sequence = m_expected.terms().sequence;
        if (!m_outbox.acknowledge(sequence, m_receiver, receipt,
                                  QDateTime::currentSecsSinceEpoch())) { reject(); return; }
        m_pending.clear();
        m_expected = {};
        m_deadline.stop();
        // No member access after emitting: a subscriber may delete this object.
        emit acknowledged(sequence, receipt);
        return;
    }
}
