#ifndef MONZERO_DEX_ENGINE_H
#define MONZERO_DEX_ENGINE_H

#include <QObject>
#include <QVariantMap>
#include <QJsonObject>
#include <QVariantList>
#include <QStringList>
#include <functional>
#include <QFutureWatcher>
#include <QPointer>
#include <QHash>

enum class OfferState { Draft, Quoted, Matched, XmzLocked, ExternalLocked, Redeemable, Settled, Refunding, Refunded, Expired, Cancelled };
class NativeFeePreviewController;
class SwapWalletSession;
class SwapWalletRpcProcess;
class QTimer;
class DexRiskControl;
class NativeQuoteCoordinator;
class BchAdmissionJob;
class BchFundingReceiptJob;
class BchNativePreparationJob;
class BchNativeRelayJob;
class BchNativeProofJob;
class BchNativeObservationJob;
class EvmTradeJob;
class BchSuccessReleaseReviewJob;
class BchSuccessPreparationJob;
class BchPublicationRepairJob;
class BchRefundProtectionJob;
class BchRecoveryRetentionJob;
class BchCancellationJob;
class BchPunishmentJob;
class BchRefundWalletJob;
class BchRefundReleaseJob;
class BchFundingJob;
class BchPreparationJob;
class LitecoinLightJob;
class LitecoinLightFundingController;
class BchLightFundingController;
class LitecoinJournalJob;
class LitecoinNegotiationSession;
class LitecoinReservationJob;
class LitecoinRedemptionJob;
class LitecoinPeerStartup;

class DexEngine : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap litecoinLightFundingStatus READ litecoinLightFundingStatus NOTIFY litecoinStateChanged)
    Q_PROPERTY(QVariantMap lightWallets READ lightWallets NOTIFY lightWalletsChanged)
    // Ticker and name of the swap chain selected at startup ("LTC"/"Litecoin" or "BTC"/"Bitcoin").
    Q_PROPERTY(QString swapCoin READ swapCoin CONSTANT)
    Q_PROPERTY(QString swapCoinName READ swapCoinName CONSTANT)
    Q_PROPERTY(QVariantMap litecoinLightStatus READ litecoinLightStatus NOTIFY litecoinLightChanged)
    Q_PROPERTY(QVariantMap bchPreparationStatus READ bchPreparationStatus NOTIFY bchPreparationChanged)
    Q_PROPERTY(QVariantMap bchFundingStatus READ bchFundingStatus NOTIFY bchFundingChanged)
    Q_PROPERTY(QVariantMap bchFundingReceiptStatus READ bchFundingReceiptStatus NOTIFY bchFundingReceiptChanged)
    Q_PROPERTY(QVariantMap bchNativePreparationStatus READ bchNativePreparationStatus NOTIFY bchNativePreparationChanged)
    Q_PROPERTY(QVariantMap bchNativeRelayStatus READ bchNativeRelayStatus NOTIFY bchNativeRelayChanged)
    Q_PROPERTY(QVariantMap bchNativeProofStatus READ bchNativeProofStatus NOTIFY bchNativeProofChanged)
    Q_PROPERTY(QVariantMap bchNativeObservationStatus READ bchNativeObservationStatus NOTIFY bchNativeObservationChanged)
    Q_PROPERTY(QVariantMap evmTradeStatus READ evmTradeStatus NOTIFY evmTradeChanged)
    Q_PROPERTY(QVariantMap bchSuccessReleaseReviewStatus READ bchSuccessReleaseReviewStatus NOTIFY bchSuccessReleaseReviewChanged)
    Q_PROPERTY(QVariantMap bchPublicationRepairStatus READ bchPublicationRepairStatus NOTIFY bchPublicationRepairChanged)
    Q_PROPERTY(QVariantMap bchSuccessPreparationStatus READ bchSuccessPreparationStatus NOTIFY bchSuccessPreparationChanged)
    Q_PROPERTY(QVariantMap bchSuccessSignedReleaseReviewStatus READ bchSuccessSignedReleaseReviewStatus NOTIFY bchSuccessSignedReleaseReviewChanged)
    Q_PROPERTY(QVariantMap bchSuccessSignatureReviewStatus READ bchSuccessSignatureReviewStatus NOTIFY bchSuccessSignatureReviewChanged)
    Q_PROPERTY(QVariantMap bchRefundProtectionStatus READ bchRefundProtectionStatus NOTIFY bchRefundProtectionChanged)
    Q_PROPERTY(QVariantMap bchRecoveryRetentionStatus READ bchRecoveryRetentionStatus NOTIFY bchRecoveryRetentionChanged)
    Q_PROPERTY(QVariantMap bchRefundWalletStatus READ bchRefundWalletStatus NOTIFY bchRefundWalletChanged)
    Q_PROPERTY(QVariantMap dexRiskStatus READ dexRiskStatus NOTIFY dexRiskChanged)
    Q_PROPERTY(QVariantList tradeFolders READ tradeFolders NOTIFY tradeFoldersChanged)
    Q_PROPERTY(QString loadedTradeId READ loadedTradeId NOTIFY tradeFoldersChanged)
    Q_PROPERTY(QString litecoinStartRefusal READ litecoinStartRefusal NOTIFY dexRiskChanged)
    Q_PROPERTY(QVariantMap bchPunishmentStatus READ bchPunishmentStatus NOTIFY bchPunishmentChanged)
    Q_PROPERTY(QVariantMap bchCancellationStatus READ bchCancellationStatus NOTIFY bchCancellationChanged)
    Q_PROPERTY(QVariantMap bchRefundReleaseStatus READ bchRefundReleaseStatus NOTIFY bchRefundReleaseChanged)
    Q_PROPERTY(QVariantMap bchLightFundingStatus READ bchLightFundingStatus NOTIFY bchLightFundingChanged)
    Q_PROPERTY(QVariantMap bchAdmissionStatus READ bchAdmissionStatus NOTIFY bchAdmissionChanged)
    Q_PROPERTY(QVariantMap litecoinAppStatus READ litecoinAppStatus NOTIFY litecoinStateChanged)
    Q_PROPERTY(QVariantMap litecoinFundingStatus READ litecoinFundingStatus NOTIFY litecoinStateChanged)
    Q_PROPERTY(QVariantMap litecoinPaymentStatus READ litecoinPaymentStatus NOTIFY litecoinStateChanged)
    Q_PROPERTY(QVariantMap litecoinRedemptionStatus READ litecoinRedemptionStatus NOTIFY litecoinStateChanged)
    Q_PROPERTY(QVariantMap litecoinReservationStatus READ litecoinReservationStatus NOTIFY litecoinStateChanged)
    Q_PROPERTY(QVariantMap litecoinNegotiationStatus READ litecoinNegotiationStatus NOTIFY litecoinStateChanged)
    Q_PROPERTY(QVariantMap litecoinJournalStatus READ litecoinJournalStatus NOTIFY litecoinStateChanged)
    Q_PROPERTY(bool researchMarketBusy READ researchMarketBusy NOTIFY researchMarketBusyChanged)
    Q_PROPERTY(QVariantMap nativeFeePreview READ nativeFeePreview NOTIFY nativeFeePreviewChanged)
    Q_PROPERTY(QVariantMap nativeWalletStatus READ nativeWalletStatus NOTIFY nativeWalletStatusChanged)
    Q_PROPERTY(QVariantMap nativeRecoveryStatus READ nativeRecoveryStatus NOTIFY nativeRecoveryStatusChanged)
    Q_PROPERTY(QVariantMap nativeQuoteReview READ nativeQuoteReview NOTIFY nativeQuoteReviewChanged)
    Q_PROPERTY(QVariantMap nativeQuoteStatus READ nativeQuoteStatus NOTIFY nativeQuoteReviewChanged)
