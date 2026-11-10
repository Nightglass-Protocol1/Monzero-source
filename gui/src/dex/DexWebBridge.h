#ifndef MONZERO_DEX_WEB_BRIDGE_H
#define MONZERO_DEX_WEB_BRIDGE_H
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QStringList>
#include <QVariantMap>

class QTcpServer;
class QTcpSocket;

// The link between the DEX web page and this wallet. The page is the screen; the wallet stays
// the engine and the authority.
//
// A loopback-only HTTP server (127.0.0.1, first free port of a short fixed range) that answers
// only the DEX web page: the request's Origin must be on the allowed list and its Host must be
// this loopback address (so another site, or a name pointed at 127.0.0.1, gets nothing).
//
// What the page may do, once the user has accepted the connection IN THE WALLET (the prompt
// shows the site, the MetaMask account and a code that the page shows too):
//   read    the trading wallet's balance, the trade account, this app's offers and trade state
//   ask     to post, take or cancel an offer
// What it can never do: approve a trade, change where the proceeds go, or read a key. A trade
// starts only when the user approves the terms the wallet itself shows; the proceeds go to the
// account accepted at connection time. A page that lies about a price changes nothing.
class DexWebBridge final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap pairRequest READ pairRequest NOTIFY changed)   // {id, origin, account, code} or empty
    Q_PROPERTY(QVariantMap connection READ connection NOTIFY changed)     // {origin, account} or empty
    Q_PROPERTY(int port READ port NOTIFY changed)
    Q_PROPERTY(bool keysWanted READ keysWanted NOTIFY changed)            // the page waits for the trading keys: ask for the password
public:
    DexWebBridge(QObject *engine,const QStringList &allowedOrigins,bool allowLocalPages,QObject *parent=nullptr);
    bool listen(const QString &settingsFile);
    int port() const;
    QVariantMap pairRequest() const {return m_pairRequest;}
    QVariantMap connection() const;
    Q_INVOKABLE bool acceptPairing(const QString &id);
    Q_INVOKABLE void refusePairing(const QString &id);
    Q_INVOKABLE void disconnectPage();
    bool keysWanted() const {return m_keysWanted;}
    Q_INVOKABLE void dismissKeys();
    static const int FirstPort=38471,Ports=5;
signals:
    void changed();
    // Something the user must look at in the wallet (a connection request, a trade to approve)
    void attention(const QString &title,const QString &text);
    // The page asks the wallet to move XMZ between the main balance and the trading wallet. The
    // wallet shows its own screens for both; nothing moves without the user confirming there.
    void xmzAddRequested(const QString &xmz);
    void xmzReturnRequested();
private slots:
    void watchTrade();
private:
    struct Request {QByteArray method,path,query,body;QByteArray origin,host,token;};
    void serve(QTcpSocket *socket);
    bool originAllowed(const QByteArray &origin) const;
    QByteArray respond(const Request &request,int *status);
    QVariantMap statusFor() const;
    bool call(const char *method,const QVariantList &args) const;
    void save() const;
    QPointer<QObject> m_engine;
    QStringList m_origins;
    bool m_localPages;
    QTcpServer *m_server=nullptr;
    QString m_settingsFile;
    QVariantMap m_pairRequest;          // pending; with "token" kept back until accepted
    qint64 m_pairExpires=0;
    QString m_pairResult;               // "", "accepted", "refused" for the last request id
    QString m_pairResultId,m_pairResultToken;
    QByteArray m_tokenHash;             // SHA-256 of the page's token
    // The user's own accounts, accepted at connection time: where proceeds go (MetaMask for
    // ETH/BNB, a Solana wallet such as Phantom for SOL). Either may be absent.
    QString m_origin,m_account,m_solAccount;
    QString payoutAccount(qint64 chainId) const;
    QString m_paidTrade,m_announcedTrade;
    bool m_keysWanted=false;
    qint64 m_lastAsk=0;
};
#endif
