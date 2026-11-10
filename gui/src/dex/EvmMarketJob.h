#ifndef MONZERO_EVM_MARKET_JOB_H
#define MONZERO_EVM_MARKET_JOB_H
#include <QJsonObject>
#include <QObject>
#include <QProcess>
#include <QTimer>
#include <QVariantList>
// Public XMZ<->ETH/BNB offers through the pinned monzero-dex-evm-helper (one JSON request per run):
// list the relays' offers (checked by the helper: maker signature, pinned contract), post one,
// answer takes of ours (serve), take one, cancel ours, report a finished claim for the chart.
// The key exchange runs through sealed relay mail, so neither trader needs an open port.
// Once keys are agreed the folder holds a saved trade, and `agreed` hands it to EvmTradeJob.
// The wallet seed comes from native vault code only (never QML) and is wiped once written.
class EvmMarketJob final : public QObject {
    Q_OBJECT
public:
    explicit EvmMarketJob(QObject *parent=nullptr);
    ~EvmMarketJob() override;
    // Private config (0600): monzero-private-xmz-evm-market-v1
    bool configure(const QString &path);
    bool refreshListings(const QString &pair);
    bool post(QByteArray &walletSeed,qint64 chainId,const QString &side,const QString &valueWei,const QString &xmzAtoms,qint64 lifetimeSecs);
    bool take(QByteArray &walletSeed,const QString &listingId);
    bool cancel(QByteArray &walletSeed,const QString &folder);
    bool report(const QString &folder);
    // Send the trade account's unused coin to the user's own wallet; the helper refuses while a
    // trade is running or an offer of ours is open
    bool withdraw(QByteArray &walletSeed,const QString &address);
    // Answers takes of our open offers and continues our takes; one helper run per call
    bool step(QByteArray &walletSeed);
    QVariantMap status() const;
private:
    QVariantMap m_withdrawal;
public:
    // Keys of the trade account from the light-wallet vault (hex); used instead of a wallet seed
    void setSwapKeys(const QByteArray &swapSeedHex,const QByteArray &evmKeyHex);
    QJsonObject config() const {return m_config;}
    // The values a trade config for EvmTradeJob needs (helper, endpoints, account)
    QJsonObject tradeConfig(const QString &tradeDir) const;
    // Coin per XMZ, as text, from wei and atoms (display only)
    static QString priceText(const QString &valueWei,const QString &xmzAtoms);
    static QString weiToCoin(const QString &valueWei);
    // Folders of trades whose keys were agreed (running or finished)
    QStringList agreedFolders() const;
    void rescan() {scanFolders();emit changed();}
signals:
    void changed();
    void agreed(const QString &tradeDir);
private:
    QByteArray m_swapSeedHex,m_evmKeyHex;
    struct Folder {QString path,kind,listingId,state,detail;QJsonObject listing;};
    bool run(const QJsonObject &request,const QString &folder);
    bool tickConfig(QByteArray &walletSeed,QJsonObject *out) const;
    void scanFolders();
    void drain();
    void finish(bool success);
    QJsonObject base(const QString &command,const QString &folder) const;
    QString m_path,m_program,m_digest,m_tradesDir,m_command,m_folder,m_state="unconfigured",m_error;
    QByteArray m_source,m_input,m_output;
    QJsonObject m_config;
    QHash<QString,QJsonObject> m_signed;   // listing id -> signed listing, as the helper returned it
    QVariantList m_listings;
    QList<Folder> m_folders;
    QProcess m_process;
    QTimer m_deadline;
    qint64 m_errors=0;
    int m_next=0;
    bool m_busy=false;
};
#endif