public:
    explicit DexEngine(QObject *parent = nullptr);
    ~DexEngine();
    bool configureLightWallet(const QString &coin,const QString &path);
    bool configureLightWalletSetup(const QString &helper,const QString &digest);
    Q_PROPERTY(bool lightWalletSetupAvailable READ lightWalletSetupAvailable NOTIFY lightWalletsChanged)
    bool lightWalletSetupAvailable() const {return !m_lightSetupHelper.isEmpty();}
    Q_PROPERTY(QVariantMap lightWalletSetupNotices READ lightWalletSetupNotices NOTIFY lightWalletsChanged)
    QVariantMap lightWalletSetupNotices() const {return m_lightSetupNotices;}
    Q_INVOKABLE QVariantMap reviewLightWalletHelperUpgrade(const QString &coin,const QString &directory) const;
    Q_INVOKABLE bool upgradeLightWalletHelper(const QString &coin,const QString &directory,const QString &commitment);
    Q_INVOKABLE bool openLocalLightWallet(const QString &coin,const QString &directory);
    Q_INVOKABLE bool prepareLocalBackupRestore(const QString &coin,const QString &network,const QString &directory,const QString &backup,const QString &host,int port);
    Q_INVOKABLE bool previewLightBackupImport(const QString &coin,const QString &password);
    Q_INVOKABLE bool importLightBackup(const QString &coin,const QString &digest,const QString &password,bool approved);
    Q_INVOKABLE bool prepareLocalLightWallet(const QString &coin,const QString &network,const QString &directory,const QString &host,int port,int accountIndex=0);
    QVariantMap lightWallets() const;
    QString swapCoin() const;
    QString swapCoinName() const;
    Q_INVOKABLE bool prepareLightPayment(const QString &coin,const QVariantMap &payment);
    Q_INVOKABLE bool reopenLightPayment(const QString &coin,const QString &requestId,const QVariantMap &payment,const QVariantMap &review);
    Q_INVOKABLE bool signLightPayment(const QString &coin,const QString &commitment,const QString &password);
    Q_INVOKABLE bool checkLightPaymentSend(const QString &coin,const QString &requestId,const QVariantMap &payment,const QVariantMap &review);
    Q_INVOKABLE bool sendLightPayment(const QString &coin,const QString &commitment,const QString &txid,const QVariantMap &endpoint);
    Q_INVOKABLE bool reconcileLightPayment(const QString &coin,const QString &requestId,const QVariantMap &payment,const QVariantMap &review);
    Q_INVOKABLE bool loadLightPaymentRequests(const QString &coin);
    Q_INVOKABLE bool activateLightWalletImport(const QString &coin,const QString &digest,const QString &password,bool approved);
    Q_INVOKABLE bool recoverLightWalletImport(const QString &coin,const QString &digest,const QString &password,bool approved);
    Q_INVOKABLE bool discoverLightWalletImport(const QString &coin,int receiveMinimum,int changeMinimum,bool approved);
    Q_INVOKABLE bool previewLightWalletImport(const QString &coin,const QString &digest,const QString &phrase,const QString &passphrase);
    Q_INVOKABLE bool importLightWalletMnemonic(const QString &coin,const QString &digest,const QString &phrase,const QString &passphrase,const QString &password,const QString &confirmation,bool approved);
    Q_INVOKABLE bool restoreLightWalletMnemonic(const QString &coin,const QString &digest,const QString &phrase,const QString &passphrase,const QString &password,const QString &confirmation,bool approved);
    Q_INVOKABLE bool restoreLightWalletRecovery(const QString &coin,const QString &digest,const QString &seed,const QString &password,const QString &confirmation,bool approved);
    Q_INVOKABLE bool revealLightWalletRecovery(const QString &coin,const QString &digest,const QString &password,bool approved);
    Q_INVOKABLE QString lightWalletRecoveryPassphrase(const QString &coin) const;
    Q_INVOKABLE QString lightWalletRecoverySeed(const QString &coin) const;
    Q_INVOKABLE void hideLightWalletRecovery(const QString &coin);
    Q_INVOKABLE bool createLightWallet(const QString &coin,const QString &digest,const QString &password,const QString &confirmation);
    Q_INVOKABLE bool reviewCreatedLightWallet(const QString &coin);
    Q_INVOKABLE bool openCreatedLightWallet(const QString &coin,const QString &digest);
    Q_INVOKABLE bool receiveLightWallet(const QString &coin);
    Q_INVOKABLE bool backupLightWallet(const QString &coin,const QString &directory,const QString &password,bool save);
    Q_INVOKABLE bool restoreLightWallet(const QString &coin,const QString &directory,const QString &digest,const QString &password);
    Q_INVOKABLE bool synchronizeLightWallet(const QString &coin,quint32 maximumHeaders);
    Q_INVOKABLE bool refreshLightWallet(const QString &coin);
    Q_INVOKABLE void cancelLightWallet(const QString &coin);
    bool configureLitecoinLight(const QString &path);
    QVariantMap litecoinLightStatus() const;
    Q_INVOKABLE bool requestLitecoinLightStatus();
    Q_INVOKABLE bool synchronizeLitecoinLight(quint32 maximumDownloadHeaders);
    Q_INVOKABLE bool prepareLitecoinLightPayment(const QString &requestId,const QVariantMap &payment,quint32 gap,quint32 addresses,quint32 entries);
    Q_INVOKABLE bool recoverSignedLitecoinPayment(const QString &requestId,const QVariantMap &payment,const QVariantMap &savedReview);
    Q_INVOKABLE bool signReviewedLitecoinPayment(const QString &commitment,const QString &password);
    Q_INVOKABLE bool checkLitecoinPaymentSendStatus(const QString &requestId,const QVariantMap &payment,const QVariantMap &savedReview,quint32 gap,quint32 addresses,quint32 entries);
    Q_INVOKABLE bool sendReviewedLitecoinPayment(const QString &commitment,const QString &txid,const QVariantMap &endpoint);
    Q_INVOKABLE bool createLitecoinWallet(const QString &digest,const QString &password,const QString &confirmation);
    Q_INVOKABLE bool reviewCreatedLitecoinWallet();
    Q_INVOKABLE bool openCreatedLitecoinWallet(const QString &digest);
    Q_INVOKABLE bool restoreLitecoinWallet(const QString &directory,const QString &digest,const QString &password);
    Q_INVOKABLE bool backupLitecoinWallet(const QString &directory,const QString &password,bool save);
    Q_INVOKABLE bool reconcileLitecoinPayment(const QString &requestId,const QVariantMap &payment,const QVariantMap &savedReview,quint32 maximumHeaders,quint32 maximumHistory);
    Q_INVOKABLE bool reopenLitecoinPayment(const QString &requestId,const QVariantMap &payment,const QVariantMap &review,quint32 gap,quint32 addresses,quint32 entries);
    Q_INVOKABLE bool estimateLitecoinPaymentFee(const QVariantMap &quote,quint32 gap,quint32 addresses,quint32 entries);
    Q_INVOKABLE bool prepareCurrentLitecoinPayment(const QVariantMap &payment,quint32 gap,quint32 addresses,quint32 entries);
    Q_INVOKABLE bool loadLitecoinPaymentRequests();
    Q_INVOKABLE bool initializeLitecoinLightWallet();
    Q_INVOKABLE bool receiveLitecoinLightWallet(const QString &requestId);
    Q_INVOKABLE bool showLitecoinReceivingAddress();
    Q_INVOKABLE bool refreshLitecoinLightWallet(quint32 gap,quint32 addresses,quint32 entries);
    Q_INVOKABLE void cancelLitecoinLight();
    void requestBchStartupChecks();
    bool configureBchPreparation(const QString &path);
    bool configureBchSigningRecovery(const QString &path);
    Q_INVOKABLE bool requestBchSigningRecoveryReview();
    Q_INVOKABLE bool importBchSigningRecovery(const QString &commitment);
    QVariantMap bchPreparationStatus() const;
    Q_INVOKABLE bool prepareBchFunding();
    Q_INVOKABLE bool requestBchPreparationCheck();
    Q_INVOKABLE bool requestBchSigningReview();
    Q_INVOKABLE bool requestBchSigningCheck();
    Q_INVOKABLE bool submitBchSigning(const QString &commitment);
    Q_INVOKABLE bool requestBchLockReview();
    Q_INVOKABLE bool requestBchLockCheck();
    Q_INVOKABLE bool submitBchLock(const QString &commitment);
    bool configureBchFunding(const QString &path);
    bool configureBchFundingSubmission(const QString &path);
    QVariantMap bchFundingStatus() const;
    Q_INVOKABLE bool requestBchFundingReview();
    Q_INVOKABLE bool requestBchFundingCheck();
    Q_INVOKABLE bool requestBchFundingObservation();
    Q_INVOKABLE bool submitBchFunding(const QString &commitment);
    bool configureBchFundingReceipt(const QString &path);
    bool configureBchNativePreparation(const QString &path);
    bool configureBchNativeRelay(const QString &path);
    bool configureBchNativeProof(const QString &path,const QJsonObject &fundingContext={});
    bool configureBchRefundProtection(const QString &path);
    bool configureBchRecoveryRetention(const QString &path);
    bool configureBchRefundWallet(const QString &path);
    bool configureBchRefundWalletReview(const QString &path);
    // Native coordinator only; no QML attachment of arbitrary RPC services.
    bool attachBchRefundWalletService(SwapWalletRpcProcess *owner);
    bool provisionBchRefundWalletService(SwapWalletRpcProcess *owner,const QString &bchRpcFile,const QString &observerRpcFile);
    // Startup coordinator: binds one trade-bound service directory, resumes it only
    // when cleanly stopped, then provisions/attaches the review. Never creates a
    // replacement service or retries a refused/recovery-required directory.
    bool configureBchRefundWalletService(const QString &path);
    // Blocking clean stop for application shutdown (bounded by the service's own kill timer).
    void shutdownBchRefundWalletService();
    bool submitBchRefundWallet(const QString &commitment,QByteArray &&password);
    Q_INVOKABLE bool approveBchRefundWallet(const QString &commitment,const QString &password,const QString &confirmation,bool approved);
    Q_INVOKABLE bool requestBchRefundWalletReview(const QString &action);
    QVariantMap bchRefundWalletStatus() const;
    Q_INVOKABLE bool requestBchRefundWalletCheck();
    bool configureBchPunishment(const QString &path);
    bool configureBchPunishmentObservation(const QString &path);
    bool configureBchPunishmentSubmission(const QString &path);
    Q_INVOKABLE bool submitBchPunishment(const QString &commitment);
    Q_INVOKABLE bool observeBchPunishment();
    QVariantMap bchPunishmentStatus() const;
    Q_INVOKABLE bool requestBchPunishmentReview();
    Q_INVOKABLE bool requestBchPunishmentCheck();
    bool configureBchCancellation(const QString &path);
    bool configureBchRefundRelease(const QString &path);
    bool configureBchCancellationSubmission(const QString &path);
    bool configureBchRefundReleaseSubmission(const QString &path);
    Q_INVOKABLE bool observeBchCancellation();
    Q_INVOKABLE bool observeBchRefundRelease();
    QVariantMap bchFundingReceiptStatus() const;
    QVariantMap bchNativePreparationStatus() const;
    QVariantMap bchNativeRelayStatus() const;
    QVariantMap bchNativeProofStatus() const;
    bool configureBchNativeObservation(const QString &path);
    QVariantMap bchNativeObservationStatus() const;
    // XMZ<->BNB/ETH trade (prototype): pinned helper config, plus a private file holding the
    // wallet seed until the vault supplies it. Steps run every 20 s while the trade is open,
    // so protective steps never wait for a click; locking needs the user's approval.
    bool configureEvmTrade(const QString &config,const QString &seedFile);
    QVariantMap evmTradeStatus() const;
    Q_INVOKABLE bool refreshEvmTrade();
    Q_INVOKABLE bool approveEvmTrade(const QString &commitment);
    // After the trade: pay what its account holds to the user's own wallet (e.g. MetaMask)
    Q_INVOKABLE bool payoutEvmTrade(const QString &address);
    Q_INVOKABLE bool requestBchNativeObservation();
    bool configureBchPublicationRepair(const QString &path);
    QVariantMap bchPublicationRepairStatus() const;
    Q_INVOKABLE bool requestBchPublicationRepairReview(const QString &stage);
    Q_INVOKABLE bool submitBchPublicationRepair(const QString &commitment);
    bool configureBchSuccessPreparation(const QString &path);
    QVariantMap bchSuccessPreparationStatus() const;
    Q_INVOKABLE bool submitBchSuccessPreparation(const QString &commitment);
    Q_INVOKABLE bool requestBchSuccessPreparationCheck();
    bool configureBchSuccessReleaseReview(const QString &path);
    QVariantMap bchSuccessReleaseReviewStatus() const;
    Q_INVOKABLE bool requestBchSuccessReleaseReview();
    bool configureBchSuccessSignedReleaseReview(const QString &path);
    QVariantMap bchSuccessSignedReleaseReviewStatus() const;
    Q_INVOKABLE bool requestBchSuccessSignedReleaseReview();
    bool configureBchSuccessSignatureReview(const QString &path);
    QVariantMap bchSuccessSignatureReviewStatus() const;
    Q_INVOKABLE bool requestBchSuccessSignatureReview();
    QVariantMap bchRefundProtectionStatus() const;
    QVariantMap bchRecoveryRetentionStatus() const;
    QVariantMap bchCancellationStatus() const;
    QVariantMap bchRefundReleaseStatus() const;
    Q_INVOKABLE bool requestBchFundingReceiptReview();
    Q_INVOKABLE bool requestBchNativePreparationReview();
    Q_INVOKABLE bool requestBchNativeRelayReview();
    Q_INVOKABLE bool requestBchNativeProofReview();
    Q_INVOKABLE bool requestBchRefundProtectionReview();
    Q_INVOKABLE bool requestBchRecoveryRetentionReview();
    Q_INVOKABLE bool requestBchCancellationReview();
    Q_INVOKABLE bool requestBchRefundReleaseReview();
    Q_INVOKABLE bool submitBchFundingReceipt(const QString &commitment);
    Q_INVOKABLE bool submitBchNativePreparation(const QString &commitment);
    Q_INVOKABLE bool submitBchNativeRelay(const QString &commitment);
    Q_INVOKABLE bool submitBchNativeProof(const QString &commitment);
    Q_INVOKABLE bool submitBchRefundProtection(const QString &commitment);
    Q_INVOKABLE bool submitBchRecoveryRetention(const QString &commitment);
    Q_INVOKABLE bool submitBchCancellation(const QString &commitment);
    Q_INVOKABLE bool submitBchRefundRelease(const QString &commitment);
    Q_INVOKABLE bool requestBchFundingReceiptCheck();
    Q_INVOKABLE bool requestBchNativePreparationCheck();
    Q_INVOKABLE bool requestBchNativeRelayCheck();
    Q_INVOKABLE bool requestBchNativeProofCheck();
    Q_INVOKABLE bool requestBchRefundProtectionCheck();
    Q_INVOKABLE bool requestBchRecoveryRetentionCheck();
    Q_INVOKABLE bool requestBchCancellationCheck();
    Q_INVOKABLE bool requestBchRefundReleaseCheck();
    bool configureBchAdmission(const QString &path);
    QVariantMap bchLightFundingStatus() const;
    Q_INVOKABLE bool useBchLightWallet();
    Q_INVOKABLE bool selectBchLightFundingSource();
    Q_INVOKABLE bool queryBchLightTerms();
    Q_INVOKABLE bool prepareBchLightFunding(const QString &feeAtomic,int confirmations=1);
    Q_INVOKABLE bool reopenBchLightFunding();
    Q_INVOKABLE bool reviewBchLightSignature();
    Q_INVOKABLE bool signBchLightFunding(const QString &commitment,const QString &password);
    Q_INVOKABLE bool reviewBchLightRecovery();
    Q_INVOKABLE bool retainBchLightRecovery(const QString &commitment);
    Q_INVOKABLE bool reviewBchLightSend();
    Q_INVOKABLE bool sendBchLightFunding(const QString &commitment);
    Q_INVOKABLE bool observeBchLightFunding();
    Q_INVOKABLE bool reviewBchLightRetry();
    Q_INVOKABLE bool retryBchLightFunding(const QString &commitment);
    Q_INVOKABLE bool reconcileBchLightAcknowledgement();
    QVariantMap bchAdmissionStatus() const;
    Q_INVOKABLE bool requestBchAdmissionReview();
    Q_INVOKABLE bool submitBchAdmission(const QString &commitment);
    Q_INVOKABLE bool requestBchAdmissionCheck();
    bool configureLitecoinApp(const QString &path,const QString &expectedDigest=QString());
    QVariantMap litecoinAppStatus() const;
    QVariantMap litecoinFundingStatus() const;
    QVariantMap litecoinPaymentStatus() const;
    Q_INVOKABLE bool requestLitecoinPhaseCheck(const QString &phase);
    QVariantMap litecoinRedemptionStatus() const;
    bool configureLitecoinRedemption(const QString &path);
    Q_INVOKABLE bool reviewLitecoinRedemptionTransaction();
    Q_INVOKABLE bool submitLitecoinRedemptionTransaction(const QString &commitment);
    Q_INVOKABLE bool checkLitecoinRedemptionTransaction();
    QVariantMap litecoinJournalStatus() const;
    QVariantMap litecoinNegotiationStatus() const;
    Q_INVOKABLE bool startLitecoinRedemption(const QString &approvedFundingTxid);
    bool attachLitecoinNegotiation(LitecoinNegotiationSession *session);
