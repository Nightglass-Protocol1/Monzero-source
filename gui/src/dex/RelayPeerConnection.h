#ifndef MONZERO_RELAY_PEER_CONNECTION_H
#define MONZERO_RELAY_PEER_CONNECTION_H

#include "RelayIngress.h"
#include <QObject>
#include <QPointer>
#include <QTcpSocket>
#include <QTimer>
#include <QElapsedTimer>

// Opt-in transport for public market artifacts only; never wallet secrets.
// Socket and coordinator must be on this object's thread. Coordinator outlives it.
class RelayPeerConnection final : public QObject
{
    Q_OBJECT
public:
    RelayPeerConnection(QTcpSocket &socket, RelayIngress::Coordinator &ingress,
                        QObject *parent = nullptr);
    // Receiver identity must outlive this connection. Opt-in signed receipts.
    RelayPeerConnection(QTcpSocket &socket, RelayIngress::Coordinator &ingress,
                        const OfferIdentity &receiver, QObject *parent = nullptr);
    static QByteArray frame(const QByteArray &message);
    bool send(const QByteArray &message);
    bool failed() const { return m_failed; }
signals:
    void applied(const QVariantMap &evidence);
    void rejected();
private:
    void read();
    void reject();
    QPointer<QTcpSocket> m_socket;
    RelayIngress::Coordinator &m_ingress;
    const OfferIdentity *m_receiver = nullptr;
    QByteArray m_pending;
    QTimer m_deadline;
    QElapsedTimer m_rateWindow;
    int m_messages = 0;
    bool m_failed = false;
    bool m_scheduled = false;
};
#endif
