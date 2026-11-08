#ifndef MONZERO_DEX_WALLET_SERVICE_H
#define MONZERO_DEX_WALLET_SERVICE_H
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QVariantMap>
#include <functional>

class SwapWalletRpcProcess;

// The XMZ wallets the app runs for trading, so traders need no wallet services of their own and
// the main GUI wallet is never opened twice:
//  - the DEX trading wallet: one wallet in the app's own wallet RPC service (created on first use,
//    resumed after); the user funds it from the main wallet, XMZ from trades lands in it, and the
//    user sends it back from the DEX screen;
//  - one swap-wallet service per trade, bound to the trade's folder (the trade's shared wallet).
// Services use the wallet RPC installed with the app (digest taken when the app starts) and the
// local XMZ daemon. Wallet actions run through the pinned EVM helper (`xmzWallet`).
class DexWalletService final : public QObject {
    Q_OBJECT
public:
    explicit DexWalletService(QObject *parent=nullptr);
    ~DexWalletService() override;
    bool configure(const QString &root,const QString &walletRpc,const QString &walletRpcSha256,quint16 daemonPort,
                   const QString &networkFlag,const QString &helper,const QString &helperSha256);
    bool start();
    void stop();
    bool ready() const {return m_state=="ready";}
    QJsonObject mainEndpoint() const;
    QVariantMap status() const;
    bool refresh();
    // Sends XMZ from the trading wallet (everything when atoms is empty)
    bool send(const QString &address,const QString &atoms);
    // A swap-wallet service bound to this trade; `done` gets its endpoint once it answers
    void tradeService(const QString &tradeDir,const QString &tradeId,std::function<void(const QJsonObject&)> done);
    QString helper() const {return m_helper;}
    QString root() const {return m_root;}
    // The XMZ wallet settings a BTC/LTC trade needs (trade kit "local.xmz"); empty until all services answer
    QJsonObject utxoLocal() const;
    QString networkFlag() const {return m_networkFlag;}
    quint16 daemonPort() const {return m_daemonPort;}
    QString helperSha256() const {return m_helperSha256;}
signals:
    void changed();
private:
    bool runHelper(const QJsonObject &walletRequest,std::function<void(const QJsonObject&)> done);
    void openWallet();
    bool makePasswordFile(const QString &path);
    bool resumeAfterUncleanExit(const QString &serviceDir,const QString &portPath);
    bool startService(const QString &name,const QString &id,QPointer<SwapWalletRpcProcess> &slot,quint16 &port,std::function<void()> listening);
    static quint16 freePort();
    QString m_root,m_walletRpc,m_walletRpcSha256,m_networkFlag,m_helper,m_helperSha256,m_state="unconfigured",m_error;
    quint16 m_daemonPort=0,m_walletPort=0;
    QPointer<SwapWalletRpcProcess> m_main,m_claim,m_observer;
    quint16 m_claimPort=0,m_observerPort=0;
    bool m_claimUp=false,m_observerUp=false,m_stopping=false;
    QHash<QString,QPointer<SwapWalletRpcProcess>> m_trades;
    QHash<QString,quint16> m_tradePorts;
    QJsonObject m_wallet;
    QTimer m_poll;
    int m_running=0;
};
#endif
