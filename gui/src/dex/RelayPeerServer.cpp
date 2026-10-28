#include "RelayPeerServer.h"

RelayPeerServer::RelayPeerServer(RelayIngress::Coordinator &ingress, QObject *parent)
    : QObject(parent), m_ingress(ingress)
{
    m_server.setMaxPendingConnections(maximumPeers);
    connect(&m_server, &QTcpServer::newConnection, this, &RelayPeerServer::accept);
}

RelayPeerServer::~RelayPeerServer() { stop(); }

RelayPeerServer::RelayPeerServer(RelayIngress::Coordinator &ingress,
    const OfferIdentity &receiver, QObject *parent)
    : RelayPeerServer(ingress, parent)
{
    m_receiver = &receiver;
}

bool RelayPeerServer::listen(const QHostAddress &address, quint16 port)
{
    if (address.isNull() || m_server.isListening()) return false;
    return m_server.listen(address, port);
}

void RelayPeerServer::stop()
{
    m_server.close();
    while (m_server.hasPendingConnections()) {
        auto *socket = m_server.nextPendingConnection();
        socket->abort();
        delete socket;
    }
    const auto peers = m_peers;
    m_peers.clear();
    for (auto *socket : peers) {
        disconnect(socket, nullptr, this, nullptr);
        socket->abort();
        socket->deleteLater();
    }
}

void RelayPeerServer::accept()
{
    while (m_server.hasPendingConnections()) {
        auto *socket = m_server.nextPendingConnection();
        int sameAddress = 0;
        for (const auto *peer : m_peers)
            if (peer->peerAddress().isEqual(socket->peerAddress(), QHostAddress::ConvertV4MappedToIPv4))
                ++sameAddress;
        if (socket->state() != QAbstractSocket::ConnectedState
            || m_peers.size() >= maximumPeers || sameAddress >= maximumPeersPerAddress) {
            socket->abort();
            socket->deleteLater();
            continue;
        }
        socket->setParent(this);
        m_peers.insert(socket);
        connect(socket, &QTcpSocket::disconnected, this, [this, socket] {
            m_peers.remove(socket);
            socket->deleteLater();
        });
        auto *connection = m_receiver
            ? new RelayPeerConnection(*socket, m_ingress, *m_receiver, socket)
            : new RelayPeerConnection(*socket, m_ingress, socket);
        auto *idle = new QTimer(socket);
        idle->setSingleShot(true);
        idle->setInterval(60000);
        connect(idle, &QTimer::timeout, socket, &QTcpSocket::abort);
        connect(connection, &RelayPeerConnection::applied, this,
            [this, idle](const QVariantMap &evidence) {
                idle->start();
                emit applied(evidence);
            });
        idle->start();
    }
}
