#ifndef MONZERO_LITECOIN_LIGHT_JOB_H
#define MONZERO_LITECOIN_LIGHT_JOB_H
#include <QObject>
#include <QProcess>
#include <QTimer>
#include <QJsonObject>
#include <QJsonArray>
#include <QVariantMap>
#include <QPointer>

// Provisional observations with separately reviewed signing and submission; no trading authority.
class LitecoinLightJob final : public QObject {
    Q_OBJECT
public:
    explicit LitecoinLightJob(QObject *parent=nullptr);
    explicit LitecoinLightJob(const QString &coin,QObject *parent=nullptr);
    ~LitecoinLightJob() override;
    bool configure(const QString &path,const QString &expectedHelper=QString{},const QString &expectedDigest=QString{});
    // Internal desktop funding connection; never exposes seed/password material.
    bool fundingConfiguration(QString &path,QByteArray &bytes,QString &helper,QString &digest,const QString &expectedCoin=QStringLiteral("ltc")) const;
    bool acquireFunding(QObject *owner,const QString &expectedCoin=QStringLiteral("ltc"));
    void releaseFunding(QObject *owner);
    bool previewBackupImport(const QString &password);
    bool importBackup(const QString &digest,const QString &password,bool approved);
    bool refresh();
    bool synchronize(quint32 maximumDownloadHeaders);
    bool initializeWallet();
    bool receiveWallet(const QString &requestId);
    bool receiveCurrentWallet();
    bool refreshWallet(quint32 gap,quint32 addresses,quint32 entries);
    bool prepareWallet(const QString &requestId,const QJsonObject &payment,quint32 gap,quint32 addresses,quint32 entries);
    bool prepareCurrentWallet(const QJsonObject &payment,quint32 gap,quint32 addresses,quint32 entries);
    bool estimateWallet(const QJsonObject &quote,quint32 gap,quint32 addresses,quint32 entries);
    bool reopenWallet(const QString &requestId,const QJsonObject &payment,const QJsonObject &review,quint32 gap,quint32 addresses,quint32 entries);
    bool recoverSignedWallet(const QString &requestId,const QJsonObject &payment,const QJsonObject &savedReview);
    bool signReviewedWallet(const QString &commitment,const QString &password);
    bool checkBroadcastWallet(const QString &requestId,const QJsonObject &payment,const QJsonObject &savedReview,quint32 gap,quint32 addresses,quint32 entries);
    bool broadcastReviewedWallet(const QString &commitment,const QString &txid,const QJsonObject &endpoint);
    bool reconcileWallet(const QString &requestId,const QJsonObject &payment,const QJsonObject &savedReview,quint32 maximumHeaders,quint32 maximumHistory);
    bool previewMnemonicImport(const QString &digest,const QString &phrase,const QString &passphrase);
    bool importMnemonic(const QString &digest,const QString &phrase,const QString &passphrase,const QString &password,const QString &confirmation,bool approved);
    bool discoverImportedWallet(quint32 receiveMinimum,quint32 changeMinimum,quint32 gap,quint32 addresses,quint32 entries,quint32 headers,bool approved);
    bool activateImportedWallet(const QString &digest,const QString &password,bool approved);
    bool recoverImportedWallet(const QString &digest,const QString &password,bool approved);
    bool createWallet(const QString &digest,const QString &password,const QString &confirmation);
    bool reviewCreatedWallet();
    bool openCreatedWallet(const QString &digest);
    bool restoreWallet(const QString &directory,const QString &digest,const QString &password);
    bool backupWallet(const QString &directory,const QString &password,bool save);
    bool loadPaymentRequests();
    bool restoreMnemonicRecovery(const QString &digest,const QString &phrase,const QString &passphrase,const QString &password,const QString &confirmation,bool approved);
    bool restoreRecovery(const QString &digest,const QString &seed,const QString &password,const QString &confirmation,bool approved);
    bool revealRecovery(const QString &digest,const QString &password,bool approved);
    QString recoverySeed() const;
    QString recoveryPassphrase() const;
    void hideRecovery();
    void cancel();
    QVariantMap status() const;
signals:
    void changed();
private:
    bool sourcesMatch() const;
    bool operationBusy() const {return m_busy || !m_fundingOwner.isNull();}
    QPointer<QObject> m_fundingOwner;
    bool adoptBackupRestore(const QJsonObject &expected=QJsonObject{});
    bool adoptActivatedImport(const QJsonObject &expected=QJsonObject{});
    bool createdConfiguration(QByteArray &raw,QJsonObject &receipt,bool imported=false) const;
    bool run(const QString &operation,quint32 budget,const QJsonObject &scan=QJsonObject{},const QString &requestId=QString{},const QJsonObject &payment=QJsonObject{},const QJsonObject &review=QJsonObject{},const QByteArray &password=QByteArray{},const QJsonObject &broadcastApproval=QJsonObject{},const QString &backupDirectory=QString{},const QString &restoreDigest=QString{},const QByteArray &recoverySeed=QByteArray{},const QByteArray &recoveryPassphrase=QByteArray{});
    bool validResponse(const QJsonObject &response) const;
    void drain();
    void finish(bool success);
    const QString m_coin;
    QString m_path,m_configPath,m_program,m_digest,m_network,m_request,m_operation,m_backupDirectory,m_bootstrapPath,m_state="unconfigured";
    QByteArray m_wrapper,m_config,m_input,m_output,m_bootstrap;
    QJsonObject m_importReview,m_discoveryReview,m_discoveryResponse,m_importRecoveryReceipt;
    QJsonArray m_importRecoveryRequests;
    QString m_pendingConfigPath,m_activationReceiptPath;
    QByteArray m_activationReceiptBytes;
    QJsonObject m_importActivation;
    QByteArray m_pendingConfig;
    bool m_importPending=false,m_backupSetup=false;
    QJsonObject m_backupImportReview,m_backupRestoration;
    QString m_backupIntentPath;
    QByteArray m_backupIntentBytes;
    QJsonObject m_chain,m_synchronization,m_wallet,m_scan,m_payment,m_retainedReview,m_uiContext,m_signReview,m_backupReview;
    QJsonArray m_paymentRequests;
    QProcess m_process;
    QTimer m_deadline,m_recoveryDeadline;
    QByteArray m_recovery,m_recoveryPassphrase;
    quint32 m_maximumHeight=0,m_budget=0;
    qint64 m_errors=0;
    qint64 m_startedAt=0;
    bool m_busy=false,m_sync=false,m_endpoint=false,m_sourcesValid=false,m_walletConfigured=false,m_spendConfigured=false,m_vaultConfigured=false,m_setupConfigured=false;
};
#endif
