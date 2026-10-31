#include "PrivateQuoteChannel.h"
#include <QCryptographicHash>
#include <QPointer>
#include <QSslKey>

namespace { int limit(PrivateQuoteChannel::Profile profile) {
    return profile==PrivateQuoteChannel::Profile::Quotes?98304:
        profile==PrivateQuoteChannel::Profile::LitecoinNegotiation?410000:0;
} }

PrivateQuoteChannel::PrivateQuoteChannel(QSslConfiguration configuration,
    QByteArray peerPin, QObject *parent, Profile profile)
    : QObject(parent), m_profile(profile), m_maximumMessage(limit(profile)), m_pin(std::move(peerPin))
{
    configuration.setProtocol(QSsl::TlsV1_2OrLater);
    configuration.setPeerVerifyMode(QSslSocket::VerifyPeer);
    m_socket.setSslConfiguration(configuration);
    m_socket.setReadBufferSize(m_maximumMessage + 4);
    m_deadline.setSingleShot(true);
    m_deadline.setInterval(10000);
    connect(&m_deadline, &QTimer::timeout, this, &PrivateQuoteChannel::reject);
    connect(&m_socket, QOverload<const QList<QSslError> &>::of(&QSslSocket::sslErrors),
            this, [this](const QList<QSslError> &) { reject(); });
    connect(&m_socket, &QSslSocket::encrypted, this, [this] {
        if (m_failed || m_socket.peerCertificate().isNull()
            || m_socket.peerCertificate().digest(QCryptographicHash::Sha256) != m_pin) {
            reject(); return;
        }
        m_deadline.stop();
        m_ready = true;
        m_rateWindow.start(); m_messages = 0;
        QPointer<PrivateQuoteChannel> guard(this);
        emit authenticated();
        if (guard) read();
    });
    connect(&m_socket, &QSslSocket::readyRead, this, &PrivateQuoteChannel::read);
    connect(&m_socket, &QSslSocket::disconnected, this, [this] {
        m_ready = false;
        if (!m_pending.isEmpty()) { reject(); return; }
        m_deadline.stop();
        emit closed();
    });
}

bool PrivateQuoteChannel::connectToPeer(const QString &host, quint16 port, const QString &peerName)
{
    if (!m_maximumMessage || m_failed || m_ready || m_socket.state() != QAbstractSocket::UnconnectedState
        || host.isEmpty() || !port || peerName.isEmpty() || m_pin.size() != 32
        || m_pin == QByteArray(32, '\0') || !QSslSocket::supportsSsl()) return false;
    m_deadline.start();
    m_socket.connectToHostEncrypted(host, port, peerName);
    return true;
}

bool PrivateQuoteChannel::hasWriteCapacity(qint64 plaintextBytes, qint64 encryptedBytes, int messageBytes, Profile profile)
{
    const int maximumMessage=limit(profile);
    if (plaintextBytes < 0 || encryptedBytes < 0 || messageBytes < 1
        || messageBytes > maximumMessage) return false;
    const qint64 available = 2 * maximumMessage - messageBytes - 4;
    // Subtraction avoids overflow even with anomalously large queue counts.
    return plaintextBytes <= available && encryptedBytes <= available - plaintextBytes;
}

bool PrivateQuoteChannel::send(const QByteArray &message)
{
    if (!ready() || !m_socket.isEncrypted()
        || !hasWriteCapacity(m_socket.bytesToWrite(), m_socket.encryptedBytesToWrite(), message.size(), m_profile)) return false;
    QByteArray frame;
    for (int shift = 24; shift >= 0; shift -= 8) frame.append(char((quint32(message.size()) >> shift) & 255));
    frame += message;
    if (m_socket.write(frame) != frame.size()) { reject(); return false; }
    return true;
}

bool PrivateQuoteChannel::acceptPeer(qintptr socketDescriptor)
{
    if (!m_maximumMessage || m_failed || m_ready || m_socket.state() != QAbstractSocket::UnconnectedState
        || m_pin.size() != 32 || m_pin == QByteArray(32, '\0') || !QSslSocket::supportsSsl()
        || m_socket.localCertificate().isNull() || m_socket.privateKey().isNull()) return false;
    if (!m_socket.setSocketDescriptor(socketDescriptor)) return false;
    m_deadline.start();
    m_socket.startServerEncryption();
    return true;
}

void PrivateQuoteChannel::reject()
{
    if (m_failed) return;
    m_failed = true; m_ready = false;
    m_deadline.stop(); m_pending.clear();
    QPointer<PrivateQuoteChannel> guard(this);
    m_socket.abort();
    if (guard) emit rejected();
}

void PrivateQuoteChannel::close()
{
    m_failed = true; m_ready = false;
    m_deadline.stop(); m_pending.clear();
    m_socket.abort();
}

void PrivateQuoteChannel::read()
{
    if (!ready()) return;
    int processed = 0;
    while (m_socket.bytesAvailable() && processed < 8) {
        if (m_pending.isEmpty()) m_deadline.start();
        int target = 4;
        if (m_pending.size() >= 4) {
            quint32 length = 0;
            for (int i = 0; i < 4; ++i) length = (length << 8) | static_cast<unsigned char>(m_pending.at(i));
            if (!length || length > quint32(m_maximumMessage)) { reject(); return; }
            target += int(length);
        }
        m_pending += m_socket.read(target - m_pending.size());
        if (m_pending.size() < target) return;
        if (target == 4) {
            quint32 length = 0;
            for (char byte : m_pending) length = (length << 8) | static_cast<unsigned char>(byte);
            if (!length || length > quint32(m_maximumMessage)) { reject(); return; }
            continue;
        }
        const auto message = m_pending.mid(4);
        m_pending.clear(); m_deadline.stop(); ++processed;
        if (m_rateWindow.elapsed() >= 1000) { m_rateWindow.restart(); m_messages = 0; }
        if (++m_messages > 64) { reject(); return; }
        QPointer<PrivateQuoteChannel> guard(this);
        emit received(message);
        if (!guard || !ready()) return;
    }
    if (m_socket.bytesAvailable()) QTimer::singleShot(0, this, &PrivateQuoteChannel::read);
}
