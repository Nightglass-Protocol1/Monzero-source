#ifndef MONZERO_SWAP_WALLET_RPC_PROCESS_H
#define MONZERO_SWAP_WALLET_RPC_PROCESS_H
#include <QObject>
#include <QString>
#include <memory>
class QLockFile;
class QProcess;
class QTimer;

// One-shot owner of a fresh or cleanly stopped separate service. Does not open/create a wallet,
// attest readiness, sign, transfer, or share the active GUI wallet's files.
class SwapWalletRpcProcess : public QObject {
    Q_OBJECT
public:
    explicit SwapWalletRpcProcess(QObject *parent = nullptr);
    ~SwapWalletRpcProcess() override;
    bool startFresh(const QString &verifiedProgram, const QString &privateParent,
        quint16 daemonPort, quint16 walletPort);
    // Expected digest comes from trusted installation metadata, never peers.
    bool startFreshWithDigest(const QString &program, const QString &expectedSha256,
        const QString &privateParent, quint16 daemonPort, quint16 walletPort);
    bool resumeClean(const QString &verifiedProgram, const QString &dataDirectory,
        quint16 daemonPort, quint16 walletPort);
    bool resumeCleanWithDigest(const QString &program, const QString &expectedSha256,
        const QString &dataDirectory, quint16 daemonPort, quint16 walletPort);
    // Stable per-trade directory: create once, then resume only cleanly stopped.
    bool startBoundWithDigest(const QString &program,const QString &expectedSha256,
        const QString &directory,const QString &tradeId,quint16 daemonPort,quint16 walletPort);
    void stop();
    // At quit: end the service now (it saves its wallet on SIGTERM), waiting up to waitMs, then kill
    void stopNow(int waitMs);
    // The XMZ network of the daemon ("--testnet" or "--stagenet"; none for mainnet). Before start.
    bool setNetworkFlag(const QString &flag);
    // The node the wallet reads the chain from: 127.0.0.1 (the app's own node, the default) or a
    // remote node's host name or IPv4 address (Monero: the project's or the user's own node)
    bool setDaemonHost(const QString &host);
    // The program is Monero's own wallet RPC (its login file is named after it)
    bool setMoneroWallet();
    // Caller must record before dispatch and complete only after verifying the
    // native operation. Tokens prevent late callbacks completing a newer action.
    bool walletOperationAvailable() const;
    QString beginWalletOperation();
    bool completeWalletOperation(const QString &token, bool verifiedSuccess);
    QString provisionHelperSession(const QString &expectedGenesis);
    bool ownsHelperSession(const QString &path) const;
    // Private connection descriptor for BCH helpers; credentials stay in the
    // service-owned login file. Callers still need an operation token to mutate.
    QString provisionBchNativeSession();
    bool ownsBchNativeSession(const QString &path) const;
    QString dataDirectory() const { return m_directory; }
    QString loginFile() const { return m_login; }
    qint64 processId() const;
signals:
    void availabilityChanged();
    void processStarted(); // Not authenticated readiness or chain validation.
    void stopped(bool cleanExit);
private:
    void notifyAvailabilityChanged();
    static bool rebindProgram(const QString &directory,const QByteArray &binding);
    bool launch(const QString &program, const QString &directory, quint16 daemonPort,
        quint16 walletPort, bool resume,const QByteArray &binding=QByteArray());
    bool recordState(const QByteArray &state);
    QString m_networkFlag;
    QString m_daemonHost=QStringLiteral("127.0.0.1");
    QString m_loginPrefix=QStringLiteral("monzero-wallet-rpc");
    bool finishRecord(bool clean);
    bool removeLoginAfterExit();
    std::unique_ptr<QLockFile> m_lock;
    QProcess *m_process;
    QTimer *m_stopTimer;
    QString m_directory, m_login;
    bool m_used = false;
    bool m_terminal = false;
    bool m_recoveryRequired = false;
    QString m_operation;
    QString m_helperSession, m_helperGenesis, m_bchSession;
    QByteArray m_bchSessionBytes;
    quint16 m_daemonPort = 0, m_walletPort = 0;
};
#endif
