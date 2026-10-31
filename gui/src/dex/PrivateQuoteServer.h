#ifndef MONZERO_PRIVATE_QUOTE_SERVER_H
#define MONZERO_PRIVATE_QUOTE_SERVER_H
#include "PrivateQuoteChannel.h"
#include <QTcpServer>
#include <QSet>

// One explicitly pinned participant per listener; no discovery or public relay.
class PrivateQuoteServer final : public QTcpServer
{
    Q_OBJECT
public:
    PrivateQuoteServer(QSslConfiguration configuration, QByteArray peerPin,
                       QObject *parent = nullptr, PrivateQuoteChannel::Profile profile = PrivateQuoteChannel::Profile::Quotes);
    ~PrivateQuoteServer() override;
    bool start(const QHostAddress &address, quint16 port);
    void stop();
    int peerCount() const { return m_peers.size(); }
signals:
    void authenticated(PrivateQuoteChannel *channel);
private:
    void incomingConnection(qintptr descriptor) override;
    PrivateQuoteChannel::Profile m_profile;
    QSslConfiguration m_configuration;
    QByteArray m_pin;
    QSet<PrivateQuoteChannel *> m_peers;
};
#endif
