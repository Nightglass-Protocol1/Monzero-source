#ifndef MONZERO_DIRECT_MARKET_JOB_H
#define MONZERO_DIRECT_MARKET_JOB_H
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QProcess>
#include <QTimer>
#include <QVariantList>
// Direct (coin <-> coin) trades between ETH, BNB and SOL, through the pinned helper's direct
// commands (one JSON request per run): list the relays' direct offers, post one, answer takes of
// ours (directServe), take one (directTake), approve a taken trade (taker), step every agreed
// trade (directTick) until it swapped or refunded, then pay what it brought in to the user's
// own wallet (directPayout). Each offer, take and trade is a folder under tradesDir, so all of
// it survives a restart. Unlike the XMZ markets, any number of direct trades run at once: each
// step() runs the helper once for every folder that has work, one after another.
//
// The trade keys come from native vault code only (setSwapKeys); without them nothing runs.
class DirectMarketJob final : public QObject {
    Q_OBJECT
public:
    explicit DirectMarketJob(QObject *parent=nullptr);
    ~DirectMarketJob() override;
    // {helper, helperDigest, tradesDir, rpcs: {"<chainId>": url}, relays: [...], mainnet: bool,
    //  localSolProgram?: base58}. The chains with a node are the chains this market trades.
    bool configure(const QJsonObject &config);
    void setSwapKeys(const QByteArray &swapSeedHex,const QByteArray &evmKeyHex);
    bool keysSet() const {return !m_swapSeedHex.isEmpty();}
    // Every direct pair's offers
    bool refreshListings();
    // Amounts in base units (wei, lamports) as decimal digits
    bool post(qint64 givesChain,const QString &givesValue,qint64 wantsChain,const QString &wantsValue,qint64 lifetimeSecs);
    bool take(const QString &listingId);
    // Our open offer: withdrawn from the relays. Our take still waiting for the maker: we stop
    // waiting (nothing of it is locked yet).
    bool cancel(const QString &folder);
    // The taker's go-ahead for the terms its trade shows (its commitment)
    bool approve(const QString &folder,const QString &commitment);
    // After the trade: what it brought in goes to `address` on the chain it came in on
    bool payout(const QString &folder,const QString &address);
    // One helper run for each folder with work (serve, take, tick); false when nothing to do
    bool step();
    QVariantMap status() const;
    // A listing the market holds (as shown), or empty
    QVariantMap listing(const QString &id) const;
    // A folder's entry (as status() shows it), or empty
    QVariantMap folder(const QString &path) const;
    QJsonObject config() const {return m_config;}
    QList<qint64> chains() const;
    // Base units (digits) as coin text, and back; display only
    static QString unitsText(const QString &digits,int places);
    static QString hexToDecimal(const QString &hex);
    // Price of a pair's base in its quote coin: quoteUnits/10^qd ÷ baseUnits/10^bd, six figures
    static QString priceText(const QString &baseUnits,int baseDecimals,const QString &quoteUnits,int quoteDecimals);
    static QString pairOf(qint64 chainA,qint64 chainB);
    static QString tickerOf(qint64 chainId);
    static int decimalsOf(qint64 chainId);
signals:
    void changed();
    // Keys agreed for one of our offers (we are its maker) or takes; the trade starts now
    void agreed(const QString &folder);
private:
    struct Folder {
        QString path,kind,listingId,state,detail;
        QJsonObject listing;      // the signed listing
        QVariantMap trade;        // the helper's last view of the trade (directStatus / directTick)
    };
    struct Run {QJsonObject request;QString folder;};
    QJsonObject base(const QString &command,const QString &folder) const;
    QJsonObject keys() const;
    bool enqueue(const QJsonObject &request,const QString &folder,bool urgent=false);
    void next();
    void scanFolders();
    Folder *find(const QString &path);
    void drain();
    void finish(bool success);
    void handle(const QJsonObject &r);
    QVariantMap shown(const QJsonObject &signedListing,int relays) const;
    QVariantMap shown(const Folder &f) const;

    QJsonObject m_config;
    QString m_program,m_digest,m_tradesDir,m_command,m_folder,m_state="unconfigured",m_error;
    QByteArray m_swapSeedHex,m_evmKeyHex,m_input,m_output;
    QHash<QString,QJsonObject> m_signed;   // listing id -> signed listing, as the helper returned it
    QVariantList m_listings;
    QStringList m_relayErrors;
    QList<Folder> m_folders;
    QList<Run> m_queue;
    QVariantMap m_lastPayout;
    QProcess m_process;
    QTimer m_deadline;
    qint64 m_errors=0;
    bool m_busy=false;
};
#endif
