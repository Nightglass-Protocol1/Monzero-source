#ifndef MONZERO_DEX_EXCHANGE_FEED_H
#define MONZERO_DEX_EXCHANGE_FEED_H
#include <QHash>
#include <QObject>
#include <QStringList>
#include <QVariantMap>
#include <functional>
class QNetworkAccessManager;

// Read-only public data for the in-app exchange (Linux): candles and completed trades from the
// open relays, the trade account's coin balance from the chain's RPC node, and USD prices and
// the NoirTrade XMZ/USDT market (monzero.org's own copy; the app never contacts NoirTrade) from
// monzero.org. QML may not use the network (NetworkAccessBlockingFactory), so the page asks here.
// It names only a coin and an interval: the hosts and the account come from the engine's own
// market configuration (the same relays and RPC nodes its helper already uses), so the page
// cannot make the app contact anything else. Nothing here is signed or used to trade.
class DexExchangeFeed final : public QObject {
    Q_OBJECT
    // "candles:<coin>:<interval>" -> relay candles, "prints:<coin>" -> relay prints,
    // "balance:<coin>" -> the trade account's balance in wei/lamports (decimal text), "usd" -> {coin: price},
    // "usdHistory" -> {coin: [[seconds, usd], …]} hourly, "noir" -> NoirTrade's XMZ/USDT market from monzero.org: {last, stale, trades: [[ms, price], …]}
    Q_PROPERTY(QVariantMap data READ data NOTIFY changed)
public:
    struct Source {QStringList relays;QString rpc,account;bool solana=false;};
    using SourceFor=std::function<Source(const QString &coin)>;
    DexExchangeFeed(SourceFor sources,QObject *parent=nullptr);
    QVariantMap data() const {return m_data;}
    Q_INVOKABLE bool candles(const QString &coin,int interval);
    Q_INVOKABLE bool prints(const QString &coin);
    Q_INVOKABLE bool balance(const QString &coin);
    Q_INVOKABLE bool usdPrices();
    Q_INVOKABLE bool noirTrade();
    // Accepted coins and chart intervals (the relays' own list)
    static bool knownCoin(const QString &coin);
    static bool knownInterval(int interval);
    // A relay or RPC base the feed will use: https, or http to this machine (tests)
    static bool allowedBase(const QString &url);
signals:
    void changed();
private:
    void get(const QStringList &bases,const QString &path,const QString &key,int index=0);
    void store(const QString &key,const QVariant &value);
    SourceFor m_sources;
    QNetworkAccessManager *m_net;
    QVariantMap m_data;
    QHash<QString,bool> m_pending;
};
#endif