#ifdef MONZERO_DEX_TESTING
    bool dropLitecoinTransportForTesting();
    int litecoinReconnectsForTesting() const;
#endif
    bool startLitecoinRecovery(const QByteArray &reservedPsbt);
    QVariantMap litecoinLightFundingStatus() const;
    Q_INVOKABLE bool useLitecoinLightWallet();
    Q_INVOKABLE bool queryLitecoinLightTerms();
    Q_INVOKABLE bool prepareLitecoinLightFunding(const QString &feeAtomic,int confirmations=1);
    Q_INVOKABLE bool reopenLitecoinLightFunding();
    Q_INVOKABLE bool resumeLitecoinLightRecovery();
    Q_INVOKABLE bool reviewLitecoinLightSignature();
    Q_INVOKABLE bool signLitecoinLightFunding(const QString &commitment,const QString &password);
    Q_INVOKABLE bool reviewLitecoinLightSend();
    Q_INVOKABLE bool sendLitecoinLightFunding(const QString &commitment);
    Q_INVOKABLE bool observeLitecoinLightFunding();
    Q_INVOKABLE bool reviewLitecoinLightRetry();
    Q_INVOKABLE bool retryLitecoinLightFunding(const QString &commitment);
    bool configureLitecoinReservation(const QString &path,const QString &journalPath);
    QVariantMap litecoinReservationStatus() const;
    Q_INVOKABLE bool prepareLitecoinReservation();
    Q_INVOKABLE bool checkLitecoinRecoverySpend(const QString &operation);
    Q_INVOKABLE bool reviewLitecoinRecoverySpend(const QString &operation);
    Q_INVOKABLE bool submitLitecoinRecoverySpend(const QString &commitment);
    Q_INVOKABLE bool reviewLitecoinLockFunding();
    Q_INVOKABLE bool submitLitecoinLockFunding(const QString &commitment);
    bool configureLitecoinJournal(const QString &path);
    Q_INVOKABLE bool requestLitecoinJournalCheck();
    Q_INVOKABLE bool requestLitecoinStartReview();
    Q_INVOKABLE bool submitLitecoinStart(const QString &commitment);
    // Operator kill switch and caps; consulted only before starting a new trade.
    DexRiskControl *riskControl() const {return m_riskControl;}
    QVariantMap dexRiskStatus() const;
    // Empty when the configured Litecoin start may proceed; otherwise a user-facing reason.
    QString litecoinStartRefusal() const;
    // Trade folders: reopen the single active Litecoin trade for this network,
    // or the requested one. Returns false only on a rejected requested trade or
    // a configuration failure; zero or several candidates load nothing.
    bool openTradeFolders(const QString &root,const QString &requestedTradeId=QString());
    Q_INVOKABLE QVariantMap reviewLitecoinTradeSetup(const QString &appPath);
    Q_INVOKABLE bool importLitecoinTradeSetup(const QString &appPath,const QString &commitment,bool confirmed);
    bool registerConfiguredLitecoinApp(const QString &root,const QString &appPath);

    QVariantList tradeFolders() const {return m_tradeFolders;}
    QString loadedTradeId() const {return m_loadedTradeId;}
    // Marks the loaded trade closed so it is not reopened automatically. The
    // folder and every file are kept; --dex-trade <id> can still reopen it.
    Q_INVOKABLE bool closeLoadedTrade(bool confirmed);
    Q_INVOKABLE bool requestLitecoinRefundReview(bool retransmit);
    Q_INVOKABLE bool requestLitecoinPayoutReview(bool retransmit);
    Q_INVOKABLE bool restoreLitecoinRecoveredWallet();
    Q_INVOKABLE bool openLitecoinRecoveredWallet();
    Q_INVOKABLE bool checkLitecoinRecoveredWallet();
    Q_INVOKABLE bool prepareLitecoinPayout();
    Q_INVOKABLE bool prepareLitecoinRefund();
    Q_INVOKABLE bool prepareLitecoinFunding();
    Q_INVOKABLE bool requestLitecoinFundingReview(bool retransmit);
    Q_INVOKABLE bool checkLitecoinFunding();
    Q_INVOKABLE bool submitLitecoinFunding(const QString &commitment);
    Q_INVOKABLE bool submitLitecoinRefund(const QString &commitment);
    Q_INVOKABLE bool checkLitecoinRefund();
    Q_INVOKABLE bool submitLitecoinPayout(const QString &commitment);
    Q_INVOKABLE bool checkLitecoinPayout();
    QVariantMap nativeFeePreview() const;
    QVariantMap nativeWalletStatus() const;
    QVariantMap nativeRecoveryStatus() const;
    QVariantMap nativeQuoteReview() const;
    QVariantMap nativeQuoteStatus() const;
    bool attachNativeQuoteCoordinator(NativeQuoteCoordinator *coordinator);
    Q_INVOKABLE bool approveNativeQuote(const QString &commitmentHex);
    // Read-only presentation attachment, native startup only. Does not take
    // ownership, launch RPC, unlock a wallet, or enable trading.
    bool attachSwapWalletSession(SwapWalletSession *session);
    Q_INVOKABLE bool cancelNativeWalletOperation();
    Q_INVOKABLE bool requestNativeRecoveryCheck();
    Q_INVOKABLE bool requestNativeFeePreview(const QString &destination, const QString &amountXmz);
    // Native backend integration only; deliberately not invokable from QML.
    NativeFeePreviewController *feePreviewController() const { return m_feePreviewController; }

    Q_INVOKABLE QVariantMap createLocalOffer(const QString &pair, const QString &side,
                                            const QString &amount, const QString &price,
                                            int lifetimeSeconds);
    Q_INVOKABLE QVariantList localOffers() const;
    QVariantMap readResearchMarket(const QString &path) const;
    Q_INVOKABLE bool refreshResearchMarket(const QString &path);
    bool researchMarketBusy() const { return m_researchBusy; }
    Q_INVOKABLE bool cancelLocalOffer(const QString &id);
    Q_INVOKABLE bool signLocalOffer(const QString &id);
    Q_INVOKABLE bool publishOffer(const QString &id);
    Q_INVOKABLE bool validateOffer(const QVariantMap& offerData) const;
    Q_INVOKABLE bool matchOffers(const QString& buyOfferId, const QString& sellOfferId);
    Q_INVOKABLE QVariantMap previewOrder(const QString &pair, const QString &side,
                                         const QString &amount, const QString &price,
                                         const QString &externalMinerFee) const;
    Q_INVOKABLE bool canTransition(OfferState from, OfferState to) const;
    Q_INVOKABLE bool canTransition(const QString &from, const QString &to) const;
    // Listed markets support unsigned research previews only.
    Q_INVOKABLE QStringList supportedPairs() const;
    Q_INVOKABLE QVariantMap marketInfo(const QString &pair) const;
    Q_INVOKABLE QVariantMap fundingPlan(const QString &payCoin, const QString &receiveCoin,
                                       const QString &unlockedXmz, const QString &protocolFeeXmz,
                                       const QString &networkFeeXmz, const QString &xmzPrincipal) const;
    Q_INVOKABLE QVariantMap selfTest() const;
    Q_INVOKABLE QVariantMap activationReadiness(const QString &pair, const QVariantMap &evidence) const;

