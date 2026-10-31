#ifndef MONZERO_LITECOIN_JOURNAL_JOB_H
#define MONZERO_LITECOIN_JOURNAL_JOB_H
#include <QObject>
#include <QVariantMap>
#include <QByteArray>
class QProcess;
class QTimer;
// Native startup owns paths and RPC configuration; QML supplies only a reviewed commitment.
class LitecoinJournalJob : public QObject {
    Q_OBJECT
public:
    explicit LitecoinJournalJob(QObject *parent=nullptr);
    ~LitecoinJournalJob() override;
    bool configure(const QString &path);
    bool refresh();
    bool reviewStart();
    bool submitStart(const QString &commitment);
    // Signed start quote terms (atomic amounts as decimal strings); empty until configured.
    QVariantMap startTerms() const {return m_startTerms;}
    QString tradeId() const {return m_trade;}
    bool reviewRefund(bool retransmit);
    bool reviewPayout(bool retransmit);
    bool restoreRecoveredWallet();
    bool openRecoveredWallet();
    bool checkRecoveredWallet();
    bool preparePayout();
    bool prepareRefund();
    bool prepareFunding(quint32 safetyBlocks=2);
    bool reviewFunding(bool retransmit,quint32 safetyBlocks=2);
    bool checkFunding();
    bool submitFunding(const QString &commitment);
    bool submitRefund(const QString &commitment);
    bool checkRefund();
    bool submitPayout(const QString &commitment);
    bool checkPayout();
    QVariantMap status() const;
signals:
    void changed();
private:
    bool start(const QString &action,const QString &commitment={},bool checking=false,bool payoutOperation=false,quint32 fundingSafety=0,int startupOperation=0,bool preparing=false,const QString &walletOperation={});
    bool runWalletOperation(const QString &operation);
    void drain();
    void finish(bool success);
    QProcess *m_process;
    QTimer *m_timer;
    QString m_path,m_program,m_digest,m_trade,m_role,m_request,m_action,m_rpcPath,m_checkState="not-checked",m_startQuotePath,m_startState="unconfigured";
    QByteArray m_config,m_input,m_output,m_rpcConfig,m_startQuote;
    QVariantMap m_snapshot,m_fundingReview,m_payoutReview,m_review,m_pendingReview,m_submission,m_confirmation,m_startReview,m_startPending,m_startTerms,m_startPolicy;
    QVariantMap m_fundingSource,m_prepareSnapshot;
    QString m_preparationState="unconfigured";
    bool m_canPrepareFunding=false,m_canPreparePayment=false,m_preparing=false;
    QVariantMap m_walletSnapshot,m_walletResult;
    QString m_walletOperation,m_walletState="not-checked",m_walletAddress;
    bool m_canRecoverWallet=false,m_walletRestoreAttempted=false;
    int m_startOperation=0;
    bool m_canStart=false,m_startAttempted=false;
    qint64 m_errors=0;
    quint32 m_nativeMinimum=0,m_fundingSafety=0;
    bool m_busy=false,m_canSubmit=false,m_canFund=false,m_submitting=false,m_checking=false,m_payoutOperation=false;
};
#endif
