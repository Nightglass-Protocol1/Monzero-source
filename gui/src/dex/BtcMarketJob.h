#ifndef MONZERO_BTC_MARKET_JOB_H
#define MONZERO_BTC_MARKET_JOB_H
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <functional>

class QNetworkAccessManager;

// BTC/XMZ offers through the open relays, in the app: list the relays' signed offers, post one,
// take one, answer takes of ours, cancel. The setup (identity card, terms, both signatures) goes
// through sealed relay mail exactly as the trade kit does it (BtcTradeSetup), so an app and a kit
// can trade with each other. Each offer or take gets its own identity and folder under tradesDir;
// once both signatures are in, the folder holds a signed quote and an app folder whose peer is the
// relay, and `agreed` hands its app.json to the normal trade setup review.
//
// Private config (0600) monzero-private-btc-market-v1:
//   network      "btc-mainnet" | "btc-signet" | "btc-testnet4" | "btc-regtest"
//   relays       1-8 relay URLs (https, or http://127.0.0.1:port)
//   tradesDir    where trade folders go
//   journalHelper the trade journal helper the app runs (copied into each app folder)
//   local        this machine's services, as the kit's LOCAL.json (litecoin node or light, xmz wallets)
//   params       our trade parameters when we make an offer: fees, confirmations, timelocks,
//                networks and genesis hashes, and our four destinations (ltcPayout, ltcRefund,
//                xmzPayout, xmzRefund); the ones for our role go into every trade
//   policy       the trade policy the apps run (must agree with params)
class BtcMarketJob final : public QObject {
    Q_OBJECT
public:
    explicit BtcMarketJob(QObject *parent=nullptr);
    bool configure(const QString &path);
    bool refreshListings();
    bool post(const QString &side,const QString &btcSats,const QString &xmzAtoms,int lifetimeHours);
    bool take(const QString &listingId);
    bool cancel(const QString &folder);
    // Reads our mailboxes once and answers or continues every open offer and take
    bool step();
    QVariantMap status() const;
    QString network() const {return m_config.value("network").toString();}
    QVariantMap listingAmounts(const QString &listingId) const;
signals:
    void changed();
    void agreed(const QString &appPath);
private:
    struct Folder {QString path,kind,state,detail,listingId;QJsonObject listing;};
    using Replies=QList<QPair<QString,QByteArray>>;
    void request(const QString &path,const QJsonObject &body,std::function<void(const Replies&)> done,bool post);
    bool newIdentity(const QString &dir,const QString &role,QJsonObject *card);
    QByteArray signTerms(const QString &dir,const QByteArray &payload,const QString &role,QString *error) const;
    void sendLetter(const QString &toHex,const QJsonObject &letter);
    void handleMaker(Folder &f,const QJsonObject &letter);
    void handleTaker(Folder &f,const QJsonObject &letter);
    void finishTrade(Folder &f,const QByteArray &payload,const QString &ownSignature,const QString &peerSignature,
                     const QJsonObject &peerCard,const QJsonObject &policy);
    void scanFolders();
    void fail(const QString &why);
    QNetworkAccessManager *m_network;
    QString m_path,m_state="unconfigured",m_error;
    QJsonObject m_config;
    QStringList m_relays;
    QHash<QString,QJsonObject> m_signed;
    QVariantList m_listings;
    QList<Folder> m_folders;
    QHash<QString,qint64> m_takeSentAt;
    int m_outstanding=0;
};
#endif
