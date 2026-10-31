#ifndef MONZERO_SWAP_WALLET_SESSION_H
#define MONZERO_SWAP_WALLET_SESSION_H
#include "SwapWalletRpcProcess.h"
#include "NativeFeePreviewController.h"
#include <QPointer>
class NativeFeePreviewJob;
class NativeWalletControlJob;
class NativeRecoveryJob;
class QTimer;

// Native application coordinator, not trade authority. The caller verifies the
// helper and owns exclusive access to the separate RPC. No wallet password is
// retained here; Open means wallet identity checked, not chain/funding readiness.
class SwapWalletSession : public QObject {
    Q_OBJECT
public:
    enum State { Unknown, Closed, Open, Busy, RecoveryRequired, Stopped };
    Q_ENUM(State)
    SwapWalletSession(SwapWalletRpcProcess *owner, const QString &verifiedHelper,
        const QString &genesis, QObject *parent = nullptr);
    ~SwapWalletSession() override;
    // Native startup only. Once pinned, every operation rechecks this digest.
    // Metadata trust and protected installation paths remain caller obligations.
    bool pinHelperDigest(const QString &expectedSha256);
    bool pinRecoveryDirectory(const QString &canonicalPrivateDirectory);
    QVariantMap recovery() const;
    // Native coordinator only: pin one journal-derived target for this session.
    bool pinRecoveryTarget(const QString &lockTxid, const QString &preparedTxid,
        const QString &minimumConfirmations);
    bool requestPinnedRecovery();
    // Native trade coordinator only; identities must come from its trusted journal.
    bool checkRecovery(const QString &lockTxid, const QString &preparedTxid,
        const QString &minimumConfirmations);
    State state() const { return m_state; }
    QString address() const { return m_address; }
    QVariantMap preview() const { return m_preview->preview(); }
    bool create(const QString &walletId, QByteArray password);
    bool open(const QString &walletId, QByteArray password, const QString &expectedAddress);
    bool close();
    // Requests cancellation of this session's active helper only. Not rollback:
    // interrupted RPC work leaves the owner requiring recovery.
    bool cancelPendingOperation();
    bool estimateFee(const QString &destination, const QString &amountAtoms,
        const QString &account = "0", const QString &priority = "1");
signals:
    void stateChanged();
    void previewChanged();
    void recoveryChanged();
    void operationCompleted(bool verified);
private:
    bool control(const QString &operation, const QString &walletId, QByteArray password,
        const QString &expectedAddress);
    QPointer<SwapWalletRpcProcess> m_owner;
    NativeFeePreviewController *m_preview;
    QPointer<NativeFeePreviewJob> m_fee;
    QPointer<NativeWalletControlJob> m_control;
    QPointer<NativeRecoveryJob> m_recovery;
    QTimer *m_recoveryExpiry;
    QString m_recoveryDirectory;
    QString m_recoveryLock, m_recoveryTx, m_recoveryMinimum, m_recoveryWallet;
    QString m_helper, m_genesis, m_address;
    QString m_helperDigest;
    State m_state = Unknown;
};
#endif
