#ifndef MONZERO_RELAY_SENDER_H
#define MONZERO_RELAY_SENDER_H
#include "RelayOutboxStore.h"
#include <QObject>
#include <QPointer>
#include <QTcpSocket>
#include <QTimer>

// One outstanding delivery on a dedicated socket. Dependencies outlive sender
// and share its thread. Acknowledgement is not settlement evidence.
class RelaySender final : public QObject
{
    Q_OBJECT
public:
    RelaySender(QTcpSocket &socket, RelayOutboxStore &outbox,
                const RelayIngress::Coordinator &codec, QByteArray receiver,
                QObject *parent = nullptr);
    bool send(quint64 sequence);
    bool failed() const { return m_failed; }
    bool busy() const { return m_expected.accepted(); }
signals:
    void acknowledged(quint64 sequence, const QByteArray &receipt);
    void rejected();
private:
    void read();
    void reject();
    QPointer<QTcpSocket> m_socket;
    RelayOutboxStore &m_outbox;
    const RelayIngress::Coordinator &m_codec;
    QByteArray m_receiver, m_pending;
    RelayEnvelope::Evidence m_expected;
    QTimer m_deadline;
    bool m_failed = false;
};
#endif