signals:
    void bchAdmissionChanged();
    void bchLightFundingChanged();
    void bchFundingReceiptChanged();
    void bchNativePreparationChanged();
    void bchNativeRelayChanged();
    void bchNativeProofChanged();
    void bchNativeObservationChanged();
    void evmTradeChanged();
    void bchSuccessReleaseReviewChanged();
    void bchSuccessSignedReleaseReviewChanged();
    void bchSuccessSignatureReviewChanged();
    void bchSuccessPreparationChanged();
    void bchPublicationRepairChanged();
    void bchRefundProtectionChanged();
    void bchRecoveryRetentionChanged();
    void bchCancellationChanged();
    void bchPunishmentChanged();
    void bchRefundWalletChanged();
    void dexRiskChanged();
    void tradeFoldersChanged();
    void bchRefundReleaseChanged();
    void litecoinLightChanged();
    void lightWalletsChanged();
    void bchPreparationChanged();
    void bchFundingChanged();
    void litecoinStateChanged();
    void litecoinJournalChanged();
    void litecoinNegotiationChanged();
    void litecoinReservationChanged();
    void litecoinRedemptionChanged();
    void nativeFeePreviewChanged();
    void nativeWalletStatusChanged();
    void nativeRecoveryStatusChanged();
    void nativeQuoteReviewChanged();
    void researchMarketReady(const QVariantMap &snapshot);
    void researchMarketBusyChanged();
    void offerCreated(const QString& id, const QString& pair);
    void offerCancelled(const QString& id);
    void offerMatched(const QString& buyId, const QString& sellId);
    void errorOccurred(const QString& error);

