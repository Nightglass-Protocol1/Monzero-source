#ifndef MONZERO_PRIVATE_QUOTE_CHANNEL_H
#define MONZERO_PRIVATE_QUOTE_CHANNEL_H
#include <QObject>
#include <QSslSocket>
#include <QSslConfiguration>
#include <QTimer>
#include <QHostAddress>
#include <QElapsedTimer>

// Dedicated TLS client for participant messages, not a public offer relay.
// Certificate trust/name validation AND an out-of-band SHA-256 leaf pin are
// required. Received bytes still require signed-quote verification.
class PrivateQuoteChannel final : public QObject
{
    Q_OBJECT
public:
    PrivateQuoteChannel(QSslConfiguration configuration, QByteArray peerPin,
                        QObject *parent = nullptr);
    bool connectToPeer(const QString &host, quint16 port, const QString &peerName);
    // Adopts an accepted TCP descriptor. Configure a local certificate/key and
    // trusted client CA first; peerPin pins the client's leaf certificate.
    bool acceptPeer(qintptr socketDescriptor);
    bool send(const QByteArray &message);
    // Admission policy for both Qt TLS queues, excluding kernel/TLS overhead.
    static bool hasWriteCapacity(qint64 plaintextBytes, qint64 encryptedBytes, int messageBytes);
    bool ready() const { return m_ready && !m_failed; }
    bool failed() const { return m_failed; }
    QHostAddress peerAddress() const { return m_socket.peerAddress(); }
    void close();
signals:
    void authenticated();
    void received(const QByteArray &message);
    void rejected();
    void closed();
private:
    void reject();
    void read();
    QSslSocket m_socket;
    QTimer m_deadline;
    QElapsedTimer m_rateWindow;
    int m_messages = 0;
    QByteArray m_pin, m_pending;
    bool m_ready = false, m_failed = false;
};
#endif
