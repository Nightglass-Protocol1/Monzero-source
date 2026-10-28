#include "PrivateQuoteServer.h"
#include <QSslKey>

PrivateQuoteServer::PrivateQuoteServer(QSslConfiguration configuration, QByteArray peerPin,
    QObject *parent) : QTcpServer(parent), m_configuration(std::move(configuration)), m_pin(std::move(peerPin))
{
    setMaxPendingConnections(8);
}
PrivateQuoteServer::~PrivateQuoteServer() { stop(); }

bool PrivateQuoteServer::start(const QHostAddress &address, quint16 port)
{
    if (isListening() || address.isNull() || m_pin.size() != 32 || m_pin == QByteArray(32, '\0')
        || m_configuration.localCertificate().isNull() || m_configuration.privateKey().isNull()
        || !QSslSocket::supportsSsl()) return false;
    return listen(address, port);
}

void PrivateQuoteServer::stop()
{
    QTcpServer::close();
    const auto peers = m_peers;
    m_peers.clear();
    for (auto *peer : peers) { disconnect(peer, nullptr, this, nullptr); peer->close(); peer->deleteLater(); }
}

void PrivateQuoteServer::incomingConnection(qintptr descriptor)
{
    if (m_peers.size() >= 8) {
        QTcpSocket socket; socket.setSocketDescriptor(descriptor); socket.abort(); return;
    }
    auto *channel = new PrivateQuoteChannel(m_configuration, m_pin, this);
    if (!channel->acceptPeer(descriptor)) {
        QTcpSocket socket; socket.setSocketDescriptor(descriptor); socket.abort(); delete channel; return;
    }
    int sameAddress = 0;
    for (const auto *peer : m_peers)
        if (peer->peerAddress().isEqual(channel->peerAddress(), QHostAddress::ConvertV4MappedToIPv4)) ++sameAddress;
    if (sameAddress >= 2) { channel->close(); channel->deleteLater(); return; }
    m_peers.insert(channel);
    const auto release = [this, channel] { m_peers.remove(channel); channel->deleteLater(); };
    connect(channel, &PrivateQuoteChannel::closed, this, release);
    connect(channel, &PrivateQuoteChannel::rejected, this, release);
    connect(channel, &PrivateQuoteChannel::authenticated, this, [this, channel] { emit authenticated(channel); });
    auto *idle = new QTimer(channel);
    idle->setSingleShot(true); idle->setInterval(60000);
    connect(idle, &QTimer::timeout, channel, &PrivateQuoteChannel::close);
    connect(channel, &PrivateQuoteChannel::received, idle, [idle](const QByteArray &) { idle->start(); });
    idle->start();
}
