#ifndef MONZERO_RELAY_MAIL_TRANSPORT_H
#define MONZERO_RELAY_MAIL_TRANSPORT_H
#include <QByteArray>
#include <QHash>
#include <QMap>
#include <QObject>
#include <QSet>
#include <QStringList>
#include <QTimer>

class QNetworkAccessManager;
class QNetworkReply;

// A two-party message channel carried by the open DEX relays' mailboxes, for traders who cannot
// accept incoming connections. It stands in for the TLS socket of PrivateQuoteChannel.
//
// Both sides hold their own mailbox key (X25519) and pin the other's public key, exchanged in the
// trade's cards. Every letter is encrypted and authenticated (ChaCha20-Poly1305) under a key from
// the static-static X25519 secret and the sender's session, so only the two traders can write or
// read letters, and a relay can only delay, drop or replay them. Each channel instance has a fresh
// 16-byte session; letters carry (session, acknowledged peer session, sequence). The channel is
// authenticated once the peer acknowledged our session, delivers payloads in sequence order once,
// and reports `closed` if the peer starts a new session (it restarted), as a dropped socket would.
class RelayMailTransport final : public QObject {
    Q_OBJECT
public:
    static constexpr int maximumPayload = 410000;
    explicit RelayMailTransport(QObject *parent=nullptr);
    ~RelayMailTransport() override;
    // relays: https URLs (or http://127.0.0.1:port); ownSecret 32 bytes; peerPublic 32 bytes
    bool start(const QStringList &relays,const QByteArray &ownSecret,const QByteArray &peerPublic);
    bool send(const QByteArray &payload);
    void stop();
    bool ready() const {return m_ready && !m_stopped;}
    QByteArray ownPublic() const {return m_ownPublic;}
    // The letter format, for tests: seal one payload as `session` with sequence `seq`
    static QByteArray sealLetter(const QByteArray &key,const QByteArray &session,const QByteArray &ackSession,quint64 seq,const QByteArray &payload);
    static bool openLetter(const QByteArray &key,const QByteArray &letter,QByteArray *session,QByteArray *ackSession,quint64 *seq,QByteArray *payload);
    // Direction keys from the static secret: ours to send with, the peer's to read with
    static bool directionKeys(const QByteArray &ownSecret,const QByteArray &peerPublic,QByteArray *sendKey,QByteArray *receiveKey);
signals:
    void authenticated();
    void received(const QByteArray &payload);
    void closed();
    void failed();
private:
    void poll();
    void handleLetter(const QByteArray &letter);
    void post(const QByteArray &letter);
    void sendHello();
    QNetworkAccessManager *m_network;
    QTimer m_poll,m_handshake;
    QStringList m_relays;
    QByteArray m_ownSecret,m_ownPublic,m_peerPublic,m_sendKey,m_receiveKey,m_session,m_peerSession;
    QMap<quint64,QByteArray> m_early;   // payloads that arrived ahead of their turn
    QHash<QString,qint64> m_since;      // relay -> last letter time read
    QSet<QByteArray> m_seen;            // letter digests already handled
    quint64 m_nextSend=1,m_nextReceive=1,m_dataSent=0;
    int m_pending=0;
    bool m_ready=false,m_stopped=true,m_peerAcked=false;
};
#endif
