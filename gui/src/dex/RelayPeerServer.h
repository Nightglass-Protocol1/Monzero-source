#ifndef MONZERO_RELAY_PEER_SERVER_H
#define MONZERO_RELAY_PEER_SERVER_H

#include "RelayPeerConnection.h"
#include <QTcpServer>
#include <QSet>

// Explicitly started research listener. Coordinator must outlive the server.
class RelayPeerServer final : public QObject
{
    Q_OBJECT
public:
    static constexpr int maximumPeers = 8;
    static constexpr int maximumPeersPerAddress = 2;
    explicit RelayPeerServer(RelayIngress::Coordinator &ingress, QObject *parent = nullptr);
    // Identity is provisioned externally and must outlive the server.
    RelayPeerServer(RelayIngress::Coordinator &ingress, const OfferIdentity &receiver,
                    QObject *parent = nullptr);
    ~RelayPeerServer() override;
    bool listen(const QHostAddress &address, quint16 port);
    void stop();
    quint16 port() const { return m_server.serverPort(); }
    int peerCount() const { return m_peers.size(); }
signals:
    void applied(const QVariantMap &evidence);
private:
    void accept();
    QTcpServer m_server;
    RelayIngress::Coordinator &m_ingress;
    const OfferIdentity *m_receiver = nullptr;
    QSet<QTcpSocket *> m_peers;
};
#endif