private:
    bool litecoinOperationAllowed();
    bool litecoinLightFundingAllowed(bool peerRequired=false);
    bool litecoinLightFundingSelected() const;
    bool litecoinAppBusy() const;
    QVariantMap litecoinAppView(QVariantMap value) const;
    LitecoinJournalJob *litecoinFundingJob() const;
    LitecoinJournalJob *litecoinPaymentJob() const;
    LitecoinJournalJob *m_ltcFunding=nullptr,*m_ltcPayment=nullptr;
    LitecoinPeerStartup *m_ltcAppPeer=nullptr;
    QMap<QString,QByteArray> m_ltcAppSources;
    bool m_ltcAppFailed=false;
    BchFundingReceiptJob *m_bchFundingReceipt;
    BchNativePreparationJob *m_bchNativePreparation;
    BchNativeRelayJob *m_bchNativeRelay;
    BchNativeProofJob *m_bchNativeProof;
    BchNativeObservationJob *m_bchNativeObservation;
    EvmTradeJob *m_evmTrade;
    QTimer *m_evmTicker;
    QString m_evmSeedFile;
    void stepEvmTrade();
    BchSuccessReleaseReviewJob *m_bchSuccessReleaseReview;
    BchSuccessReleaseReviewJob *m_bchSuccessSignedReleaseReview;
    BchSuccessReleaseReviewJob *m_bchSuccessSignatureReview;
    BchSuccessPreparationJob *m_bchSuccessPreparation;
    BchPublicationRepairJob *m_bchPublicationRepair;
    BchRefundProtectionJob *m_bchRefundProtection;
    BchRecoveryRetentionJob *m_bchRecoveryRetention;
    BchCancellationJob *m_bchCancellation;
    BchPunishmentJob *m_bchPunishment;
    BchRefundWalletJob *m_bchRefundWallet;
    QPointer<SwapWalletRpcProcess> m_bchRefundWalletOwner;
    QPointer<SwapWalletRpcProcess> m_bchRefundWalletService;
    DexRiskControl *m_riskControl;
    QString m_tradeRoot,m_loadedTradeId;
    QVariantList m_tradeFolders;
    QVariantMap m_ltcSetupReview;
    qint64 m_ltcSetupReviewTime=0;
    QTimer *m_bchRefundWalletServiceAttach=nullptr;
    QString m_bchRefundWalletServiceState="unconfigured",m_bchRefundWalletServiceBch,m_bchRefundWalletServiceObserver;
    int m_bchRefundWalletServiceAttempts=0;
    BchRefundReleaseJob *m_bchRefundRelease;
    BchAdmissionJob *m_bchAdmission;
    BchLightFundingController *m_bchLightFunding;
    QJsonObject m_bchLightContext;
    bool m_bchLightBinding=false;
    bool bchLightFundingAllowed() const;
    bool bchOperationIdle() const;
    QVariantMap successReviewStatus(BchSuccessReleaseReviewJob *job) const;
    void advanceBchStartupChecks();
    QStringList m_bchStartupChecks;
    LitecoinLightJob *m_ltcLight;
    // Light wallet of the deployment's swap chain (Litecoin or Bitcoin).
    LitecoinLightJob *swapLight() const;
    QHash<QString,LitecoinLightJob*> m_lightWallets;
    QString m_lightSetupHelper,m_lightSetupDigest;
    QVariantMap m_lightSetupNotices;
    void rememberLightWallet(const QString &coin,const QString &directory);
    BchPreparationJob *m_bchPreparation;
    BchFundingJob *m_bchFunding;
    LitecoinJournalJob *m_ltcJournal;
    LitecoinReservationJob *m_ltcReservation;
    LitecoinLightFundingController *m_ltcLightFunding;
    LitecoinRedemptionJob *m_ltcRedemption;
    QPointer<LitecoinNegotiationSession> m_ltcNegotiation;
    NativeFeePreviewController *m_feePreviewController;
    QPointer<SwapWalletSession> m_walletSession;
    QPointer<NativeQuoteCoordinator> m_quoteCoordinator;
    bool m_sessionWasAttached = false;
    QFutureWatcher<QVariantMap> *m_researchWatcher;
    bool m_researchBusy = false;
    QVariantList m_localOffers;
    static bool parseAtomic(const QString &text, int decimals, quint64 &value);
    static QString formatAtomic(quint64 value, int decimals);
};
#endif
