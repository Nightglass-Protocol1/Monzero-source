import QtQuick 2.9
import QtTest 1.2
import "../../pages" as Pages

TestCase {
    id: test
    name: "DexRecoveryPresentation"
    // The page reads the engine only when the DEX prototype is enabled
    property bool dexPrototypeEnabled: true
    property QtObject persistentSettings: QtObject { property string dexExternalWallet: "" }
    when: windowShown
    visible: true
    width: 1280
    height: 900
    property QtObject windowSettings: QtObject {
        id: appWindow
        property bool themeTransition: false
    }
    property QtObject translations: QtObject {
        id: translationManager
        property string emptyString: ""
    }
    property QtObject backend: QtObject {
        id: dexEngine
        property var lightWallets: ({})
        property bool lightWalletSetupAvailable: false
        property var lightWalletSetupNotices: ({})
        property var setupRequest: ({})
        property var openFolderRequest: ({})
        property var backupSetupRequest: ({})
        property var backupPreviewRequest: ({})
        property var backupImportRequest: ({})
        function prepareLocalBackupRestore(coin,network,directory,backup,host,port) {backupSetupRequest={coin:coin,network:network,directory:directory,backup:backup};return true;}
        function previewLightBackupImport(coin,password) {backupPreviewRequest={coin:coin,passwordProvided:password.length>0};return true;}
        function importLightBackup(coin,digest,password,approved) {backupImportRequest={coin:coin,digest:digest,passwordProvided:password.length>0,approved:approved};return true;}
        property var helperUpgradeRequest: ({})
        function reviewLightWalletHelperUpgrade(coin,directory) {return {commitment:"reviewed-helper",previousHelper:"/old/helper",previousDigest:"old",helper:"/installed/helper",helperDigest:"new"};}
        function upgradeLightWalletHelper(coin,directory,commitment) {helperUpgradeRequest={coin:coin,directory:directory,commitment:commitment};return true;}
        function openLocalLightWallet(coin,directory) {openFolderRequest={coin:coin,directory:directory};return true;}
        function prepareLocalLightWallet(coin,network,directory,host,port,accountIndex) {setupRequest={coin:coin,network:network,directory:directory,host:host,port:port,accountIndex:accountIndex};return true;}

        property var importActivationRequest: ({})
        function activateLightWalletImport(coin,digest,password,approved) {importActivationRequest={coin:coin,digest:digest,approved:approved};return true;}
        property var importRecoveryRequest: ({})
        function recoverLightWalletImport(coin,digest,password,approved) {importRecoveryRequest={coin:coin,digest:digest,approved:approved};return true;}
        property var discoveryRequest: ({})
        function discoverLightWalletImport(coin,receiveMinimum,changeMinimum,approved) {discoveryRequest={coin:coin,receiveMinimum:receiveMinimum,changeMinimum:changeMinimum,approved:approved};return true;}
        property var importPreviewRequest: ({})
        property var importCommitRequest: ({})
        function previewLightWalletImport(coin,digest,phrase,passphrase) {importPreviewRequest={coin:coin,digest:digest};return true;}
        function importLightWalletMnemonic(coin,digest,phrase,passphrase,password,confirmation,approved) {importCommitRequest={coin:coin,digest:digest,approved:approved};return true;}
        property var mnemonicRestoreRequest: ({})
        function restoreLightWalletMnemonic(coin,digest,phrase,passphrase,password,confirmation,approved) {mnemonicRestoreRequest={coin:coin,digest:digest,approved:approved};return true;}
        property var recoveryRestoreRequest: ({})
        function restoreLightWalletRecovery(coin,digest,seed,password,confirmation,approved) {recoveryRestoreRequest={coin:coin,digest:digest,approved:approved};return true;}
        property var recoveryRevealRequest: ({})
        property string recoveryHiddenCoin: ""
        function revealLightWalletRecovery(coin,digest,password,approved) {recoveryRevealRequest={coin:coin,digest:digest,approved:approved};return true;}
        function lightWalletRecoveryPassphrase(coin) {return "test-only-passphrase";}
        function lightWalletRecoverySeed(coin) {return "test-only-recovery-value";}
        function hideLightWalletRecovery(coin) {recoveryHiddenCoin=coin;}
        property var multiCoinPayment: ({})
        property var multiCoinSigning: ({})
        property var multiCoinSend: ({})
        function prepareLightPayment(coin,payment) {multiCoinPayment={coin:coin,payment:payment};return true;}
        function signLightPayment(coin,commitment,password) {multiCoinSigning={coin:coin,commitment:commitment,password:password};return true;}
        function sendLightPayment(coin,commitment,txid,endpoint) {multiCoinSend={coin:coin,commitment:commitment,txid:txid,endpoint:endpoint};return true;}
        property var multiCoinCreation: ({})
        property string multiCoinReceived: ""
        function createLightWallet(coin,digest,password,confirmation) { multiCoinCreation={coin:coin,digest:digest,password:password,confirmation:confirmation};return true; }
        function receiveLightWallet(coin) {multiCoinReceived=coin;return true;}
        property var litecoinLightStatus: ({configured:false})
        property int lightRequestLoads: 0
        function loadLitecoinPaymentRequests() {lightRequestLoads++;return true;}
        property var lightSignedRecoveryRequest: ({})
        property bool lightSignedRecoveryAccepted: true
        function recoverSignedLitecoinPayment(requestId,payment,review) {lightSignedRecoveryRequest={requestId:requestId,payment:payment,review:review};return lightSignedRecoveryAccepted;}
        property var lightSigningRequest: ({})
        property bool lightSigningAccepted: true
        function signReviewedLitecoinPayment(commitment,password) {lightSigningRequest={commitment:commitment,password:password};return lightSigningAccepted;}
        property var lightCreateRequest: ({})
        property bool lightCreateAccepted: true
        property int lightCreatedReviews: 0
        property bool lightCreatedReviewAccepted: true
        property string lightCreatedOpenDigest: ""
        property bool lightCreatedOpenAccepted: true
        function createLitecoinWallet(digest,password,confirmation) {lightCreateRequest={digest:digest,password:password,confirmation:confirmation};return lightCreateAccepted;}
        function reviewCreatedLitecoinWallet() {lightCreatedReviews++;return lightCreatedReviewAccepted;}
        function openCreatedLitecoinWallet(digest) {lightCreatedOpenDigest=digest;return lightCreatedOpenAccepted;}
        property var lightRestoreRequest: ({})
        property bool lightRestoreAccepted: true
        function restoreLitecoinWallet(directory,digest,password) {lightRestoreRequest={directory:directory,digest:digest,password:password};return lightRestoreAccepted;}
        property var lightBackupRequest: ({})
        property bool lightBackupAccepted: true
        function backupLitecoinWallet(directory,password,save) {lightBackupRequest={directory:directory,password:password,save:save};return lightBackupAccepted;}
        property var lightReconcileRequest: ({})
        property bool lightReconcileAccepted: true
        function reconcileLitecoinPayment(requestId,payment,review,headers,history) {
            lightReconcileRequest={requestId:requestId,payment:payment,review:review,headers:headers,history:history};return lightReconcileAccepted;
        }
        property var lightSendStatusRequest: ({})
        property bool lightSendStatusAccepted: true
        property var lightSendRequest: ({})
        property bool lightSendAccepted: true
        function checkLitecoinPaymentSendStatus(requestId,payment,review,gap,addresses,entries) {lightSendStatusRequest={requestId:requestId,payment:payment,review:review,scan:[gap,addresses,entries]};return lightSendStatusAccepted;}
        function sendReviewedLitecoinPayment(commitment,txid,endpoint) {lightSendRequest={commitment:commitment,txid:txid,endpoint:endpoint};return lightSendAccepted;}
        property var lightReopenRequest: ({})
        property bool lightReopenAccepted: true
        function reopenLitecoinPayment(requestId,payment,review,gap,addresses,entries) {lightReopenRequest={requestId:requestId,payment:payment,review:review,scan:[gap,addresses,entries]};return lightReopenAccepted;}
        property var lightFeeRequest: ({})
        property bool lightFeeAccepted: true
        function estimateLitecoinPaymentFee(quote,gap,addresses,entries) {lightFeeRequest={quote:quote,scan:[gap,addresses,entries]};return lightFeeAccepted;}
        property var lightPaymentRequest: ({})
        property bool lightPaymentAccepted: true
        function prepareCurrentLitecoinPayment(payment,gap,addresses,entries) {lightPaymentRequest={payment:payment,scan:[gap,addresses,entries]};return lightPaymentAccepted;}
        property int lightReceives: 0
        function showLitecoinReceivingAddress() {lightReceives++;return true;}
        property int lightWalletInits: 0
        property var lightWalletScan: []
        function initializeLitecoinLightWallet() {lightWalletInits++;return true;}
        function refreshLitecoinLightWallet(gap,addresses,entries) {lightWalletScan=[gap,addresses,entries];return true;}
        property int lightLoads: 0
        property int lightSyncs: 0
        property int lightCancels: 0
        property int lightBudget: 0
        function requestLitecoinLightStatus() {lightLoads++;return true;}
        function synchronizeLitecoinLight(budget) {lightSyncs++;lightBudget=budget;return true;}
        function cancelLitecoinLight() {lightCancels++;}
        property var bchPreparationStatus: ({configured:false})
        property int bchRecoveryReviews: 0
        property string bchImportedCommitment: ""
        function requestBchSigningRecoveryReview() {bchRecoveryReviews++;return true;}
        function importBchSigningRecovery(commitment) {bchImportedCommitment=commitment;return true;}
        property int bchSigningReviews: 0
        property int bchSigningChecks: 0
        property string bchSignedCommitment: ""
        function requestBchSigningReview() {bchSigningReviews++;return true;}
        function requestBchSigningCheck() {bchSigningChecks++;return true;}
        function submitBchSigning(commitment) {bchSignedCommitment=commitment;return true;}
        property int bchLockReviews: 0
        property int bchLockChecks: 0
        property string bchLockedCommitment: ""
        function requestBchLockReview() {bchLockReviews++;return true;}
        function requestBchLockCheck() {bchLockChecks++;return true;}
        function submitBchLock(commitment) {bchLockedCommitment=commitment;return true;}
        property int bchPrepares: 0
        property int bchPreparationChecks: 0
        function prepareBchFunding() {bchPrepares++;return true;}
        function requestBchPreparationCheck() {bchPreparationChecks++;return true;}
        property var bchFundingStatus: ({configured:false})
        property int bchFundingReviews: 0
        property int bchFundingObservations: 0
        function requestBchFundingObservation() {bchFundingObservations++;return true;}
        property int bchFundingChecks: 0
        property string bchFundingSubmitted: ""
        function requestBchFundingCheck() {bchFundingChecks++;return true;}
        function submitBchFunding(commitment) {bchFundingSubmitted=commitment;return true;}
        function requestBchFundingReview() {bchFundingReviews++;return true;}
        property var bchNativePreparationStatus: ({configured:false})
        property int bchNativePreparationChecks: 0
        property int bchNativePreparationReviews: 0
        property string bchNativePreparationSubmitted: ""
        function requestBchNativePreparationCheck() { bchNativePreparationChecks++; return true; }
        function requestBchNativePreparationReview() { bchNativePreparationReviews++; return true; }
        function submitBchNativePreparation(commitment) { bchNativePreparationSubmitted=commitment; return true; }
        property var bchNativeRelayStatus: ({configured:false})
        property int bchNativeRelayChecks: 0
        property int bchNativeRelayReviews: 0
        property string bchNativeRelaySubmitted: ""
        function requestBchNativeRelayCheck() { bchNativeRelayChecks++; return true; }
        function requestBchNativeRelayReview() { bchNativeRelayReviews++; return true; }
        function submitBchNativeRelay(commitment) { bchNativeRelaySubmitted=commitment; return true; }
        property var bchPublicationRepairStatus: ({configured:false})
        property string bchPublicationRepairStage: ""
        property string bchPublicationRepairApproval: ""
        function requestBchPublicationRepairReview(stage) {bchPublicationRepairStage=stage;return true;}
        function submitBchPublicationRepair(approval) {bchPublicationRepairApproval=approval;return true;}
        property var bchSuccessPreparationStatus: ({configured:false})
        property int bchSuccessPreparationChecks: 0
        function requestBchSuccessPreparationCheck() {bchSuccessPreparationChecks++;return true;}
        property string bchSuccessPreparationCommitment: ""
        function submitBchSuccessPreparation(commitment) { bchSuccessPreparationCommitment=commitment; return true; }
        property var bchSuccessSignatureReviewStatus: ({configured:false})
        property var bchSuccessSignedReleaseReviewStatus: ({configured:false})
        property int bchSuccessSignatureReviewRequests: 0
        property int bchSuccessSignedReleaseReviewRequests: 0
        function requestBchSuccessSignatureReview() { bchSuccessSignatureReviewRequests++; return true; }
        function requestBchSuccessSignedReleaseReview() { bchSuccessSignedReleaseReviewRequests++; return true; }
        property var bchSuccessReleaseReviewStatus: ({configured:false})
        property int bchSuccessReleaseReviewRequests: 0
        function requestBchSuccessReleaseReview() { bchSuccessReleaseReviewRequests++; return true; }
        property var bchNativeObservationStatus: ({configured:false})
        property int bchNativeObservationChecks: 0
        function requestBchNativeObservation() { bchNativeObservationChecks++; return true; }
        property var bchNativeProofStatus: ({configured:false})
        property int bchNativeProofChecks: 0
        property int bchNativeProofReviews: 0
        property string bchNativeProofSubmitted: ""
        function requestBchNativeProofCheck() { bchNativeProofChecks++; return true; }
        function requestBchNativeProofReview() { bchNativeProofReviews++; return true; }
        function submitBchNativeProof(commitment) { bchNativeProofSubmitted=commitment; return true; }
        property var bchFundingReceiptStatus: ({configured:false})
        property int bchFundingReceiptChecks: 0
        property int bchFundingReceiptReviews: 0
        property string bchFundingReceiptSubmitted: ""
        property bool bchFundingReceiptAccepts: true
        function requestBchFundingReceiptCheck() { bchFundingReceiptChecks++; return bchFundingReceiptAccepts; }
        function requestBchFundingReceiptReview() { bchFundingReceiptReviews++; return bchFundingReceiptAccepts; }
        function submitBchFundingReceipt(commitment) { bchFundingReceiptSubmitted=commitment; return bchFundingReceiptAccepts; }
        property var bchRefundProtectionStatus: ({configured:false})
        property int bchRefundProtectionChecks: 0
        property int bchRefundProtectionReviews: 0
        property string bchRefundProtectionSubmitted: ""
        property bool bchRefundProtectionAccepts: true
        function requestBchRefundProtectionCheck() { bchRefundProtectionChecks++; return bchRefundProtectionAccepts; }
        function requestBchRefundProtectionReview() { bchRefundProtectionReviews++; return bchRefundProtectionAccepts; }
        function submitBchRefundProtection(commitment) { bchRefundProtectionSubmitted=commitment; return bchRefundProtectionAccepts; }
        property var bchRecoveryRetentionStatus: ({configured:false})
        property int bchRecoveryRetentionChecks: 0
        property int bchRecoveryRetentionReviews: 0
        property string bchRecoveryRetentionSubmitted: ""
        property bool bchRecoveryRetentionAccepts: true
        function requestBchRecoveryRetentionCheck() { bchRecoveryRetentionChecks++; return bchRecoveryRetentionAccepts; }
        function requestBchRecoveryRetentionReview() { bchRecoveryRetentionReviews++; return bchRecoveryRetentionAccepts; }
        function submitBchRecoveryRetention(commitment) { bchRecoveryRetentionSubmitted=commitment; return bchRecoveryRetentionAccepts; }
        property var bchRefundWalletStatus: ({configured:false})
        property string bchRefundWalletReviewAction: ""
        function requestBchRefundWalletReview(action) {bchRefundWalletReviewAction=action;return bchRefundWalletAccepts;}
        property var bchRefundWalletSubmitted: ({})
        function approveBchRefundWallet(commitment,password,confirmation,approved) {
            bchRefundWalletSubmitted={commitment:commitment,password:password,confirmation:confirmation,approved:approved};return bchRefundWalletAccepts;
        }
        property int bchRefundWalletChecks: 0
        property bool bchRefundWalletAccepts: true
        function requestBchRefundWalletCheck() {bchRefundWalletChecks++;return bchRefundWalletAccepts;}
        property var bchPunishmentStatus: ({configured:false})
        property int bchPunishmentChecks: 0
        property string bchPunishmentSubmitted: ""
        function submitBchPunishment(commitment) {bchPunishmentSubmitted=commitment;return bchPunishmentAccepts;}
        property int bchPunishmentObservations: 0
        function observeBchPunishment() {bchPunishmentObservations++;return bchPunishmentAccepts;}
        property int bchPunishmentReviews: 0
        property bool bchPunishmentAccepts: true
        function requestBchPunishmentCheck() {bchPunishmentChecks++;return bchPunishmentAccepts;}
        function requestBchPunishmentReview() {bchPunishmentReviews++;return bchPunishmentAccepts;}
        property var bchCancellationStatus: ({configured:false})
        property int bchCancellationChecks: 0
        property int bchCancellationReviews: 0
        property string bchCancellationSubmitted: ""
        property bool bchCancellationAccepts: true
        function requestBchCancellationCheck() { bchCancellationChecks++; return bchCancellationAccepts; }
        function requestBchCancellationReview() { bchCancellationReviews++; return bchCancellationAccepts; }
        function submitBchCancellation(commitment) { bchCancellationSubmitted=commitment; return bchCancellationAccepts; }
        property int bchCancellationObservations: 0
        function observeBchCancellation() { bchCancellationObservations++; return bchCancellationAccepts; }
        property var bchRefundReleaseStatus: ({configured:false})
        property int bchRefundReleaseChecks: 0
        property int bchRefundReleaseReviews: 0
        property string bchRefundReleaseSubmitted: ""
        property bool bchRefundReleaseAccepts: true
        function requestBchRefundReleaseCheck() { bchRefundReleaseChecks++; return bchRefundReleaseAccepts; }
        function requestBchRefundReleaseReview() { bchRefundReleaseReviews++; return bchRefundReleaseAccepts; }
        function submitBchRefundRelease(commitment) { bchRefundReleaseSubmitted=commitment; return bchRefundReleaseAccepts; }
        property int bchRefundReleaseObservations: 0
        function observeBchRefundRelease() { bchRefundReleaseObservations++; return bchRefundReleaseAccepts; }
        property var bchLightAction: ({})
        function useBchLightWallet() {bchLightAction={operation:"configure"};return true;}
        function selectBchLightFundingSource() {bchLightAction={operation:"select"};return true;}
        function queryBchLightTerms() {bchLightAction={operation:"terms"};return true;}
        function prepareBchLightFunding(fee,confirmations) {bchLightAction={operation:"prepare",fee:fee,confirmations:confirmations};return true;}
        function reopenBchLightFunding() {bchLightAction={operation:"reopen"};return true;}
        function reviewBchLightSignature() {bchLightAction={operation:"review"};return true;}
        function signBchLightFunding(commitment,password) {bchLightAction={operation:"sign",commitment:commitment,passwordProvided:password.length>0};return true;}
        function reviewBchLightRecovery() {bchLightAction={operation:"recovery-review"};return true;}
        function retainBchLightRecovery(commitment) {bchLightAction={operation:"recovery-retain",commitment:commitment};return true;}
        function reviewBchLightSend() {bchLightAction={operation:"send-review"};return true;}
        function sendBchLightFunding(commitment) {bchLightAction={operation:"send",commitment:commitment};return true;}
        function observeBchLightFunding() {bchLightAction={operation:"observe"};return true;}
        function reviewBchLightRetry() {bchLightAction={operation:"retry-review"};return true;}
        function retryBchLightFunding(commitment) {bchLightAction={operation:"retry",commitment:commitment};return true;}
        function reconcileBchLightAcknowledgement() {bchLightAction={operation:"ack"};return true;}
        property var bchLightFundingStatus: ({configured:false})
        property var bchAdmissionStatus: ({configured:false})
        property int bchReviews: 0
        property int bchChecks: 0
        property string bchSubmitted: ""
        function requestBchAdmissionReview() {bchReviews++;return true;}
        function requestBchAdmissionCheck() {bchChecks++;return true;}
        function submitBchAdmission(commitment) {bchSubmitted=commitment;return true;}
        property var nativeWalletStatus: ({attached:true,state:"open",readyForTrading:false})
        property var litecoinNegotiationStatus: ({attached: false, state: "unattached", executable: false})
        property var litecoinRedemptionStatus: ({configured:false})
        property int redemptionReviews: 0
        property int redemptionChecks: 0
        property string submittedRedemption: ""
        function reviewLitecoinRedemptionTransaction() {redemptionReviews++;return true;}
        function checkLitecoinRedemptionTransaction() {redemptionChecks++;return true;}
        function submitLitecoinRedemptionTransaction(commitment) {submittedRedemption=commitment;return true;}
        property var litecoinLightFundingStatus: ({configured:false})
        property var lightDexAction: ({})
        function useLitecoinLightWallet() {lightDexAction={operation:"select"};return true;}
        function queryLitecoinLightTerms() {lightDexAction={operation:"terms"};return true;}
        function prepareLitecoinLightFunding(fee,confirmations) {lightDexAction={operation:"prepare",fee:fee,confirmations:confirmations};return true;}
        function resumeLitecoinLightRecovery() {lightDexAction={operation:"resume-recovery"};return true;}
        function reopenLitecoinLightFunding() {lightDexAction={operation:"reopen"};return true;}
        function reviewLitecoinLightSignature() {lightDexAction={operation:"review"};return true;}
        function signLitecoinLightFunding(commitment,password) {lightDexAction={operation:"sign",commitment:commitment,passwordProvided:password.length>0};return true;}
        function reviewLitecoinLightSend() {lightDexAction={operation:"send-review"};return true;}
        function sendLitecoinLightFunding(commitment) {lightDexAction={operation:"send",commitment:commitment};return true;}
        function observeLitecoinLightFunding() {lightDexAction={operation:"observe"};return true;}
        function reviewLitecoinLightRetry() {lightDexAction={operation:"retry-review"};return true;}
        function retryLitecoinLightFunding(commitment) {lightDexAction={operation:"retry",commitment:commitment};return true;}
        property var litecoinReservationStatus: ({configured:false})
        property string redemptionApproval: "unset"
        function startLitecoinRedemption(txid) {redemptionApproval=txid;return true;}
        property string recoveryKind: ""
        property string recoveryCheckKind: ""
        function checkLitecoinRecoverySpend(kind) {recoveryCheckKind=kind;return true;}
        property string recoveryToken: ""
        function reviewLitecoinRecoverySpend(kind) { recoveryKind=kind; return true; }
        function submitLitecoinRecoverySpend(token) { recoveryToken=token; return true; }
        property int lockReviews: 0
        property string submittedLock: ""
        function reviewLitecoinLockFunding() { lockReviews++; return true; }
        function submitLitecoinLockFunding(commitment) { submittedLock = commitment; return true; }
        property int reservationRequests: 0
        function prepareLitecoinReservation() { reservationRequests++; return true; }
        property var litecoinJournalStatus: ({attached:false,busy:false,valid:false})
        property var litecoinAppStatus: ({configured:false})
        property var litecoinFundingStatus: litecoinJournalStatus
        property var litecoinPaymentStatus: litecoinJournalStatus
        property string requestedPhase: ""
        function requestLitecoinPhaseCheck(phase) {requestedPhase=phase;return true;}
        property int startReviews: 0
        property string litecoinStartRefusal: ""
        property var tradeFolders: []
        property string loadedTradeId: ""
        property int closeRequests: 0
        function closeLoadedTrade(confirmed) { if (confirmed) closeRequests++; return confirmed; }
        property string submittedStart: ""
        function requestLitecoinStartReview() { startReviews++; return true; }
        function submitLitecoinStart(commitment) { submittedStart = commitment; return true; }
        property int journalRequests: 0
        function requestLitecoinJournalCheck() { journalRequests++; return true; }
        property bool refundReviewRetransmit: false
        property bool fundingReviewRetransmit: false
        function requestLitecoinFundingReview(retransmit) { fundingReviewRetransmit = retransmit; return true; }
        property bool payoutReviewRetransmit: false
        function requestLitecoinPayoutReview(retransmit) { payoutReviewRetransmit = retransmit; return true; }
        function requestLitecoinRefundReview(retransmit) { refundReviewRetransmit = retransmit; return true; }
        property int refundChecks: 0
        function checkLitecoinRefund() { refundChecks++; return true; }
        property int walletRestores: 0
        property int walletOpens: 0
        property int walletChecks: 0
        function restoreLitecoinRecoveredWallet() { walletRestores++; return true; }
        function openLitecoinRecoveredWallet() { walletOpens++; return true; }
        function checkLitecoinRecoveredWallet() { walletChecks++; return true; }
        property int payoutPreparations: 0
        property int refundPreparations: 0
        function prepareLitecoinPayout() { payoutPreparations++; return true; }
        function prepareLitecoinRefund() { refundPreparations++; return true; }
        property int fundingPreparations: 0
        function prepareLitecoinFunding() { fundingPreparations++; return true; }
        property int fundingChecks: 0
        function checkLitecoinFunding() { fundingChecks++; return true; }
        property int payoutChecks: 0
        function checkLitecoinPayout() { payoutChecks++; return true; }
        property string submittedRefund: ""
        function submitLitecoinRefund(commitment) { submittedRefund = commitment; return true; }
        property string submittedFunding: ""
        function submitLitecoinFunding(commitment) { submittedFunding = commitment; return true; }
        property string submittedPayout: ""
        function submitLitecoinPayout(commitment) { submittedPayout = commitment; return true; }
        property var nativeFeePreview: ({valid:false})
        property var nativeRecoveryStatus: ({valid:false,retryAllowed:false})
        property int recoveryRequests: 0
        function requestNativeRecoveryCheck() { recoveryRequests++; return true; }
        property var nativeQuoteReview: ({})
        property var nativeQuoteStatus: ({state:"unattached",executable:false})
        property string approvedCommitment: ""
        function approveNativeQuote(commitment) { approvedCommitment = commitment; return true; }
        property bool researchMarketBusy: false
        signal researchMarketReady(var snapshot)
        function activationReadiness(pair, evidence) { return {ready:false,checksPassed:0,checksRequired:2,blockers:["Test backend"]}; }
        function localOffers() { return []; }
        function supportedPairs() { return ["BTC / XMZ", "LTC / XMZ", "BCH / XMZ", "XMR / XMZ", "DOGE / XMZ", "ETH / XMZ", "RUNE / XMZ"]; }
        function marketInfo(pair) {
            var symbol = String(pair).split(" / ")[0];
            return {symbol:symbol, decimals:symbol === "ETH" ? 18 : symbol === "XMR" ? 12 : 8,
                executable:false,settlementStatus:"unimplemented",settlementBlocker:symbol + " settlement unavailable"};
        }
    }
    Pages.Dex { id: page; width: test.width; height: test.height; showAdvanced: true }
    function test_litecoinOverviewRejectsIncompleteObservations() {
        page.selectPair("LTC / XMZ")
        var balance=findChild(page,"litecoinWalletOverviewBalance"),address=findChild(page,"litecoinOverviewAddress")
        for (var i=0;i<3;i++) {
            dexEngine.litecoinLightStatus={configured:true,state:"wallet-checked",wallet:{balanceStatus:"provisional",watchOnly:true,spendable:false,observedConfirmedAtomic:[undefined,"invalid",12][i]}}
            compare(balance.text,"No current balance available · refresh to check")
        }
        dexEngine.litecoinLightStatus={configured:true,busy:true,state:"wallet-checked",wallet:{balanceStatus:"provisional",watchOnly:true,spendable:false,observedConfirmedAtomic:"0"}}
        compare(balance.text,"Wallet operation in progress…")
        dexEngine.litecoinLightStatus={configured:true,state:"wallet-checked",wallet:{balanceStatus:"provisional",watchOnly:true,spendable:false,observedConfirmedAtomic:"0"}}
        verify(balance.text.indexOf("0.00000000 LTC")>=0)
        dexEngine.litecoinLightStatus={configured:true,state:"wallet-address",wallet:{watchOnly:true,spendable:false,address:"fixture-address"}}
        verify(!address.visible);compare(address.text,"")
        dexEngine.litecoinLightStatus={configured:true,network:"regtest",state:"wallet-address",wallet:{watchOnly:true,spendable:false,address:""}}
        verify(!address.visible);compare(address.text,"")
        dexEngine.litecoinLightStatus={configured:false};page.selectPair("BTC / XMZ")
    }
    function test_litecoinContinueCreation() {
        page.selectPair("LTC / XMZ")
        dexEngine.litecoinLightStatus={configured:true,setupConfigured:true,walletCreateAvailable:true,network:"regtest",creation:{}}
        var button=findChild(page,"litecoinOverviewCreate")
        verify(button.visible);verify(button.enabled);compare(button.text,"Continue setup")
        button.clicked();verify(findChild(page,"litecoinLightCreatePassword").input.activeFocus)
        dexEngine.litecoinLightStatus={configured:true,setupConfigured:true,busy:true}
        verify(!button.enabled)
        dexEngine.litecoinLightStatus={configured:true,setupConfigured:false}
        verify(!button.visible)
        dexEngine.litecoinLightStatus={configured:false};page.selectPair("BTC / XMZ")
    }
    function test_litecoinOverviewActions() {
        page.selectPair("LTC / XMZ")
        dexEngine.litecoinLightStatus={configured:true,walletReceiveAvailable:true,walletRefreshAvailable:true,syncAvailable:true}
        var receive=findChild(page,"litecoinOverviewReceive"),refresh=findChild(page,"litecoinOverviewRefresh"),sync=findChild(page,"litecoinOverviewSync")
        var oldReceives=dexEngine.lightReceives,oldSyncs=dexEngine.lightSyncs
        verify(receive.enabled);verify(refresh.enabled);verify(sync.enabled)
        receive.clicked();compare(dexEngine.lightReceives,oldReceives+1)
        refresh.clicked();compare(dexEngine.lightWalletScan,[20,500,1000])
        sync.clicked();compare(dexEngine.lightSyncs,oldSyncs+1);compare(dexEngine.lightBudget,10000000)
        dexEngine.litecoinLightStatus={configured:true,busy:true,walletReceiveAvailable:true,walletRefreshAvailable:true,syncAvailable:true}
        verify(!receive.enabled);verify(!refresh.enabled);verify(!sync.enabled)
        receive.clicked();sync.clicked();compare(dexEngine.lightReceives,oldReceives+1);compare(dexEngine.lightSyncs,oldSyncs+1)
        dexEngine.litecoinLightStatus={configured:true,network:"ltc-regtest",state:"wallet-address",wallet:{watchOnly:true,spendable:false,address:"fixture-address"}}
        var address=findChild(page,"litecoinOverviewAddress");verify(address.visible);verify(address.text.indexOf("fixture-address")>=0)
        dexEngine.litecoinLightStatus={configured:true,state:"unknown",wallet:{watchOnly:true,spendable:false,address:"fixture-address"}}
        verify(!address.visible);compare(address.text,"")
        dexEngine.litecoinLightStatus={configured:false};page.selectPair("BTC / XMZ")
    }
    function test_litecoinWalletVisibleBeforeSetup() {
        dexEngine.litecoinLightStatus={configured:false};dexEngine.lightWalletSetupAvailable=true
        page.selectPair("BTC / XMZ")
        var card=findChild(page,"litecoinWalletOverview"),open=findChild(page,"litecoinWalletOpenMarket")
        verify(card.visible);verify(open.visible);open.clicked();compare(page.baseCoin,"LTC")
        verify(findChild(page,"litecoinWalletOverviewStatus").text.indexOf("No Litecoin wallet open")>=0)
        verify(findChild(page,"lightWalletSetupPanel").visible)
        var create=findChild(page,"litecoinOverviewCreate");verify(create.visible);verify(create.enabled)
        var oldParent=page.parent
        var scroll=Qt.createQmlObject('import QtQuick 2.9; Flickable { width:1280; height:300; contentHeight:10000 }',test)
        page.parent=scroll.contentItem
        create.clicked();verify(findChild(page,"lightSetupNetwork").activeFocus);verify(scroll.contentY>0)
        page.parent=oldParent;scroll.destroy()
        var setupCaptured=false;verify(card.grabToImage(function(result){setupCaptured=result.saveToFile("build/ltc-create-wallet-entry-20261015.png")}));tryVerify(function(){return setupCaptured},5000)
        compare(findChild(page,"litecoinWalletOverviewBalance").text,"No wallet selected")
        dexEngine.litecoinLightStatus={configured:true,network:"ltc-regtest",state:"wallet-checked",wallet:{balanceStatus:"provisional",watchOnly:true,spendable:false,observedConfirmedAtomic:"125000000"}}
        verify(findChild(page,"litecoinWalletOverviewBalance").text.indexOf("1.25000000 LTC")>=0)
        var captured=false;verify(card.grabToImage(function(result){captured=result.saveToFile("build/litecoin-wallet-overview-20261015.png")}));tryVerify(function(){return captured},5000)
        dexEngine.litecoinLightStatus={configured:true,state:"unknown",wallet:{observedConfirmedAtomic:"125000000"}}
        compare(findChild(page,"litecoinWalletOverviewBalance").text,"No current balance available · refresh to check")
        dexEngine.litecoinLightStatus={configured:false};dexEngine.lightWalletSetupAvailable=false
        verify(findChild(page,"litecoinWalletOverviewStatus").text.indexOf("unavailable")>=0)
        verify(!create.enabled)
        page.selectPair("BTC / XMZ")
    }
    function test_bchNativePreparationRequiresConsentAndShowsHistory() {
        page.selectPair("BCH / XMZ")
        dexEngine.bchNativePreparationStatus={configured:true,state:"ready",checkAvailable:true}
        var panel=findChild(page,"bchNativePreparationPanel"), check=findChild(page,"bchNativePreparationCheck")
        var review=findChild(page,"bchNativePreparationReview"), submit=findChild(page,"bchNativePreparationSubmit")
        var consent=findChild(page,"bchNativePreparationConsent"), outcome=findChild(page,"bchNativePreparationOutcome")
        wait(0);verify(panel.visible);verify(!review.enabled);verify(!submit.enabled)
        check.clicked();compare(dexEngine.bchNativePreparationChecks,1)
        dexEngine.bchNativePreparationStatus={configured:true,state:"not-attempted",reviewAvailable:true,checkAvailable:true}
        wait(0);review.clicked();compare(dexEngine.bchNativePreparationReviews,1)
        var approved={commitment:"deposit-approved",details:{swapId:"trade",wallet:"A".repeat(95),account:0,priority:1,destination:"B".repeat(95),amountAtoms:"200000000000",maximumFeeAtoms:"10000000000",safetyBlocks:5}}
        dexEngine.bchNativePreparationStatus={configured:true,state:"reviewed",review:approved,submitAvailable:true,checkAvailable:true}
        wait(0);var text=findChild(page,"bchNativePreparationDetails").text
        verify(text.indexOf("Deposit: 2 XMZ")>=0);verify(text.indexOf("Maximum funding fee: 0.1 XMZ")>=0);verify(text.indexOf("B".repeat(95))>=0)
        var grabbed=false;panel.grabToImage(function(result){verify(result.saveToFile("build/bch-native-preparation-units-panel-20261011.png"));grabbed=true});tryVerify(function(){return grabbed},5000)
        verify(!submit.enabled);consent.checked=true;tryCompare(submit,"enabled",true)
        submit.clicked();compare(dexEngine.bchNativePreparationSubmitted,"deposit-approved");verify(!consent.checked)
        consent.checked=true;page.selectPair("LTC / XMZ");verify(!consent.checked);verify(!panel.visible)
        page.selectPair("BCH / XMZ");consent.checked=true
        dexEngine.bchNativePreparationStatus={configured:true,state:"attempted",checkAvailable:true}
        tryCompare(consent,"checked",false);verify(!submit.enabled);verify(outcome.text.indexOf("replacement")>=0)
        dexEngine.bchNativePreparationStatus={configured:true,state:"prepared",review:approved,checkAvailable:true,preparation:{payment:{txid:"a".repeat(64),feeAtoms:"1386240000"}}}
        wait(0);verify(outcome.text.indexOf("a".repeat(64))>=0);verify(outcome.text.indexOf("Funding fee: 0.0138624 XMZ")>=0);verify(!submit.enabled)
        dexEngine.bchNativePreparationStatus={configured:false};page.selectPair("BTC / XMZ")
    }
    function test_bchNativeRelayRequiresConsentAndShowsHistory() {
        page.selectPair("BCH / XMZ")
        dexEngine.bchNativeRelayStatus={configured:true,state:"ready",checkAvailable:true}
        var panel=findChild(page,"bchNativeRelayPanel"), check=findChild(page,"bchNativeRelayCheck")
        var review=findChild(page,"bchNativeRelayReview"), submit=findChild(page,"bchNativeRelaySubmit")
        var consent=findChild(page,"bchNativeRelayConsent"), outcome=findChild(page,"bchNativeRelayOutcome")
        wait(0);verify(panel.visible);verify(!review.enabled);verify(!submit.enabled)
        check.clicked();compare(dexEngine.bchNativeRelayChecks,1)
        dexEngine.bchNativeRelayStatus={configured:true,state:"not-attempted",reviewAvailable:true,checkAvailable:true}
        wait(0);review.clicked();compare(dexEngine.bchNativeRelayReviews,1)
        var approved={commitment:"deposit-approved",details:{swapId:"trade",wallet:"A".repeat(95),account:0,priority:1,destination:"B".repeat(95),amountAtoms:"200000000000",maximumFeeAtoms:"10000000000",cancellationMarginBlocks:5,txid:"a".repeat(64),feeAtoms:"1386240000"}}
        dexEngine.bchNativeRelayStatus={configured:true,state:"reviewed",review:approved,submitAvailable:true,checkAvailable:true}
        wait(0);var text=findChild(page,"bchNativeRelayDetails").text
        verify(text.indexOf("a".repeat(64))>=0);verify(text.indexOf("Funding fee: 0.0138624 XMZ")>=0);verify(text.indexOf("Deposit: 2 XMZ")>=0);verify(text.indexOf("Maximum funding fee: 0.1 XMZ")>=0);verify(text.indexOf("B".repeat(95))>=0)
        var grabbed=false;panel.grabToImage(function(result){verify(result.saveToFile("build/bch-native-relay-units-panel-20261011.png"));grabbed=true});tryVerify(function(){return grabbed},5000)
        verify(!submit.enabled);consent.checked=true;tryCompare(submit,"enabled",true)
        submit.clicked();compare(dexEngine.bchNativeRelaySubmitted,"deposit-approved");verify(!consent.checked)
        consent.checked=true;page.selectPair("LTC / XMZ");verify(!consent.checked);verify(!panel.visible)
        page.selectPair("BCH / XMZ");consent.checked=true
        dexEngine.bchNativeRelayStatus={configured:true,state:"attempted",checkAvailable:true}
        tryCompare(consent,"checked",false);verify(!submit.enabled);verify(outcome.text.indexOf("replacement")>=0)
        dexEngine.bchNativeRelayStatus={configured:true,state:"acknowledged",review:approved,checkAvailable:true,relay:{state:"acknowledged",txid:"a".repeat(64)}}
        wait(0);verify(outcome.text.indexOf("a".repeat(64))>=0);verify(outcome.text.indexOf("confirmation has not been verified")>=0);verify(!submit.enabled)
        dexEngine.bchNativeRelayStatus={configured:false};page.selectPair("BTC / XMZ")
    }
    function test_bchNativeProofConsentAndUnconfirmedReceipt() {
        page.selectPair("BCH / XMZ")
        dexEngine.bchNativeProofStatus={configured:true,state:"ready",checkAvailable:true}
        var panel=findChild(page,"bchNativeProofPanel"), check=findChild(page,"bchNativeProofCheck")
        var review=findChild(page,"bchNativeProofReview"), submit=findChild(page,"bchNativeProofSubmit")
        var consent=findChild(page,"bchNativeProofConsent"), outcome=findChild(page,"bchNativeProofOutcome")
        wait(0);verify(panel.visible);verify(!review.enabled);verify(!submit.enabled)
        check.clicked();compare(dexEngine.bchNativeProofChecks,1)
        dexEngine.bchNativeProofStatus={configured:true,state:"not-saved",reviewAvailable:true,checkAvailable:true}
        wait(0);review.clicked();compare(dexEngine.bchNativeProofReviews,1)
        var approved={commitment:"proof-approved",details:{swapId:"trade",bchFundingOutpoint:"a".repeat(64)+":0",destination:"B".repeat(95),amountAtoms:"200000000000",txid:"b".repeat(64)}}
        dexEngine.bchNativeProofStatus={configured:true,state:"reviewed",review:approved,submitAvailable:true,checkAvailable:true}
        wait(0);var text=findChild(page,"bchNativeProofDetails").text
        verify(text.indexOf("b".repeat(64))>=0);verify(text.indexOf("a".repeat(64)+":0")>=0)
        verify(text.indexOf("Deposit: 2 XMZ")>=0);verify(text.indexOf("B".repeat(95))>=0)
        var grabbed=false;panel.grabToImage(function(result){verify(result.saveToFile("build/bch-native-proof-panel-20261011.png"));grabbed=true});tryVerify(function(){return grabbed},5000)
        verify(!submit.enabled);consent.checked=true;tryCompare(submit,"enabled",true)
        submit.clicked();compare(dexEngine.bchNativeProofSubmitted,"proof-approved");verify(!consent.checked)
        consent.checked=true;page.selectPair("LTC / XMZ");verify(!consent.checked);verify(!panel.visible)
        page.selectPair("BCH / XMZ");consent.checked=true
        dexEngine.bchNativeProofStatus={configured:true,state:"unknown",checkAvailable:true}
        tryCompare(consent,"checked",false);verify(!submit.enabled);verify(outcome.text.indexOf("outcome is unknown")>=0)
        dexEngine.bchNativeProofStatus={configured:true,state:"saved",review:approved,checkAvailable:true,receipt:{state:"retained",receivedAt:"1791710000"}}
        wait(0);verify(outcome.text.indexOf("confirmation has not been verified")>=0);verify(!submit.enabled)
        dexEngine.bchNativeProofStatus={configured:false};page.selectPair("BTC / XMZ")
    }
    function test_bchPublicationRepairNeedsFreshConsent() {
        page.selectPair("BCH / XMZ")
        dexEngine.bchPublicationRepairStatus={configured:true,state:"ready",reviewAvailable:true}
        var panel=findChild(page,"bchPublicationRepairPanel"), prepared=findChild(page,"bchPublicationRepairPrepared")
        var signed=findChild(page,"bchPublicationRepairSigned"), consent=findChild(page,"bchPublicationRepairConsent")
        var submit=findChild(page,"bchPublicationRepairSubmit"), outcome=findChild(page,"bchPublicationRepairOutcome")
        wait(0);verify(panel.visible);verify(prepared.enabled);verify(!submit.enabled)
        prepared.clicked();compare(dexEngine.bchPublicationRepairStage,"prepared")
        signed.clicked();compare(dexEngine.bchPublicationRepairStage,"signed")
        dexEngine.bchPublicationRepairStatus={configured:true,state:"reviewed",stage:"signed",tradeId:"trade",submitAvailable:true,review:{commitment:"approved"}}
        wait(0);verify(!submit.enabled);consent.checked=true;verify(submit.enabled)
        var captured=false
        panel.grabToImage(function(result){verify(result.saveToFile("bch-publication-repair-panel.png"));captured=true})
        tryVerify(function(){return captured},5000)
        submit.clicked();compare(dexEngine.bchPublicationRepairApproval,"approved");verify(!consent.checked)
        consent.checked=true
        dexEngine.bchPublicationRepairStatus={configured:true,state:"unknown",reviewAvailable:true}
        wait(0);verify(!consent.checked);verify(!submit.enabled);verify(outcome.text.indexOf("unknown")>=0)
        dexEngine.bchPublicationRepairStatus={configured:true,state:"repaired"}
        wait(0);verify(outcome.text.indexOf("fresh settlement checks")>=0)
        consent.checked=true;page.selectPair("LTC / XMZ");wait(0);verify(!panel.visible);verify(!consent.checked)
        dexEngine.bchPublicationRepairStatus={configured:false};page.selectPair("BTC / XMZ")
    }
    function test_bchNativeObservationShowsOnlyCurrentEvidence() {
        page.selectPair("BCH / XMZ")
        dexEngine.bchNativeObservationStatus={configured:true,state:"ready",checkAvailable:false}
        var panel=findChild(page,"bchNativeObservationPanel"), check=findChild(page,"bchNativeObservationCheck")
        var details=findChild(page,"bchNativeObservationDetails"), outcome=findChild(page,"bchNativeObservationOutcome")
        wait(0);verify(panel.visible);verify(!check.enabled);verify(!details.visible);compare(dexEngine.bchNativeObservationChecks,0)
        dexEngine.bchNativeProofStatus={configured:true,state:"saved"}
        dexEngine.bchNativeObservationStatus={configured:true,state:"ready",checkAvailable:true}
        wait(0);verify(check.enabled);check.clicked();compare(dexEngine.bchNativeObservationChecks,1)
        var observation={txid:"b".repeat(64),destination:"B".repeat(95),amountAtoms:"200000000000",confirmations:"3",height:"8",tipHeight:"10",tip:"c".repeat(64),observedAt:"1791710000"}
        dexEngine.bchNativeObservationStatus={configured:true,state:"confirmed",chainObserved:true,checkAvailable:true,observation:observation}
        wait(0);verify(details.visible);verify(details.text.indexOf("Deposit: 2 XMZ")>=0)
        verify(details.text.indexOf("Confirmations at check: 3")>=0);verify(details.text.indexOf("c".repeat(64))>=0)
        verify(outcome.text.indexOf("not settlement approval")>=0)
        var grabbed=false;panel.grabToImage(function(result){verify(result.saveToFile("build/bch-native-observation-panel-20261011.png"));grabbed=true});tryVerify(function(){return grabbed},5000)
        dexEngine.bchNativeObservationStatus={configured:true,state:"checking",busy:true,checkAvailable:false}
        wait(0);verify(!details.visible);compare(details.text,"");verify(!check.enabled)
        dexEngine.bchNativeObservationStatus={configured:true,state:"unknown",checkAvailable:true}
        wait(0);verify(!details.visible);verify(outcome.text.indexOf("Previous chain evidence has been cleared")>=0)
        page.selectPair("LTC / XMZ");verify(!panel.visible)
        dexEngine.bchNativeObservationStatus={configured:false};dexEngine.bchNativeProofStatus={configured:false};page.selectPair("BTC / XMZ")
    }
    function test_bchPreparationStatusIsSeparateFromLiveEvidenceAndConsent() {
        page.selectPair("BCH / XMZ")
        dexEngine.bchSuccessReleaseReviewStatus={configured:false}
        dexEngine.bchSuccessPreparationStatus={configured:true,state:"unknown",attempted:true,checkAvailable:true}
        var panel=findChild(page,"bchSuccessPreparationPanel"),check=findChild(page,"bchSuccessPreparationCheck")
        var outcome=findChild(page,"bchSuccessPreparationOutcome"),consent=findChild(page,"bchSuccessPreparationConsent")
        wait(0);verify(panel.visible);verify(check.enabled);compare(dexEngine.bchSuccessPreparationChecks,0)
        check.clicked();compare(dexEngine.bchSuccessPreparationChecks,1);verify(!consent.checked)
        dexEngine.bchSuccessPreparationStatus={configured:true,state:"checking",busy:true,checkAvailable:false}
        wait(0);verify(!check.enabled);verify(outcome.text.indexOf("does not check current chain state")>=0)
        dexEngine.bchSuccessPreparationStatus={configured:true,state:"saved",statusKnown:true,checkAvailable:true,preparation:{preparedAt:"1791710000",signatureRetained:true}}
        wait(0);verify(outcome.text.indexOf("Current chain state has not been checked")>=0);verify(outcome.text.indexOf("1791710000")>=0)
        verify(outcome.text.indexOf("does not prove that funds were released")>=0);verify(!consent.checked)
        var grabbed=false;panel.grabToImage(function(result){verify(result.saveToFile("build/bch-success-status-panel-20261011.png"));grabbed=true});tryVerify(function(){return grabbed},5000)
        dexEngine.bchSuccessPreparationStatus={configured:true,state:"checked",statusKnown:true,checkAvailable:true}
        wait(0);verify(outcome.text.indexOf("No saved preparation found")>=0);verify(outcome.text.indexOf("1791710000")<0)
        dexEngine.bchSuccessPreparationStatus={configured:false};page.selectPair("BTC / XMZ")
    }
    function test_bchSuccessPreparationRequiresSeparateConsentAndPreservesUnknown() {
        page.selectPair("BCH / XMZ")
        dexEngine.bchSuccessReleaseReviewStatus={configured:true,state:"reviewed",review:{commitment:"exact-reviewed-commitment"}}
        dexEngine.bchSuccessPreparationStatus={configured:true,state:"ready",submitAvailable:true}
        var consent=findChild(page,"bchSuccessPreparationConsent"),submit=findChild(page,"bchSuccessPreparationSubmit")
        var outcome=findChild(page,"bchSuccessPreparationOutcome"),panel=findChild(page,"bchSuccessPreparationPanel")
        wait(0);verify(panel.visible);verify(!submit.enabled);compare(dexEngine.bchSuccessPreparationCommitment,"")
        consent.checked=true;tryCompare(submit,"enabled",true);submit.clicked()
        compare(dexEngine.bchSuccessPreparationCommitment,"exact-reviewed-commitment");verify(!consent.checked)
        dexEngine.bchSuccessPreparationStatus={configured:true,state:"preparing",busy:true,attempted:true,submitAvailable:false}
        wait(0);verify(!submit.enabled);verify(!consent.enabled)
        dexEngine.bchSuccessPreparationStatus={configured:true,state:"prepared",attempted:true,submitAvailable:true}
        wait(0);verify(outcome.text.indexOf("Funds have not been released")>=0);verify(!submit.enabled)
        var grabbed=false;panel.grabToImage(function(result){verify(result.saveToFile("build/bch-success-preparation-panel-20261011.png"));grabbed=true});tryVerify(function(){return grabbed},5000)
        consent.checked=true;dexEngine.bchSuccessReleaseReviewStatus={configured:true,state:"ready"}
        tryCompare(consent,"checked",false);verify(!submit.enabled)
        dexEngine.bchSuccessPreparationStatus={configured:true,state:"unknown",attempted:true,submitAvailable:false}
        wait(0);verify(outcome.text.indexOf("may have been saved")>=0);verify(outcome.text.indexOf("Nothing will retry automatically")>=0)
        dexEngine.bchSuccessReleaseReviewStatus={configured:true,state:"reviewed",review:{commitment:"different-review"}}
        dexEngine.bchSuccessPreparationStatus={configured:true,state:"ready",submitAvailable:true}
        wait(0);verify(!consent.checked);consent.checked=true
        page.selectPair("LTC / XMZ");verify(!panel.visible);verify(!consent.checked)
        dexEngine.bchSuccessPreparationStatus={configured:false};dexEngine.bchSuccessReleaseReviewStatus={configured:false};page.selectPair("BTC / XMZ")
    }
    function test_bchSigningReviewsAreSeparateAndExplicit() {
        page.selectPair("BCH / XMZ")
        var review={details:{bchFundingOutpoint:"ab:0",bchPayoutAtoms:"99000",bchFeeAtoms:"1000",bchPayoutScript:"76a914…88ac",nativeTxid:"cd",nativeDestination:"F-test",nativeAmountAtoms:"200000000000",minimumBchConfirmations:2,minimumNativeConfirmations:3,cancellationMarginBlocks:5}}
        for (var i=0;i<2;i++) {
            var prefix=i===0?"bchSuccessSignatureReview":"bchSuccessSignedReleaseReview"
            dexEngine[prefix+"Status"]={configured:true,state:"ready",reviewAvailable:false}
            var panel=findChild(page,prefix+"Panel"),button=findChild(page,prefix+"Request")
            var details=findChild(page,prefix+"Details"),outcome=findChild(page,prefix+"Outcome")
            wait(0);verify(panel.visible);verify(!button.enabled);verify(!details.visible)
            compare(dexEngine[prefix+"Requests"],0)
            dexEngine[prefix+"Status"]={configured:true,state:"ready",reviewAvailable:true}
            wait(0);verify(button.enabled);button.clicked();compare(dexEngine[prefix+"Requests"],1)
            dexEngine[prefix+"Status"]={configured:true,state:"reviewed",reviewAvailable:true,review:review}
            wait(0);verify(details.visible);verify(details.text.indexOf("0.00099 BCH")>=0);verify(details.text.indexOf("0.00001 BCH")>=0);verify(details.text.indexOf("2 XMZ")>=0)
            verify(outcome.text.indexOf("own approval")>=0)
            dexEngine[prefix+"Status"]={configured:true,state:"reviewing",busy:true,reviewAvailable:false}
            wait(0);verify(!details.visible);verify(!button.enabled)
            dexEngine[prefix+"Status"]={configured:true,state:"unknown",reviewAvailable:true}
            wait(0);verify(outcome.text.indexOf("could not be verified")>=0)
            page.selectPair("BTC / XMZ");wait(0);verify(!panel.visible)
            dexEngine[prefix+"Status"]={configured:false};page.selectPair("BCH / XMZ")
        }
        compare(dexEngine.bchSuccessPreparationCommitment,"")
        page.selectPair("BTC / XMZ")
    }
    function test_bchSigningReviewsSnapshot() {
        page.selectPair("BCH / XMZ")
        var review={details:{bchFundingOutpoint:"ab:0",bchPayoutAtoms:"99000",bchFeeAtoms:"1000",bchPayoutScript:"76a914…88ac",nativeTxid:"cd",nativeDestination:"F-test",nativeAmountAtoms:"200000000000",minimumBchConfirmations:2,minimumNativeConfirmations:3,cancellationMarginBlocks:5}}
        dexEngine.bchSuccessSignatureReviewStatus={configured:true,state:"reviewed",reviewAvailable:true,review:review}
        dexEngine.bchSuccessSignedReleaseReviewStatus={configured:true,state:"reviewed",reviewAvailable:true,review:review}
        wait(100)
        var panel=findChild(page,"bchSuccessSignatureReviewPanel"),originalY=page.y
        page.y=-panel.mapToItem(page,0,0).y
        wait(100)
        var captured=false
        test.grabToImage(function(result){verify(result.saveToFile("build/bch-success-signing-panels-20261011.png"));captured=true})
        tryVerify(function(){return captured},5000)
        page.y=originalY
        dexEngine.bchSuccessSignatureReviewStatus={configured:false}
        dexEngine.bchSuccessSignedReleaseReviewStatus={configured:false}
        page.selectPair("BTC / XMZ")
    }
    function test_bchSuccessReleaseReviewIsExplicitAndClearsStaleDetails() {
        page.selectPair("BCH / XMZ")
        dexEngine.bchSuccessReleaseReviewStatus={configured:true,state:"ready",reviewAvailable:false}
        var panel=findChild(page,"bchSuccessReleaseReviewPanel"), button=findChild(page,"bchSuccessReleaseReviewRequest")
        var details=findChild(page,"bchSuccessReleaseReviewDetails"), outcome=findChild(page,"bchSuccessReleaseReviewOutcome")
        wait(0);verify(panel.visible);verify(!button.enabled);verify(!details.visible);compare(dexEngine.bchSuccessReleaseReviewRequests,0)
        dexEngine.bchNativeProofStatus={configured:true,state:"saved"}
        dexEngine.bchSuccessReleaseReviewStatus={configured:true,state:"ready",reviewAvailable:true}
        wait(0);verify(button.enabled);button.clicked();compare(dexEngine.bchSuccessReleaseReviewRequests,1)
        var review={details:{bchFundingOutpoint:"ab:0",bchPayoutAtoms:"99000",bchFeeAtoms:"1000",bchPayoutScript:"76a914…88ac",nativeTxid:"cd",nativeDestination:"F-test",nativeAmountAtoms:"200000000000",minimumBchConfirmations:2,minimumNativeConfirmations:3,cancellationMarginBlocks:5}}
        dexEngine.bchSuccessReleaseReviewStatus={configured:true,state:"reviewed",reviewAvailable:true,review:review}
        wait(0);verify(details.visible);verify(details.text.indexOf("0.00099 BCH")>=0);verify(details.text.indexOf("0.00001 BCH")>=0);verify(details.text.indexOf("2 XMZ")>=0)
        verify(outcome.text.indexOf("separate approval")>=0)
        var grabbed=false;panel.grabToImage(function(result){verify(result.saveToFile("build/bch-success-release-review-panel-20261011.png"));grabbed=true});tryVerify(function(){return grabbed},5000)
        dexEngine.bchSuccessReleaseReviewStatus={configured:true,state:"reviewing",busy:true,reviewAvailable:false}
        wait(0);verify(!button.enabled);verify(!details.visible);compare(details.text,"")
        dexEngine.bchSuccessReleaseReviewStatus={configured:true,state:"unknown",reviewAvailable:true}
        wait(0);verify(!details.visible);verify(outcome.text.indexOf("Previous details have been cleared")>=0)
        page.selectPair("LTC / XMZ");verify(!panel.visible)
        dexEngine.bchSuccessReleaseReviewStatus={configured:false};dexEngine.bchNativeProofStatus={configured:false};page.selectPair("BTC / XMZ")
    }
    function test_bchFundingReceiptRequiresStatusReviewAndConsent() {
        page.selectPair("BCH / XMZ")
        dexEngine.bchFundingReceiptStatus={configured:true,state:"ready",checkAvailable:true}
        var panel=findChild(page,"bchFundingReceiptPanel"), check=findChild(page,"bchFundingReceiptCheck")
        var review=findChild(page,"bchFundingReceiptReview"), submit=findChild(page,"bchFundingReceiptSubmit")
        var consent=findChild(page,"bchFundingReceiptConsent"), outcome=findChild(page,"bchFundingReceiptOutcome")
        wait(0);verify(panel.visible);verify(!review.enabled);verify(!submit.enabled)
        compare(dexEngine.bchFundingReceiptChecks,0);compare(dexEngine.bchFundingReceiptReviews,0)
        check.clicked();compare(dexEngine.bchFundingReceiptChecks,1)
        dexEngine.bchFundingReceiptStatus={configured:true,state:"not-saved",checkAvailable:true,reviewAvailable:true}
        wait(0);review.clicked();compare(dexEngine.bchFundingReceiptReviews,1)
        var approved={commitment:"receipt-approved",details:{swapId:"trade",bchNetwork:"bch-regtest",fundingOutpoint:"a".repeat(64)+":2",principalAtoms:"100000",feeAtoms:"235",contractScript:"aa20"+"b".repeat(64)+"87"}}
        dexEngine.bchFundingReceiptStatus={configured:true,state:"reviewed",peerIdentity:"reviewed-peer",review:approved,submitAvailable:true,checkAvailable:true}
        wait(0);verify(findChild(page,"bchFundingReceiptDetails").text.indexOf("reviewed-peer")>=0)
        var shown=findChild(page,"bchFundingReceiptDetails").text
        verify(shown.indexOf("a".repeat(64)+":2")>=0);verify(shown.indexOf("0.001 BCH")>=0)
        verify(shown.indexOf("0.00000235 BCH")>=0)
        var grabbed=false
        panel.grabToImage(function(result){verify(result.saveToFile("build/bch-funding-receipt-panel-20261011.png"));grabbed=true})
        tryVerify(function(){return grabbed},5000)
        verify(!submit.enabled);consent.checked=true;tryCompare(submit,"enabled",true)
        submit.clicked();compare(dexEngine.bchFundingReceiptSubmitted,"receipt-approved");verify(!consent.checked)
        consent.checked=true;page.selectPair("LTC / XMZ");verify(!consent.checked);verify(!panel.visible);verify(!submit.enabled)
        page.selectPair("BCH / XMZ");consent.checked=true
        dexEngine.bchFundingReceiptStatus={configured:true,state:"unknown",checkAvailable:true,submitAttempted:true}
        tryCompare(consent,"checked",false);verify(!submit.enabled);verify(outcome.text.indexOf("unknown")>=0)
        dexEngine.bchFundingReceiptAccepts=false;check.clicked();verify(outcome.text.indexOf("could not start")>=0)
        dexEngine.bchFundingReceiptStatus={configured:true,state:"saved",peerIdentity:"reviewed-peer",review:approved,checkAvailable:true,receipt:{commitment:"saved-receipt"}}
        wait(0);verify(outcome.text.indexOf("saved-receipt")>=0);verify(outcome.text.indexOf("confirmation")>=0);verify(!submit.enabled)
        dexEngine.bchFundingReceiptAccepts=true;dexEngine.bchFundingReceiptStatus={configured:false};page.selectPair("BTC / XMZ")
    }
    function test_bchRefundProtectionRequiresStatusReviewAndConsent() {
        page.selectPair("BCH / XMZ")
        dexEngine.bchRefundProtectionStatus={configured:true,state:"ready",checkAvailable:true}
        var panel=findChild(page,"bchRefundProtectionPanel"), check=findChild(page,"bchRefundProtectionCheck")
        var review=findChild(page,"bchRefundProtectionReview"), submit=findChild(page,"bchRefundProtectionSubmit")
        var consent=findChild(page,"bchRefundProtectionConsent"), outcome=findChild(page,"bchRefundProtectionOutcome")
        wait(0);verify(panel.visible);verify(!review.enabled);verify(!submit.enabled)
        compare(dexEngine.bchRefundProtectionChecks,0);compare(dexEngine.bchRefundProtectionReviews,0)
        check.clicked();compare(dexEngine.bchRefundProtectionChecks,1)
        dexEngine.bchRefundProtectionStatus={configured:true,state:"not-saved",checkAvailable:true,reviewAvailable:true}
        wait(0);review.clicked();compare(dexEngine.bchRefundProtectionReviews,1)
        var approved={commitment:"refund-approved",details:{swapId:"trade",peerIdentity:"reviewed-peer",quote:"signed-quote",frame:"message-digest"}}
        dexEngine.bchRefundProtectionStatus={configured:true,state:"reviewed",review:approved,submitAvailable:true,checkAvailable:true}
        wait(0);verify(findChild(page,"bchRefundProtectionDetails").text.indexOf("reviewed-peer")>=0)
        verify(!submit.enabled);consent.checked=true;tryCompare(submit,"enabled",true)
        submit.clicked();compare(dexEngine.bchRefundProtectionSubmitted,"refund-approved");verify(!consent.checked)
        consent.checked=true;page.selectPair("LTC / XMZ");verify(!consent.checked);verify(!panel.visible);verify(!submit.enabled)
        page.selectPair("BCH / XMZ");consent.checked=true
        dexEngine.bchRefundProtectionStatus={configured:true,state:"unknown",checkAvailable:true,submitAttempted:true}
        tryCompare(consent,"checked",false);verify(!submit.enabled);verify(outcome.text.indexOf("unknown")>=0)
        dexEngine.bchRefundProtectionAccepts=false;check.clicked();verify(outcome.text.indexOf("could not start")>=0)
        dexEngine.bchRefundProtectionStatus={configured:true,state:"saved",checkAvailable:true,receipt:{commitment:"saved-receipt"}}
        wait(0);verify(outcome.text.indexOf("saved-receipt")>=0);verify(outcome.text.indexOf("No funds")>=0);verify(!submit.enabled)
        dexEngine.bchRefundProtectionAccepts=true;dexEngine.bchRefundProtectionStatus={configured:false};page.selectPair("BTC / XMZ")
    }
    function test_bchRefundWalletFreshReviewIsExplicit() {
        page.selectPair("BCH / XMZ");dexEngine.bchRefundWalletReviewAction="";dexEngine.bchRefundWalletAccepts=true
        var saved={state:"not-attempted",address:"XMZ-fixture",walletFile:"swap-fixture"}
        dexEngine.bchRefundWalletStatus={configured:true,reviewConfigured:true,reviewAvailable:true,state:"not-attempted",savedWallet:saved}
        var restore=findChild(page,"bchRefundWalletReview_restore"),open=findChild(page,"bchRefundWalletReview_open"),check=findChild(page,"bchRefundWalletReview_check")
        verify(restore.enabled);verify(!open.enabled);verify(!check.enabled);compare(dexEngine.bchRefundWalletReviewAction,"")
        open.clicked();compare(dexEngine.bchRefundWalletReviewAction,"");restore.clicked();compare(dexEngine.bchRefundWalletReviewAction,"restore")
        saved={state:"attempted",address:"XMZ-fixture",walletFile:"swap-fixture"}
        dexEngine.bchRefundWalletStatus={configured:true,reviewConfigured:true,reviewAvailable:true,state:"attempted",savedWallet:saved}
        verify(!restore.enabled);verify(open.enabled);verify(check.enabled);check.clicked();compare(dexEngine.bchRefundWalletReviewAction,"check")
        var review={commitment:"public-review",details:{action:"check",address:"XMZ-fixture",walletFile:"swap-fixture",minimumBchConfirmations:2,minimumXmzConfirmations:3,walletBackend:{host:"127.0.0.1",walletPort:18083,daemonPort:18081}}}
        dexEngine.bchRefundWalletStatus={configured:true,reviewConfigured:true,state:"reviewed",chainObserved:true,review:review,savedWallet:saved}
        var details=findChild(page,"bchRefundWalletFreshReview"),history=findChild(page,"bchRefundWalletHistory")
        verify(details.visible);verify(history.visible);verify(details.text.indexOf("not a wallet balance")>=0)
        var captured=false;verify(findChild(page,"bchRefundWalletPanel").grabToImage(function(result){captured=result.saveToFile("build/bch-refund-wallet-fresh-review-panel-20261015.png")}));tryVerify(function(){return captured},5000)
        dexEngine.bchRefundWalletStatus={configured:true,reviewConfigured:true,state:"review-expired",chainObserved:false,review:review,savedWallet:saved}
        verify(!details.visible);compare(details.text,"");verify(history.visible)
        verify(findChild(page,"bchRefundWalletOutcome").text.indexOf("expired")>=0)
        page.selectPair("LTC / XMZ");verify(!findChild(page,"bchRefundWalletPanel").visible)
        dexEngine.bchRefundWalletStatus={configured:false};page.selectPair("BTC / XMZ")
    }
    function test_bchRefundWalletApproval() {
        page.selectPair("BCH / XMZ");dexEngine.bchRefundWalletAccepts=true;dexEngine.bchRefundWalletSubmitted={}
        var form=findChild(page,"bchRefundWalletApproval"),password=findChild(page,"bchRefundWalletPassword")
        var confirmation=findChild(page,"bchRefundWalletConfirmation"),consent=findChild(page,"bchRefundWalletConsent"),submit=findChild(page,"bchRefundWalletSubmit")
        function state(action,owned) {return {configured:true,reviewConfigured:true,state:"reviewed",chainObserved:true,submitAvailable:true,serviceOwned:owned,review:{commitment:"approved-review",details:{action:action}}};}
        dexEngine.bchRefundWalletStatus=state("restore",false);verify(!form.visible);submit.clicked();compare(Object.keys(dexEngine.bchRefundWalletSubmitted).length,0)
        dexEngine.bchRefundWalletStatus=state("restore",true);verify(form.visible);verify(!submit.enabled)
        password.text="stale";confirmation.text="stale";consent.checked=true
        var replacement=state("restore",true);replacement.review.commitment="replacement-review"
        dexEngine.bchRefundWalletStatus=replacement;verify(form.visible);compare(password.text,"");verify(!consent.checked)
        dexEngine.bchRefundWalletStatus=state("restore",true)
        password.text="fixture";confirmation.text="wrong";consent.checked=true;verify(!submit.enabled)
        confirmation.text="fixture";verify(submit.enabled);submit.clicked()
        compare(dexEngine.bchRefundWalletSubmitted.commitment,"approved-review");compare(dexEngine.bchRefundWalletSubmitted.confirmation,"fixture")
        compare(password.text,"");compare(confirmation.text,"");verify(!consent.checked);verify(!submit.enabled)
        dexEngine.bchRefundWalletStatus=state("open",true);verify(!confirmation.visible);password.text="existing";consent.checked=true;submit.clicked()
        compare(dexEngine.bchRefundWalletSubmitted.password,"existing");compare(dexEngine.bchRefundWalletSubmitted.confirmation,"")
        dexEngine.bchRefundWalletStatus=state("check",true);verify(!password.visible);consent.checked=true;submit.clicked()
        compare(dexEngine.bchRefundWalletSubmitted.password,"");compare(dexEngine.bchRefundWalletSubmitted.approved,true)
        dexEngine.bchRefundWalletStatus=state("restore",true);password.text="erase";confirmation.text="erase";consent.checked=true
        dexEngine.bchRefundWalletStatus={configured:true,state:"review-expired"};verify(!form.visible);compare(password.text,"");verify(!consent.checked)
        dexEngine.bchRefundWalletStatus=state("restore",true);password.text="erase";page.selectPair("LTC / XMZ");compare(password.text,"")
        page.selectPair("BCH / XMZ");dexEngine.bchRefundWalletStatus={configured:true,state:"recovery-required",submissionUncertain:true,savedWallet:{address:"saved",walletFile:"existing"}}
        verify(findChild(page,"bchRefundWalletHistory").visible);verify(findChild(page,"bchRefundWalletOutcome").text.indexOf("may have completed")>=0)
        dexEngine.bchRefundWalletStatus={configured:false};page.selectPair("BTC / XMZ")
    }
    function test_bchRefundWalletSavedHistory() {
        page.selectPair("BCH / XMZ")
        dexEngine.bchRefundWalletChecks=0
        dexEngine.bchRefundWalletStatus={configured:true,state:"configured",checkAvailable:true}
        var panel=findChild(page,"bchRefundWalletPanel"),check=findChild(page,"bchRefundWalletCheck")
        var details=findChild(page,"bchRefundWalletDetails"),history=findChild(page,"bchRefundWalletHistory"),outcome=findChild(page,"bchRefundWalletOutcome")
        wait(0);verify(panel.visible);compare(dexEngine.bchRefundWalletChecks,0);check.clicked();compare(dexEngine.bchRefundWalletChecks,1)
        var wallet={address:"XMZ-refund-address",walletFile:"swap-example",swapId:"trade-example"}
        dexEngine.bchRefundWalletStatus={configured:true,state:"ready",checkAvailable:true,wallet:wallet,savedWallet:wallet}
        wait(0);verify(details.visible);verify(!history.visible);verify(outcome.text.indexOf("previously")>=0)
        verify(outcome.text.indexOf("not a current balance")>=0);verify(outcome.text.indexOf("separate checks and approval")>=0)
        var captured=false
        verify(panel.grabToImage(function(result) {captured=result.saveToFile("build/bch-refund-wallet-status-panel-20261015.png")}))
        tryVerify(function() {return captured})
        dexEngine.bchRefundWalletStatus={configured:true,state:"checking",busy:true,checkAvailable:true,wallet:wallet,savedWallet:wallet}
        wait(0);verify(!details.visible);verify(history.visible);verify(!check.enabled);check.clicked();compare(dexEngine.bchRefundWalletChecks,1)
        verify(history.text.indexOf("historical only")>=0)
        dexEngine.bchRefundWalletStatus={configured:true,state:"unknown",checkAvailable:true,wallet:wallet,savedWallet:wallet}
        wait(0);verify(!details.visible);verify(history.visible);verify(outcome.text.indexOf("do not recreate")>=0)
        dexEngine.bchRefundWalletStatus={configured:true,state:"attempted",checkAvailable:true,wallet:wallet}
        wait(0);verify(details.visible);verify(!history.visible);verify(outcome.text.indexOf("Do not create another wallet")>=0)
        dexEngine.bchRefundWalletStatus={configured:true,state:"not-attempted",checkAvailable:true,wallet:wallet}
        wait(0);verify(outcome.text.indexOf("does not prove")>=0)
        dexEngine.bchRefundWalletAccepts=false;check.clicked();verify(outcome.text.indexOf("could not start")>=0)
        page.selectPair("LTC / XMZ");wait(0);verify(!panel.visible)
        dexEngine.bchRefundWalletAccepts=true;dexEngine.bchRefundWalletStatus={configured:false};page.selectPair("BTC / XMZ")
    }

    function test_bchPunishmentOfflineReview() {
        page.selectPair("BCH / XMZ")
        dexEngine.bchPunishmentStatus={configured:true,state:"ready",checkAvailable:true,reviewAvailable:true}
        var panel=findChild(page,"bchPunishmentPanel"), check=findChild(page,"bchPunishmentCheck")
        var review=findChild(page,"bchPunishmentReview"), outcome=findChild(page,"bchPunishmentOutcome")
        var details=findChild(page,"bchPunishmentDetails")
        wait(0);verify(panel.visible);compare(dexEngine.bchPunishmentChecks,0)
        check.clicked();review.clicked();compare(dexEngine.bchPunishmentChecks,1);compare(dexEngine.bchPunishmentReviews,1)
        dexEngine.bchPunishmentStatus={configured:true,state:"reviewed",review:{commitment:"bound",details:{swapId:"trade",txid:"punishment-tx",payoutAtoms:"97000",feeAtoms:"2000",destinationScript:"51",punishDelayBlocks:100}}}
        wait(0);verify(details.visible);verify(details.text.indexOf("punishment-tx")>=0);verify(details.text.indexOf("0.00097000")>=0)
        verify(outcome.text.indexOf("Offline")>=0);verify(!check.enabled);verify(!review.enabled)
        var captured=false
        panel.grabToImage(function(result){verify(result.saveToFile("build/bch-punishment-review-panel-20261015.png"));captured=true})
        tryVerify(function(){return captured},5000)
        dexEngine.bchPunishmentStatus={configured:true,state:"acknowledged",checkAvailable:true};wait(0)
        verify(outcome.text.indexOf("not chain confirmation")>=0);verify(!details.visible)
        dexEngine.bchPunishmentAccepts=false;check.clicked();verify(outcome.text.indexOf("could not start")>=0)
        dexEngine.bchPunishmentAccepts=true
        dexEngine.bchPunishmentStatus={configured:true,state:"attempted",observeAvailable:true}
        var observe=findChild(page,"bchPunishmentObserve");wait(0);verify(observe.enabled)
        observe.clicked();compare(dexEngine.bchPunishmentObservations,1)
        dexEngine.bchPunishmentStatus={configured:true,state:"attempted",chainObserved:true,observation:{confirmations:"3",block:"confirmed-block",observedAt:"1234"}}
        wait(0);verify(outcome.text.indexOf("confirmed-block")>=0);verify(outcome.text.indexOf("Observed at:")>=0);verify(outcome.text.indexOf("Unix time")<0)
        dexEngine.bchPunishmentStatus={configured:true,state:"attempted",chainObserved:false};wait(0)
        verify(outcome.text.indexOf("confirmed-block")<0);verify(outcome.text.indexOf("uncertain")>=0)
        dexEngine.bchPunishmentStatus={configured:true,state:"unknown",savedRelayState:"attempted",chainObserved:false};wait(0)
        verify(outcome.text.indexOf("Previously verified send history")>=0);verify(outcome.text.indexOf("confirmed-block")<0)
        page.selectPair("LTC / XMZ");verify(!panel.visible);verify(!panel.enabled)
        dexEngine.bchPunishmentAccepts=true;dexEngine.bchPunishmentStatus={configured:false};page.selectPair("BTC / XMZ")
    }
    function test_bchPunishmentSeparateSendConsent() {
        page.selectPair("BCH / XMZ")
        var approved={commitment:"exact-recovery",details:{swapId:"trade",txid:"recovery",payoutAtoms:"97000",feeAtoms:"2000",destinationScript:"51",punishDelayBlocks:100}}
        dexEngine.bchPunishmentStatus={configured:true,submissionConfigured:true,state:"reviewed",submitAvailable:true,review:approved}
        var consent=findChild(page,"bchPunishmentConsent"), submit=findChild(page,"bchPunishmentSubmit")
        var outcome=findChild(page,"bchPunishmentOutcome")
        wait(0);verify(submit.visible);verify(!submit.enabled);compare(dexEngine.bchPunishmentSubmitted,"")
        consent.checked=true;verify(submit.enabled)
        var captured=false,panel=findChild(page,"bchPunishmentPanel")
        panel.grabToImage(function(result){verify(result.saveToFile("build/bch-punishment-submit-panel-20261015.png"));captured=true})
        tryVerify(function(){return captured},5000)
        submit.clicked();compare(dexEngine.bchPunishmentSubmitted,"exact-recovery");verify(!consent.checked)
        consent.checked=true;page.selectPair("LTC / XMZ");verify(!consent.checked);verify(!submit.enabled)
        page.selectPair("BCH / XMZ");consent.checked=true
        dexEngine.bchPunishmentStatus={configured:true,submissionConfigured:true,state:"unknown",submitAttempted:true,checkAvailable:true,observeAvailable:true}
        tryCompare(consent,"checked",false);verify(!submit.enabled);verify(outcome.text.indexOf("Do not send again")>=0)
        dexEngine.bchPunishmentStatus={configured:true,submissionConfigured:true,state:"acknowledged"};wait(0)
        verify(!submit.enabled);verify(outcome.text.indexOf("not chain confirmation")>=0)
        dexEngine.bchPunishmentStatus={configured:false};dexEngine.bchPunishmentSubmitted="";page.selectPair("BTC / XMZ")
    }
    function test_bchCancellationConsentAndObservation() {
        page.selectPair("BCH / XMZ")
        dexEngine.bchCancellationStatus={configured:true,state:"ready",checkAvailable:true,observeAvailable:true}
        var panel=findChild(page,"bchCancellationPanel"), check=findChild(page,"bchCancellationCheck")
        var review=findChild(page,"bchCancellationReview"), submit=findChild(page,"bchCancellationSubmit")
        var consent=findChild(page,"bchCancellationConsent"), outcome=findChild(page,"bchCancellationOutcome")
        var observe=findChild(page,"bchCancellationObserve")
        wait(0);verify(panel.visible);verify(!review.enabled);verify(!submit.enabled)
        compare(dexEngine.bchCancellationChecks,0);compare(dexEngine.bchCancellationObservations,0)
        check.clicked();compare(dexEngine.bchCancellationChecks,1)
        dexEngine.bchCancellationStatus={configured:true,state:"checked",statusKnown:true,relayState:"not-attempted",reviewAvailable:true}
        wait(0);review.clicked();compare(dexEngine.bchCancellationReviews,1)
        var approved={commitment:"cancel-approved",details:{swapId:"trade",txid:Array(65).join("a"),fundingOutpoint:Array(65).join("b")+":0",recoveryAtoms:"99000",feeAtoms:"1000",cancelDelayBlocks:20}}
        dexEngine.bchCancellationStatus={configured:true,state:"reviewed",review:approved,submitAvailable:true,observeAvailable:true}
        wait(0);verify(findChild(page,"bchCancellationDetails").text.indexOf("0.00001000 BCH")>=0)
        verify(!submit.enabled);consent.checked=true;tryCompare(submit,"enabled",true)
        var grabbed=false
        panel.grabToImage(function(result){verify(result.saveToFile("build/bch-cancellation-panel-20261011.png"));grabbed=true})
        tryVerify(function(){return grabbed},3000)
        submit.clicked();compare(dexEngine.bchCancellationSubmitted,"cancel-approved");verify(!consent.checked)
        consent.checked=true;page.selectPair("LTC / XMZ");verify(!consent.checked);verify(!panel.visible);verify(!submit.enabled)
        page.selectPair("BCH / XMZ")
        dexEngine.bchCancellationStatus={configured:true,state:"acknowledged",relayState:"acknowledged",observeAvailable:true}
        wait(0);verify(outcome.text.indexOf("not chain confirmation")>=0);verify(!submit.enabled)
        observe.clicked();compare(dexEngine.bchCancellationObservations,1)
        dexEngine.bchCancellationStatus={configured:true,state:"observed",chainObserved:true,observation:{txid:"observed-tx",confirmations:"3",observedAt:"123"}}
        wait(0);verify(outcome.text.indexOf("observed-tx")>=0);verify(outcome.text.indexOf("separate check")>=0)
        dexEngine.bchCancellationStatus={configured:true,state:"unknown",checkAvailable:true,submitAttempted:true}
        wait(0);verify(outcome.text.indexOf("unknown")>=0);verify(outcome.text.indexOf("observed-tx")<0);verify(!submit.enabled)
        dexEngine.bchCancellationAccepts=false;check.clicked();verify(outcome.text.indexOf("could not start")>=0)
        dexEngine.bchCancellationAccepts=true;dexEngine.bchCancellationStatus={configured:false};page.selectPair("BTC / XMZ")
    }
    function test_bchRefundReleaseConsentAndObservation() {
        page.selectPair("BCH / XMZ")
        dexEngine.bchRefundReleaseStatus={configured:true,state:"ready",checkAvailable:true,observeAvailable:true}
        var panel=findChild(page,"bchRefundReleasePanel"), check=findChild(page,"bchRefundReleaseCheck")
        var review=findChild(page,"bchRefundReleaseReview"), submit=findChild(page,"bchRefundReleaseSubmit")
        var consent=findChild(page,"bchRefundReleaseConsent"), outcome=findChild(page,"bchRefundReleaseOutcome")
        var observe=findChild(page,"bchRefundReleaseObserve")
        wait(0);verify(panel.visible);verify(!review.enabled);verify(!submit.enabled)
        compare(dexEngine.bchRefundReleaseChecks,0);compare(dexEngine.bchRefundReleaseObservations,0)
        check.clicked();compare(dexEngine.bchRefundReleaseChecks,1)
        dexEngine.bchRefundReleaseStatus={configured:true,state:"checked",statusKnown:true,relayState:"not-attempted",reviewAvailable:true}
        wait(0);review.clicked();compare(dexEngine.bchRefundReleaseReviews,1)
        var approved={commitment:"refund-release-approved",details:{swapId:"trade",txid:Array(65).join("a"),fundingOutpoint:Array(65).join("b")+":0",refundAtoms:"97000",feeAtoms:"2000",destinationScript:"51",punishmentMarginBlocks:5}}
        dexEngine.bchRefundReleaseStatus={configured:true,state:"reviewed",review:approved,submitAvailable:true,observeAvailable:true}
        wait(0);verify(findChild(page,"bchRefundReleaseDetails").text.indexOf("0.00002000 BCH")>=0)
        verify(consent.text.indexOf("recover XMZ")>=0);verify(findChild(page,"bchRefundReleaseDetails").text.indexOf("Safety margin")>=0)
        verify(!submit.enabled);consent.checked=true;tryCompare(submit,"enabled",true)
        var grabbed=false
        panel.grabToImage(function(result){verify(result.saveToFile("build/bch-refund-release-panel-20261011.png"));grabbed=true})
        tryVerify(function(){return grabbed},3000)
        submit.clicked();compare(dexEngine.bchRefundReleaseSubmitted,"refund-release-approved");verify(!consent.checked)
        consent.checked=true;page.selectPair("LTC / XMZ");verify(!consent.checked);verify(!panel.visible);verify(!submit.enabled)
        page.selectPair("BCH / XMZ")
        dexEngine.bchRefundReleaseStatus={configured:true,state:"acknowledged",relayState:"acknowledged",observeAvailable:true}
        wait(0);verify(outcome.text.indexOf("not chain confirmation")>=0);verify(!submit.enabled)
        observe.clicked();compare(dexEngine.bchRefundReleaseObservations,1)
        dexEngine.bchRefundReleaseStatus={configured:true,state:"observed",chainObserved:true,observation:{txid:"observed-tx",confirmations:"3",observedAt:"123"}}
        wait(0);verify(outcome.text.indexOf("observed-tx")>=0);verify(outcome.text.indexOf("separate check")>=0)
        dexEngine.bchRefundReleaseStatus={configured:true,state:"unknown",checkAvailable:true,submitAttempted:true}
        wait(0);verify(outcome.text.indexOf("unknown")>=0);verify(outcome.text.indexOf("observed-tx")<0);verify(!submit.enabled)
        dexEngine.bchRefundReleaseAccepts=false;check.clicked();verify(outcome.text.indexOf("could not start")>=0)
        dexEngine.bchRefundReleaseAccepts=true;dexEngine.bchRefundReleaseStatus={configured:false};page.selectPair("BTC / XMZ")
    }
    function test_bchRecoveryRetentionRequiresStatusReviewAndConsent() {
        page.selectPair("BCH / XMZ")
        dexEngine.bchRecoveryRetentionStatus={configured:true,state:"ready",checkAvailable:true}
        var panel=findChild(page,"bchRecoveryRetentionPanel"), check=findChild(page,"bchRecoveryRetentionCheck")
        var review=findChild(page,"bchRecoveryRetentionReview"), submit=findChild(page,"bchRecoveryRetentionSubmit")
        var consent=findChild(page,"bchRecoveryRetentionConsent"), outcome=findChild(page,"bchRecoveryRetentionOutcome")
        wait(0);verify(panel.visible);verify(!review.enabled);verify(!submit.enabled)
        compare(dexEngine.bchRecoveryRetentionChecks,0);compare(dexEngine.bchRecoveryRetentionReviews,0)
        check.clicked();compare(dexEngine.bchRecoveryRetentionChecks,1)
        dexEngine.bchRecoveryRetentionStatus={configured:true,state:"not-saved",checkAvailable:true,reviewAvailable:true}
        wait(0);review.clicked();compare(dexEngine.bchRecoveryRetentionReviews,1)
        var approved={commitment:"refund-approved",details:{swapId:"trade",fundingTxid:Array(65).join("a"),quote:Array(65).join("b"),cancelTxid:Array(65).join("c"),refundTxid:Array(65).join("d"),punishTxid:Array(65).join("e")}}
        dexEngine.bchRecoveryRetentionStatus={configured:true,state:"reviewed",review:approved,submitAvailable:true,checkAvailable:true}
        wait(0);verify(findChild(page,"bchRecoveryRetentionDetails").text.indexOf(Array(65).join("d"))>=0)
        verify(!submit.enabled);consent.checked=true;tryCompare(submit,"enabled",true)
        var grabbed=false
        panel.grabToImage(function(result) {verify(result.saveToFile("build/bch-recovery-retention-panel-20261011.png"));grabbed=true})
        tryVerify(function(){return grabbed},3000)
        submit.clicked();compare(dexEngine.bchRecoveryRetentionSubmitted,"refund-approved");verify(!consent.checked)
        consent.checked=true;page.selectPair("LTC / XMZ");verify(!consent.checked);verify(!panel.visible);verify(!submit.enabled)
        page.selectPair("BCH / XMZ");consent.checked=true
        dexEngine.bchRecoveryRetentionStatus={configured:true,state:"unknown",checkAvailable:true,submitAttempted:true}
        tryCompare(consent,"checked",false);verify(!submit.enabled);verify(outcome.text.indexOf("unknown")>=0)
        dexEngine.bchRecoveryRetentionAccepts=false;check.clicked();verify(outcome.text.indexOf("could not start")>=0)
        dexEngine.bchRecoveryRetentionStatus={configured:true,state:"saved",checkAvailable:true,recovery:{commitment:"saved-receipt"}}
        wait(0);verify(outcome.text.indexOf("saved-receipt")>=0);verify(outcome.text.indexOf("No funds")>=0);verify(!submit.enabled)
        dexEngine.bchRecoveryRetentionAccepts=true;dexEngine.bchRecoveryRetentionStatus={configured:false};page.selectPair("BTC / XMZ")
    }
    function test_litecoinLightExplicitActionsAndFailure() {
        page.selectPair("LTC / XMZ")
        dexEngine.litecoinLightStatus={configured:true,network:"regtest",state:"ready",busy:false,refreshAvailable:true,syncAvailable:true,chain:{},synchronization:{}}
        wait(0)
        var panel=findChild(page,"litecoinLightPanel"),load=findChild(page,"litecoinLightLoad"),sync=findChild(page,"litecoinLightSync")
        var cancel=findChild(page,"litecoinLightCancel"),summary=findChild(page,"litecoinLightSummary"),details=findChild(page,"litecoinLightSyncDetails")
        verify(panel.visible);compare(dexEngine.lightSyncs,0);verify(load.enabled);verify(sync.enabled);verify(!cancel.visible)
        load.clicked();compare(dexEngine.lightLoads,1);compare(dexEngine.lightSyncs,0)
        sync.clicked();compare(dexEngine.lightSyncs,1);compare(dexEngine.lightBudget,10000000)
        dexEngine.litecoinLightStatus={configured:true,network:"regtest",state:"synchronizing",busy:true,refreshAvailable:false,syncAvailable:false,chain:{},synchronization:{}}
        wait(0);verify(!load.enabled);verify(!sync.enabled);verify(cancel.visible);verify(summary.text.indexOf("Synchronizing")>=0)
        cancel.clicked();compare(dexEngine.lightCancels,1)
        dexEngine.litecoinLightStatus={configured:true,network:"regtest",state:"checked",busy:false,refreshAvailable:true,syncAvailable:true,chain:{height:123},synchronization:{reportedHeight:123,downloadedHeaders:2}}
        wait(0);verify(summary.text.indexOf("123")>=0);verify(details.visible);verify(findChild(page,"litecoinLightBoundary").text.indexOf("not available")>=0)
        var grabbed=false
        panel.grabToImage(function(result) {verify(result.saveToFile("litecoin-light-panel.png"));grabbed=true})
        tryVerify(function(){return grabbed},5000)
        dexEngine.litecoinLightStatus={configured:true,network:"regtest",state:"unknown",busy:false,refreshAvailable:true,syncAvailable:true,chain:{},synchronization:{}}
        wait(0);verify(summary.text.indexOf("123")<0);verify(!details.visible)
        page.selectPair("BCH / XMZ");verify(!panel.visible);compare(dexEngine.lightSyncs,1)
        dexEngine.litecoinLightStatus={configured:false}
    }
    function test_litecoinWatchOnlyActionsAndProvisionalAmounts() {
        page.selectPair("LTC / XMZ")
        dexEngine.lightWalletInits=0;dexEngine.lightWalletScan=[]
        dexEngine.litecoinLightStatus={configured:true,walletConfigured:true,network:"regtest",state:"ready",busy:false,walletInitAvailable:true,walletRefreshAvailable:true,wallet:{},chain:{}}
        wait(0)
        var init=findChild(page,"litecoinLightWalletInit"),refresh=findChild(page,"litecoinLightWalletRefresh"),amounts=findChild(page,"litecoinLightWalletAmounts")
        verify(init.visible);verify(refresh.visible);verify(!amounts.visible);compare(dexEngine.lightWalletInits,0);compare(dexEngine.lightWalletScan.length,0)
        init.clicked();compare(dexEngine.lightWalletInits,1);refresh.clicked();compare(dexEngine.lightWalletScan,[20,500,1000])
        dexEngine.litecoinLightStatus={configured:true,walletConfigured:true,network:"regtest",state:"wallet-checked",busy:false,walletInitAvailable:true,walletRefreshAvailable:true,chain:{height:2},wallet:{watchOnly:true,spendable:false,balanceStatus:"provisional",observedConfirmedAtomic:"60000",immatureAtomic:"0",pendingSpentAtomic:"100000",pendingReceivedAtomic:"30000"}}
        wait(0);verify(amounts.visible);verify(amounts.text.indexOf("0.00060000")>=0);verify(amounts.text.indexOf("0.00100000")>=0)
        compare(page.formatLtcAtomic("18446744073709551615"),"184467440737.09551615");compare(page.formatLtcAtomic("bad"),"—")
        var grabbed=false
        findChild(page,"litecoinLightPanel").grabToImage(function(result) {verify(result.saveToFile("litecoin-watch-wallet-panel.png"));grabbed=true})
        tryVerify(function(){return grabbed},5000)
        dexEngine.litecoinLightStatus={configured:true,walletConfigured:true,state:"wallet-loading",busy:true,walletInitAvailable:false,walletRefreshAvailable:false,wallet:{},chain:{}}
        wait(0);verify(!init.enabled);verify(!refresh.enabled);verify(!amounts.visible);compare(amounts.text,"")
        dexEngine.litecoinLightStatus={configured:true,walletConfigured:true,state:"unknown",busy:false,wallet:{},chain:{}}
        wait(0);verify(!amounts.visible);compare(amounts.text,"")
        page.selectPair("BCH / XMZ");verify(!init.visible)
        dexEngine.litecoinLightStatus={configured:false}
    }
    function test_litecoinReceivingAddressIsExplicitAndCleared() {
        page.selectPair("LTC / XMZ");dexEngine.lightReceives=0
        dexEngine.litecoinLightStatus={configured:true,walletConfigured:true,network:"regtest",state:"ready",busy:false,walletReceiveAvailable:true,wallet:{}}
        wait(0)
        var button=findChild(page,"litecoinLightReceive"),address=findChild(page,"litecoinLightReceivingAddress")
        verify(button.visible);verify(button.enabled);verify(!address.visible);compare(dexEngine.lightReceives,0)
        button.clicked();compare(dexEngine.lightReceives,1)
        dexEngine.litecoinLightStatus={configured:true,walletConfigured:true,network:"regtest",state:"wallet-address",busy:false,walletReceiveAvailable:true,wallet:{watchOnly:true,spendable:false,address:"rltc1qPUBLIC_FIXTURE_ONLY"}}
        wait(0);verify(address.visible);verify(address.text.indexOf("rltc1qPUBLIC_FIXTURE_ONLY")>=0);verify(address.text.indexOf("Watch-only")>=0)
        var grabbed=false
        findChild(page,"litecoinLightPanel").grabToImage(function(result) {verify(result.saveToFile("litecoin-receive-panel.png"));grabbed=true})
        tryVerify(function(){return grabbed},5000)
        dexEngine.litecoinLightStatus={configured:true,walletConfigured:true,state:"wallet-loading",busy:true,walletReceiveAvailable:false,wallet:{}}
        wait(0);verify(!button.enabled);verify(!address.visible);compare(address.text,"")
        dexEngine.litecoinLightStatus={configured:true,walletConfigured:true,state:"unknown",busy:false,wallet:{}}
        wait(0);verify(!address.visible);compare(address.text,"")
        page.selectPair("BCH / XMZ");verify(!button.visible)
        dexEngine.litecoinLightStatus={configured:false}
    }
    function test_litecoinFeeEstimateRequiresExplicitApplication() {
        page.selectPair("LTC / XMZ");page.ltcPaymentScan={gap:20,addressesPerBranch:500,historyEntriesPerBranch:1000}
        dexEngine.lightPaymentRequest={};dexEngine.lightFeeRequest={};dexEngine.lightFeeAccepted=true
        var status={configured:true,walletConfigured:true,walletPrepareConfigured:true,walletEstimateAvailable:true,walletPrepareAvailable:true,network:"regtest",state:"ready",busy:false,wallet:{}}
        dexEngine.litecoinLightStatus=status
        var recipient=findChild(page,"litecoinLightPaymentRecipient"),amount=findChild(page,"litecoinLightPaymentAmount"),fee=findChild(page,"litecoinLightPaymentFee")
        var button=findChild(page,"litecoinLightEstimateFee"),apply=findChild(page,"litecoinLightApplyFee"),label=findChild(page,"litecoinLightFeeEstimate")
        recipient.text="rltc1qPUBLIC_FIXTURE_ONLY";amount.text="0.0004";fee.text="0.00001"
        findChild(page,"litecoinLightPaymentMinimumChange").text="0.00001000";findChild(page,"litecoinLightPaymentConfirmations").text="1"
        findChild(page,"litecoinLightFeeTarget").text="6";findChild(page,"litecoinLightRateCap").text="0.00002";findChild(page,"litecoinLightFeeCap").text="0.00001"
        wait(0);verify(button.enabled);button.clicked()
        compare(dexEngine.lightFeeRequest.quote.maximumRateAtomicPerKvb,"2000");compare(dexEngine.lightFeeRequest.quote.maximumFeeAtomic,"1000")
        compare(dexEngine.lightFeeRequest.quote.targetBlocks,6);compare(dexEngine.lightFeeRequest.scan,[20,500,1000]);compare(fee.text,"0.00001")
        status.state="wallet-estimated";status.wallet={watchOnly:true,spendable:false,prepared:false,feeEstimate:{feeAtomic:"141",virtualBytes:141,targetBlocks:6}}
        dexEngine.litecoinLightStatus=JSON.parse(JSON.stringify(status));wait(0);verify(apply.visible);verify(label.text.indexOf("0.00000141")>=0)
        compare(Object.keys(dexEngine.lightPaymentRequest).length,0);compare(fee.text,"0.00001")
        var grabbed=false
        findChild(page,"litecoinLightPaymentForm").grabToImage(function(result){verify(result.saveToFile("litecoin-fee-estimate-panel.png"));grabbed=true})
        tryVerify(function(){return grabbed},5000)
        amount.text="0.0005";wait(0);verify(!apply.visible);verify(!label.visible)
        amount.text="0.0004";wait(0);apply.clicked();compare(fee.text,"0.00000141");verify(!apply.visible)
        compare(Object.keys(dexEngine.lightPaymentRequest).length,0)
        button.clicked();status.wallet.feeEstimate=null;dexEngine.litecoinLightStatus=JSON.parse(JSON.stringify(status));wait(0)
        verify(label.visible);verify(label.text.indexOf("no fee estimate")>=0);verify(!apply.visible);compare(fee.text,"0.00000141")
        status.busy=true;dexEngine.litecoinLightStatus=JSON.parse(JSON.stringify(status));wait(0);verify(!label.visible);verify(!button.enabled)
        status.busy=false;status.state="unknown";status.wallet={};dexEngine.litecoinLightStatus=JSON.parse(JSON.stringify(status));wait(0);verify(!label.visible)
        dexEngine.lightFeeAccepted=false;button.clicked();verify(findChild(page,"litecoinLightPaymentError").visible);dexEngine.lightFeeAccepted=true
        findChild(page,"litecoinLightFeeTarget").text="1009";wait(0);verify(!button.enabled)
        findChild(page,"litecoinLightFeeTarget").text="6";dexEngine.litecoinLightStatus={configured:false}
    }
    function test_litecoinPaymentConversionIsExact() {
        compare(page.parseLtcAtomic("0.00000001"),"1")
        compare(page.parseLtcAtomic("1.23"),"123000000")
        compare(page.parseLtcAtomic("184467440737.09551615"),"18446744073709551615")
        for (var i=0,values=["184467440737.09551616","1e-8","-1","1.000000001","01.0","."];i<values.length;i++) compare(page.parseLtcAtomic(values[i]),"")
    }
    function test_litecoinPaymentReviewIsExplicitAndBoundToForm() {
        page.selectPair("LTC / XMZ");page.ltcPaymentScan={gap:20,addressesPerBranch:500,historyEntriesPerBranch:1000};dexEngine.lightPaymentRequest={};dexEngine.lightPaymentAccepted=true
        dexEngine.litecoinLightStatus={configured:true,walletConfigured:true,walletPrepareConfigured:true,walletPrepareAvailable:true,network:"regtest",state:"ready",busy:false,wallet:{}}
        var recipient=findChild(page,"litecoinLightPaymentRecipient"),amount=findChild(page,"litecoinLightPaymentAmount"),fee=findChild(page,"litecoinLightPaymentFee")
        var confirmations=findChild(page,"litecoinLightPaymentConfirmations"),button=findChild(page,"litecoinLightPreparePayment"),review=findChild(page,"litecoinLightPaymentReview")
        recipient.text="rltc1qPUBLIC_FIXTURE_ONLY";amount.text="";fee.text="";wait(0);verify(!button.enabled);verify(!review.visible)
        amount.text="0.0004";fee.text="0.00001";confirmations.text="1";wait(0);verify(button.enabled)
        button.clicked();compare(dexEngine.lightPaymentRequest.payment.amountAtomic,"40000");compare(dexEngine.lightPaymentRequest.payment.feeAtomic,"1000")
        compare(dexEngine.lightPaymentRequest.scan,[20,500,1000]);verify(!review.visible)
        var prepared={configured:true,walletConfigured:true,walletPrepareConfigured:true,walletPrepareAvailable:true,network:"regtest",state:"wallet-prepared",busy:false,
            wallet:{prepared:true,signed:false,watchOnly:true,spendable:false,paymentAddress:recipient.text,paymentAtomic:"40000",feeAtomic:"1000",changeAtomic:"59000",changeAddress:"rltc1qPUBLIC_CHANGE_FIXTURE",inputCount:1,txid:new Array(65).join("a")}}
        dexEngine.litecoinLightStatus=prepared;wait(0);verify(review.visible);verify(review.text.indexOf("0.00040000 LTC")>=0);verify(review.text.indexOf("Sign first")>=0)
        var grabbed=false
        findChild(page,"litecoinLightPaymentForm").grabToImage(function(result){verify(result.saveToFile("litecoin-payment-panel.png"));grabbed=true})
        tryVerify(function(){return grabbed},5000)
        amount.text="0.0005";wait(0);verify(!review.visible);compare(review.text,"")
        amount.text="0.0004";dexEngine.litecoinLightStatus={configured:true,walletPrepareConfigured:true,state:"wallet-loading",busy:true,wallet:{}}
        wait(0);verify(!review.visible);verify(!button.enabled)
        dexEngine.litecoinLightStatus={configured:true,walletPrepareConfigured:true,walletPrepareAvailable:true,state:"unknown",busy:false,wallet:{}}
        wait(0);verify(!review.visible);dexEngine.lightPaymentAccepted=false;button.clicked();verify(findChild(page,"litecoinLightPaymentError").visible)
        dexEngine.lightPaymentAccepted=true;page.selectPair("BCH / XMZ");verify(!findChild(page,"litecoinLightPaymentForm").visible)
        dexEngine.litecoinLightStatus={configured:false}
    }
    function test_litecoinSavedRequestLoadsWithoutPreparing() {
        page.selectPair("LTC / XMZ");dexEngine.lightPaymentRequest={};dexEngine.lightRequestLoads=0
        dexEngine.litecoinLightStatus={configured:true,walletPrepareConfigured:true,walletRequestsAvailable:true,walletPrepareAvailable:true,state:"ready",busy:false}
        var find=findChild(page,"litecoinLightLoadRequests"),load=findChild(page,"litecoinLightLoadSelected")
        find.clicked();compare(dexEngine.lightRequestLoads,1);verify(!load.visible)
        dexEngine.litecoinLightStatus={configured:true,walletPrepareConfigured:true,walletRequestsAvailable:true,walletPrepareAvailable:true,state:"wallet-requests",busy:false,
            paymentRequests:[{requestId:"11111111-2222-4333-8444-555555555555",payment:{address:"rltc1qSAVED_FIXTURE",amountAtomic:"123456789",feeAtomic:"1000",minimumChangeAtomic:"500",minimumConfirmations:3},scan:{gap:1,addressesPerBranch:3,historyEntriesPerBranch:10}}]}
        wait(0);verify(load.visible);verify(load.enabled);load.clicked()
        compare(findChild(page,"litecoinLightPaymentRecipient").text,"rltc1qSAVED_FIXTURE")
        compare(findChild(page,"litecoinLightPaymentAmount").text,"1.23456789")
        compare(findChild(page,"litecoinLightPaymentMinimumChange").text,"0.00000500")
        compare(Object.keys(dexEngine.lightPaymentRequest).length,0);verify(!findChild(page,"litecoinLightPaymentReview").visible)
        var grabbed=false
        findChild(page,"litecoinLightPaymentForm").grabToImage(function(result){verify(result.saveToFile("litecoin-saved-request-panel.png"));grabbed=true})
        tryVerify(function(){return grabbed},5000)
        findChild(page,"litecoinLightPreparePayment").clicked();compare(dexEngine.lightPaymentRequest.scan,[1,3,10])
        compare(dexEngine.lightPaymentRequest.payment.amountAtomic,"123456789")
        dexEngine.litecoinLightStatus={configured:false};page.ltcPaymentScan={gap:20,addressesPerBranch:500,historyEntriesPerBranch:1000}
    }
    function test_litecoinSavedReviewRequiresExplicitRefresh() {
        page.selectPair("LTC / XMZ");dexEngine.lightPaymentRequest={};dexEngine.lightReopenRequest={};dexEngine.lightReopenAccepted=true
        var payment={address:"rltc1qSAVED_FIXTURE",amountAtomic:"40000",feeAtomic:"1000",minimumChangeAtomic:"1000",minimumConfirmations:1}
        var review={watchOnly:true,spendable:false,prepared:true,signed:false,paymentAddress:payment.address,paymentAtomic:"40000",feeAtomic:"1000",
            changeAddress:"rltc1qCHANGE_FIXTURE",changeAtomic:"59000",inputCount:1,txid:new Array(65).join("a"),signingCommitment:new Array(65).join("b")}
        var request={requestId:"11111111-2222-4333-8444-555555555555",payment:payment,scan:{gap:1,addressesPerBranch:3,historyEntriesPerBranch:10},review:review}
        dexEngine.litecoinLightStatus={configured:true,walletPrepareConfigured:true,walletRequestsAvailable:true,walletReopenAvailable:true,state:"wallet-requests",busy:false,paymentRequests:[request]}
        findChild(page,"litecoinLightLoadSelected").clicked();wait(0)
        var button=findChild(page,"litecoinLightReopenPayment"),display=findChild(page,"litecoinLightPaymentReview"),amount=findChild(page,"litecoinLightPaymentAmount")
        verify(button.visible);verify(button.enabled);verify(!display.visible)
        compare(Object.keys(dexEngine.lightReopenRequest).length,0);compare(Object.keys(dexEngine.lightPaymentRequest).length,0)
        amount.text="0.0005";wait(0);verify(!button.visible);amount.text="0.0004";wait(0);verify(button.visible)
        button.clicked();compare(dexEngine.lightReopenRequest.requestId,request.requestId)
        compare(dexEngine.lightReopenRequest.review,{signingCommitment:review.signingCommitment,changeAddress:review.changeAddress})
        compare(dexEngine.lightReopenRequest.scan,[1,3,10]);verify(!display.visible)
        dexEngine.litecoinLightStatus={configured:true,walletPrepareConfigured:true,walletReopenAvailable:true,network:"regtest",state:"wallet-reopened",busy:false,wallet:review}
        wait(0);verify(display.visible);verify(display.text.indexOf(review.txid)>=0)
        var grabbed=false
        findChild(page,"litecoinLightPaymentForm").grabToImage(function(result){verify(result.saveToFile("litecoin-reopened-review-panel.png"));grabbed=true})
        tryVerify(function(){return grabbed},5000)
        dexEngine.litecoinLightStatus={configured:true,walletPrepareConfigured:true,state:"wallet-loading",busy:true,wallet:{}}
        wait(0);verify(!button.enabled);verify(!display.visible)
        dexEngine.litecoinLightStatus={configured:true,walletPrepareConfigured:true,walletReopenAvailable:true,state:"unknown",busy:false,wallet:{}}
        dexEngine.lightReopenAccepted=false;button.clicked();verify(findChild(page,"litecoinLightPaymentError").visible);verify(!display.visible)
        dexEngine.lightReopenAccepted=true;page.selectPair("BCH / XMZ");page.selectPair("LTC / XMZ");verify(!button.visible)
        dexEngine.litecoinLightStatus={configured:false};page.ltcPaymentScan={gap:20,addressesPerBranch:500,historyEntriesPerBranch:1000}
    }
    function test_litecoinSigningNeedsRefreshedReviewConsentAndPassword() {
        page.selectPair("LTC / XMZ");dexEngine.lightSigningRequest={};dexEngine.lightSigningAccepted=true
        var payment={address:"rltc1qSAVED_FIXTURE",amountAtomic:"40000",feeAtomic:"1000",minimumChangeAtomic:"1000",minimumConfirmations:1}
        var review={watchOnly:true,spendable:false,prepared:true,signed:false,paymentAddress:payment.address,paymentAtomic:"40000",feeAtomic:"1000",
            changeAddress:"rltc1qCHANGE_FIXTURE",changeAtomic:"59000",inputCount:1,txid:new Array(65).join("a"),signingCommitment:new Array(65).join("b")}
        var request={requestId:"11111111-2222-4333-8444-555555555555",payment:payment,scan:{gap:1,addressesPerBranch:3,historyEntriesPerBranch:10},review:review}
        dexEngine.litecoinLightStatus={configured:true,walletPrepareConfigured:true,walletReopenAvailable:true,state:"wallet-requests",busy:false,paymentRequests:[request]}
        findChild(page,"litecoinLightLoadSelected").clicked();findChild(page,"litecoinLightReopenPayment").clicked()
        var password=findChild(page,"litecoinLightSigningPassword"),consent=findChild(page,"litecoinLightSigningConsent"),button=findChild(page,"litecoinLightSignPayment")
        var receipt=findChild(page,"litecoinLightSignedPayment"),amount=findChild(page,"litecoinLightPaymentAmount")
        verify(!button.visible);compare(Object.keys(dexEngine.lightSigningRequest).length,0)
        var current={configured:true,walletPrepareConfigured:true,walletSignConfigured:true,walletSignAvailable:true,walletReopenAvailable:true,network:"regtest",state:"wallet-reopened",busy:false,wallet:review}
        dexEngine.litecoinLightStatus=current;wait(0);verify(button.visible);verify(password.password);verify(!button.enabled)
        var invalid=JSON.parse(JSON.stringify(current));invalid.wallet.signed=true;dexEngine.litecoinLightStatus=invalid;wait(0);verify(!button.visible)
        dexEngine.litecoinLightStatus=current;wait(0);verify(button.visible)
        password.text="PUBLIC TEST PASSWORD";wait(0);verify(!button.enabled);consent.checked=true;wait(0);verify(button.enabled)
        var approvalGrabbed=false
        findChild(page,"litecoinLightPaymentForm").grabToImage(function(result){verify(result.saveToFile("litecoin-signing-approval-panel.png"));approvalGrabbed=true})
        tryVerify(function(){return approvalGrabbed},5000)
        amount.text="0.0005";wait(0);verify(!button.visible);compare(password.text,"");verify(!consent.checked)
        amount.text="0.0004";wait(0);password.text="PUBLIC TEST PASSWORD";consent.checked=true;button.clicked()
        compare(dexEngine.lightSigningRequest,{commitment:review.signingCommitment,password:"PUBLIC TEST PASSWORD"});compare(password.text,"");verify(!consent.checked)
        var signed=JSON.parse(JSON.stringify(review));signed.watchOnly=false;signed.signed=true;signed.broadcast=false
        dexEngine.litecoinLightStatus={configured:true,walletPrepareConfigured:true,state:"wallet-signed",busy:false,wallet:signed}
        wait(0);verify(receipt.visible);verify(receipt.text.indexOf("Not broadcast")>=0);verify(!findChild(page,"litecoinLightPaymentReview").visible);verify(!button.visible)
        var grabbed=false
        findChild(page,"litecoinLightPaymentForm").grabToImage(function(result){verify(result.saveToFile("litecoin-signed-payment-panel.png"));grabbed=true})
        tryVerify(function(){return grabbed},5000)
        dexEngine.litecoinLightStatus=current;wait(0);password.text="PUBLIC TEST PASSWORD";consent.checked=true
        dexEngine.litecoinLightStatus={configured:true,walletPrepareConfigured:true,state:"wallet-loading",busy:true,wallet:{}}
        wait(0);compare(password.text,"");verify(!consent.checked);verify(!receipt.visible)
        dexEngine.litecoinLightStatus=current;wait(0);password.text="PUBLIC TEST PASSWORD";consent.checked=true;dexEngine.lightSigningAccepted=false;button.clicked()
        compare(password.text,"");verify(!consent.checked);verify(findChild(page,"litecoinLightPaymentError").visible);verify(!button.visible)
        dexEngine.lightSigningAccepted=true;dexEngine.litecoinLightStatus={configured:false};page.selectPair("BCH / XMZ")
        page.ltcPaymentScan={gap:20,addressesPerBranch:500,historyEntriesPerBranch:1000}
    }
    function test_litecoinSignedRecoveryIsExplicitAndDoesNotClaimBroadcast() {
        page.selectPair("LTC / XMZ");dexEngine.lightSignedRecoveryRequest={};dexEngine.lightSignedRecoveryAccepted=true
        var payment={address:"rltc1qSAVED_FIXTURE",amountAtomic:"40000",feeAtomic:"1000",minimumChangeAtomic:"1000",minimumConfirmations:1}
        var review={watchOnly:true,spendable:false,prepared:true,signed:false,paymentAddress:payment.address,paymentAtomic:"40000",feeAtomic:"1000",
            changeAddress:"rltc1qCHANGE_FIXTURE",changeAtomic:"59000",inputCount:1,txid:new Array(65).join("a"),signingCommitment:new Array(65).join("b")}
        var request={requestId:"11111111-2222-4333-8444-555555555555",payment:payment,scan:{gap:1,addressesPerBranch:3,historyEntriesPerBranch:10},review:review}
        dexEngine.litecoinLightStatus={configured:true,walletPrepareConfigured:true,walletSignedRecoveryAvailable:true,state:"wallet-requests",busy:false,paymentRequests:[request]}
        findChild(page,"litecoinLightLoadSelected").clicked();wait(0)
        var button=findChild(page,"litecoinLightRecoverSignedPayment"),receipt=findChild(page,"litecoinLightRecoveredSignedPayment"),amount=findChild(page,"litecoinLightPaymentAmount")
        verify(button.enabled);verify(!receipt.visible);compare(Object.keys(dexEngine.lightSignedRecoveryRequest).length,0)
        button.clicked();compare(dexEngine.lightSignedRecoveryRequest,{requestId:request.requestId,payment:payment,review:review});verify(!receipt.visible)
        var recovered=JSON.parse(JSON.stringify(review));recovered.signed=true;recovered.recovered=true;recovered.broadcastStatus="unassessed"
        var status={configured:true,walletPrepareConfigured:true,walletSignedRecoveryAvailable:true,state:"wallet-signed-recovered",busy:false,wallet:recovered}
        dexEngine.litecoinLightStatus=status;wait(0);verify(receipt.visible);verify(receipt.text.indexOf("not been checked")>=0)
        verify(!findChild(page,"litecoinLightSignedPayment").visible);verify(!findChild(page,"litecoinLightSignPayment").visible)
        var grabbed=false
        findChild(page,"litecoinLightPaymentForm").grabToImage(function(result){verify(result.saveToFile("litecoin-recovered-signed-panel.png"));grabbed=true})
        tryVerify(function(){return grabbed},5000)
        amount.text="0.0005";wait(0);verify(!receipt.visible);verify(!button.visible);amount.text="0.0004"
        dexEngine.litecoinLightStatus={configured:true,walletPrepareConfigured:true,state:"wallet-loading",busy:true,wallet:{}}
        wait(0);verify(!button.enabled);verify(!receipt.visible)
        dexEngine.litecoinLightStatus=status;dexEngine.lightSignedRecoveryAccepted=false;button.clicked();verify(!receipt.visible);verify(findChild(page,"litecoinLightPaymentError").visible)
        dexEngine.lightSignedRecoveryAccepted=true;dexEngine.litecoinLightStatus={configured:false};page.selectPair("BCH / XMZ")
        page.ltcPaymentScan={gap:20,addressesPerBranch:500,historyEntriesPerBranch:1000}
    }
    function lightSendFixture() {
        page.selectPair("LTC / XMZ");dexEngine.lightSendRequest={};dexEngine.lightSendStatusRequest={}
        dexEngine.lightSendAccepted=true;dexEngine.lightSendStatusAccepted=true
        var payment={address:"rltc1qSAVED_FIXTURE",amountAtomic:"40000",feeAtomic:"1000",minimumChangeAtomic:"1000",minimumConfirmations:1}
        var review={watchOnly:true,spendable:false,prepared:true,signed:false,paymentAddress:payment.address,paymentAtomic:"40000",feeAtomic:"1000",
            changeAddress:"rltc1qCHANGE_FIXTURE",changeAtomic:"59000",inputCount:1,txid:new Array(65).join("a"),signingCommitment:new Array(65).join("b")}
        var request={requestId:"11111111-2222-4333-8444-555555555555",payment:payment,scan:{gap:1,addressesPerBranch:3,historyEntriesPerBranch:10},review:review}
        dexEngine.litecoinLightStatus={configured:true,walletPrepareConfigured:true,walletBroadcastStatusAvailable:true,state:"wallet-requests",busy:false,paymentRequests:[request]}
        findChild(page,"litecoinLightLoadSelected").clicked();wait(0)
        compare(Object.keys(dexEngine.lightSendRequest).length,0);compare(Object.keys(dexEngine.lightSendStatusRequest).length,0)
        findChild(page,"litecoinLightCheckSendStatus").clicked()
        compare(dexEngine.lightSendStatusRequest,{requestId:request.requestId,payment:payment,review:review,scan:[1,3,10]})
        compare(Object.keys(dexEngine.lightSendRequest).length,0)
        var wallet=JSON.parse(JSON.stringify(review));wallet.signed=true;wallet.recovered=true;wallet.broadcastStatus="no-local-attempt"
        wallet.broadcastEndpoint={host:"localhost",port:50002};wallet.confirmationStatus="unassessed"
        return {configured:true,network:"regtest",walletPrepareConfigured:true,walletBroadcastStatusAvailable:true,walletBroadcastAvailable:true,
            state:"wallet-broadcast-status",busy:false,wallet:wallet}
    }
    function endLightSendFixture() {
        dexEngine.litecoinLightStatus={configured:false};page.selectPair("BCH / XMZ")
        page.ltcPaymentScan={gap:20,addressesPerBranch:500,historyEntriesPerBranch:1000}
    }
    function test_litecoinSendRequiresSeparateConsentAndExactDisplayedContext() {
        var status=lightSendFixture();dexEngine.litecoinLightStatus=status;wait(0)
        var button=findChild(page,"litecoinLightSendPayment"),consent=findChild(page,"litecoinLightSendConsent")
        var receipt=findChild(page,"litecoinLightSendStatus"),amount=findChild(page,"litecoinLightPaymentAmount")
        verify(receipt.visible);verify(button.visible);verify(!button.enabled);verify(receipt.text.indexOf("No send attempt")>=0)
        verify(receipt.text.indexOf("localhost:50002")>=0);verify(receipt.text.indexOf("Confirmation has not been checked")>=0)
        button.clicked();compare(Object.keys(dexEngine.lightSendRequest).length,0)
        consent.checked=true;wait(0);verify(button.enabled)
        var grabbed=false
        findChild(page,"litecoinLightPaymentForm").grabToImage(function(result){verify(result.saveToFile("litecoin-send-approval-panel.png"));grabbed=true})
        tryVerify(function(){return grabbed},5000)
        amount.text="0.0005";wait(0);verify(!button.visible);verify(!receipt.visible);verify(!consent.checked)
        amount.text="0.0004";wait(0);consent.checked=true
        var changed=JSON.parse(JSON.stringify(status));changed.wallet.broadcastEndpoint.port=50003
        dexEngine.litecoinLightStatus=changed;wait(0);verify(!consent.checked)
        dexEngine.litecoinLightStatus=status;wait(0);consent.checked=true;button.clicked()
        compare(dexEngine.lightSendRequest,{commitment:status.wallet.signingCommitment,txid:status.wallet.txid,endpoint:status.wallet.broadcastEndpoint})
        verify(!consent.checked)
        dexEngine.litecoinLightStatus={configured:true,walletPrepareConfigured:true,state:"wallet-loading",busy:true,wallet:{}}
        wait(0);verify(!button.visible);verify(!receipt.visible)
        dexEngine.litecoinLightStatus=status;wait(0);consent.checked=true;dexEngine.lightSendAccepted=false;button.clicked()
        verify(findChild(page,"litecoinLightPaymentError").visible);verify(!receipt.visible);verify(!consent.checked)
        dexEngine.lightSendAccepted=true;endLightSendFixture()
    }
    function test_litecoinSendUnknownAndAcknowledgedAreNotConfirmations() {
        var status=lightSendFixture();status.wallet.broadcastStatus="outcome-unknown";dexEngine.litecoinLightStatus=status;wait(0)
        var receipt=findChild(page,"litecoinLightSendStatus"),button=findChild(page,"litecoinLightSendPayment"),consent=findChild(page,"litecoinLightSendConsent")
        verify(receipt.visible);verify(receipt.text.indexOf("Send outcome unknown")>=0);verify(!button.enabled)
        consent.checked=true;button.clicked()
        dexEngine.litecoinLightStatus={configured:true,walletPrepareConfigured:true,walletBroadcastStatusAvailable:true,state:"unknown",busy:false,wallet:{}}
        wait(0);verify(findChild(page,"litecoinLightSendUnknown").visible);verify(!receipt.visible);verify(!button.visible)
        findChild(page,"litecoinLightCheckSendStatus").clicked();verify(!findChild(page,"litecoinLightSendUnknown").visible)
        var acknowledged=JSON.parse(JSON.stringify(status));acknowledged.state="wallet-broadcast-acknowledged";acknowledged.walletBroadcastAvailable=false
        acknowledged.wallet.broadcastStatus="server-acknowledged";acknowledged.wallet.watchOnly=false;delete acknowledged.wallet.recovered
        dexEngine.litecoinLightStatus=acknowledged;wait(0)
        verify(receipt.visible);verify(receipt.text.indexOf("not proof of confirmation")>=0);verify(!button.visible)
        verify(!findChild(page,"litecoinLightSignedPayment").visible);verify(!findChild(page,"litecoinLightSignPayment").visible)
        wait(100) // Let the software scene graph settle after the form changes height.
        var grabbed=false
        findChild(page,"litecoinLightPaymentForm").grabToImage(function(result){verify(result.saveToFile("litecoin-send-ack-panel.png"));grabbed=true})
        tryVerify(function(){return grabbed},5000)
        acknowledged.wallet.confirmationStatus="confirmed";dexEngine.litecoinLightStatus=JSON.parse(JSON.stringify(acknowledged));wait(0);verify(!receipt.visible)
        dexEngine.lightSendStatusAccepted=false;findChild(page,"litecoinLightCheckSendStatus").clicked()
        verify(findChild(page,"litecoinLightPaymentError").visible);dexEngine.lightSendStatusAccepted=true;endLightSendFixture()
    }
    function test_litecoinChainCheckIsExplicitDatedAndClearsOldInclusion() {
        dexEngine.lightReconcileRequest={}
        var status=lightSendFixture();status.walletReconcileAvailable=true
        dexEngine.litecoinLightStatus=status;wait(0)
        compare(Object.keys(dexEngine.lightReconcileRequest).length,0)
        var check=findChild(page,"litecoinLightReconcilePayment"),receipt=findChild(page,"litecoinLightPaymentObservation")
        verify(check.enabled);verify(!receipt.visible);check.clicked()
        compare(dexEngine.lightReconcileRequest,{requestId:page.ltcLoadedRequest.requestId,payment:page.ltcLoadedRequest.payment,
            review:page.ltcLoadedRequest.review,headers:10000000,history:10})
        status.state="wallet-reconciled";status.walletBroadcastAvailable=false;status.wallet.confirmationStatus="included"
        status.wallet.paymentObservation={selectedHeight:442,selectedTip:new Array(65).join("c"),height:442,
            blockHash:new Array(65).join("c"),confirmations:1,observedAtUnix:1791676800}
        dexEngine.litecoinLightStatus=JSON.parse(JSON.stringify(status));wait(0)
        verify(receipt.visible);verify(receipt.text.indexOf("Confirmations at this check: 1")>=0)
        verify(receipt.text.indexOf("2026-10-11T00:00:00.000Z")>=0);verify(receipt.text.indexOf("Selected height: 442")>=0)
        verify(!findChild(page,"litecoinLightSendPayment").visible);verify(!findChild(page,"litecoinLightSignPayment").visible)
        wait(100)
        var grabbed=false
        findChild(page,"litecoinLightPaymentForm").grabToImage(function(result){verify(result.saveToFile("litecoin-chain-observation-panel.png"));grabbed=true})
        tryVerify(function(){return grabbed},5000)
        check.clicked();status.busy=true;status.state="wallet-loading"
        dexEngine.litecoinLightStatus=JSON.parse(JSON.stringify(status));wait(0);verify(!receipt.visible)
        status.busy=false;status.state="wallet-reconciled";status.wallet.confirmationStatus="not-reported"
        status.wallet.paymentObservation={selectedHeight:443,selectedTip:new Array(65).join("d"),height:null,blockHash:null,confirmations:0,observedAtUnix:1791676801}
        dexEngine.litecoinLightStatus=JSON.parse(JSON.stringify(status));wait(0)
        verify(receipt.visible);verify(receipt.text.indexOf("did not report")>=0);verify(receipt.text.indexOf("Confirmations at this check")<0)
        status.wallet.confirmationStatus="reported-pending";dexEngine.litecoinLightStatus=JSON.parse(JSON.stringify(status));wait(0)
        verify(receipt.text.indexOf("Block inclusion has not been verified")>=0)
        findChild(page,"litecoinLightPaymentAmount").text="0.0005";wait(0);verify(!receipt.visible)
        findChild(page,"litecoinLightPaymentAmount").text="0.0004";wait(0);check.clicked()
        status.state="unknown";status.wallet={};dexEngine.litecoinLightStatus=JSON.parse(JSON.stringify(status));wait(0)
        verify(!receipt.visible);verify(findChild(page,"litecoinLightReconcileUnknown").visible)
        dexEngine.lightReconcileAccepted=false;check.clicked();verify(findChild(page,"litecoinLightPaymentError").visible)
        dexEngine.lightReconcileAccepted=true;endLightSendFixture()
    }
    function test_litecoinBackupExplicitControlsClearPasswordsAndBindDestination() {
        page.selectPair("LTC / XMZ");dexEngine.lightBackupRequest={}
        var status={configured:true,walletConfigured:true,walletBackupConfigured:true,walletBackupAvailable:true,state:"ready",busy:false,wallet:{}}
        dexEngine.litecoinLightStatus=status;wait(0)
        var directory=findChild(page,"litecoinLightBackupDirectory"),password=findChild(page,"litecoinLightBackupPassword")
        var save=findChild(page,"litecoinLightBackupSave"),verifyBackup=findChild(page,"litecoinLightBackupVerify")
        var receipt=findChild(page,"litecoinLightBackupReceipt"),error=findChild(page,"litecoinLightBackupError")
        verify(findChild(page,"litecoinLightBackupForm").visible);verify(password.password);verify(!save.enabled)
        directory.text="/PUBLIC-fixture/backup";password.text="PUBLIC TEST PASSWORD";wait(0)
        compare(Object.keys(dexEngine.lightBackupRequest).length,0);verify(save.enabled);save.clicked()
        compare(dexEngine.lightBackupRequest,{directory:directory.text,password:"PUBLIC TEST PASSWORD",save:true});compare(password.text,"")
        status.state="wallet-backup-saved";status.backupDirectory=directory.text
        status.wallet={backupVerified:true,backupSaved:true,walletId:"11111111-2222-4333-8444-555555555555",backupDigest:new Array(65).join("a"),receivingAllocated:1,changeAllocated:2}
        dexEngine.litecoinLightStatus=JSON.parse(JSON.stringify(status));wait(0)
        verify(receipt.visible);verify(receipt.text.indexOf("saved and verified")>=0)
        verify(receipt.text.indexOf("Active swap recovery is not included")>=0);verify(!findChild(page,"litecoinLightSignPayment").visible)
        wait(100)
        var grabbed=false
        findChild(page,"litecoinLightBackupForm").grabToImage(function(result){verify(result.saveToFile("litecoin-backup-panel.png"));grabbed=true})
        tryVerify(function(){return grabbed},5000)
        password.text="PUBLIC TEST PASSWORD";directory.text="/PUBLIC-fixture/second-backup";wait(0)
        compare(password.text,"");verify(!receipt.visible)
        password.text="PUBLIC VERIFY PASSWORD";verifyBackup.clicked()
        compare(dexEngine.lightBackupRequest,{directory:directory.text,password:"PUBLIC VERIFY PASSWORD",save:false});compare(password.text,"")
        status.state="wallet-loading";status.busy=true;dexEngine.litecoinLightStatus=JSON.parse(JSON.stringify(status));wait(0)
        verify(!receipt.visible);verify(!save.enabled);verify(!verifyBackup.enabled)
        status.busy=false;status.state="wallet-backup-verified";status.backupDirectory=directory.text;status.wallet.backupSaved=false
        dexEngine.litecoinLightStatus=JSON.parse(JSON.stringify(status));wait(0)
        verify(receipt.visible);verify(receipt.text.indexOf("Existing encrypted backup verified")>=0)
        password.text="PUBLIC SECRET";status.state="unknown";status.wallet={}
        dexEngine.litecoinLightStatus=JSON.parse(JSON.stringify(status));wait(0)
        compare(password.text,"");verify(!receipt.visible);verify(error.visible);verify(error.text.indexOf("save may have completed")>=0)
        password.text="PUBLIC SECRET";dexEngine.lightBackupAccepted=false;save.clicked()
        compare(password.text,"");verify(error.visible);verify(error.text.indexOf("Could not start")>=0)
        dexEngine.lightBackupAccepted=true;password.text="PUBLIC SECRET";page.selectPair("BCH / XMZ");wait(0);compare(password.text,"")
        directory.text="";dexEngine.litecoinLightStatus={configured:false}
    }
    function test_litecoinRestoreRequiresVerifiedBackupConsentAndDisplayedDestinations() {
        page.selectPair("LTC / XMZ");dexEngine.lightRestoreRequest={}
        var status={configured:true,walletConfigured:true,walletBackupConfigured:true,walletBackupAvailable:true,state:"ready",busy:false,wallet:{},
            restoreDirectories:{vault:"/PUBLIC-fixture/restored-vault",addressBook:"/PUBLIC-fixture/restored-book"}}
        dexEngine.litecoinLightStatus=status;wait(0)
        var directory=findChild(page,"litecoinLightBackupDirectory"),password=findChild(page,"litecoinLightBackupPassword")
        var button=findChild(page,"litecoinLightRestore"),consent=findChild(page,"litecoinLightRestoreConsent")
        var receipt=findChild(page,"litecoinLightRestoreReceipt"),destinations=findChild(page,"litecoinLightRestoreDestinations")
        directory.text="/PUBLIC-fixture/backup";password.text="PUBLIC PASSWORD"
        verify(!button.visible);compare(Object.keys(dexEngine.lightRestoreRequest).length,0)
        findChild(page,"litecoinLightBackupVerify").clicked()
        status.state="wallet-backup-verified";status.walletRestoreAvailable=true;status.backupDirectory=directory.text
        status.wallet={backupVerified:true,backupSaved:false,walletId:"11111111-2222-4333-8444-555555555555",backupDigest:new Array(65).join("a"),receivingAllocated:1,changeAllocated:2}
        dexEngine.litecoinLightStatus=JSON.parse(JSON.stringify(status));wait(0)
        verify(button.visible);verify(!button.enabled);verify(destinations.visible)
        verify(destinations.text.indexOf("/PUBLIC-fixture/restored-vault")>=0);verify(destinations.text.indexOf("/PUBLIC-fixture/restored-book")>=0)
        password.text="PUBLIC PASSWORD";button.clicked();compare(Object.keys(dexEngine.lightRestoreRequest).length,0)
        consent.checked=true;wait(0);verify(button.enabled)
        status.restoreDirectories.vault="/PUBLIC-fixture/changed-vault";dexEngine.litecoinLightStatus=JSON.parse(JSON.stringify(status));wait(0)
        verify(!consent.checked);compare(password.text,"")
        password.text="PUBLIC PASSWORD";consent.checked=true;wait(0)
        var grabbed=false
        findChild(page,"litecoinLightBackupForm").grabToImage(function(result){verify(result.saveToFile("litecoin-restore-approval-panel.png"));grabbed=true})
        tryVerify(function(){return grabbed},5000)
        button.clicked();compare(dexEngine.lightRestoreRequest,{directory:directory.text,digest:status.wallet.backupDigest,password:"PUBLIC PASSWORD"})
        compare(password.text,"");verify(!consent.checked)
        status.state="wallet-loading";status.busy=true;dexEngine.litecoinLightStatus=JSON.parse(JSON.stringify(status));wait(0)
        verify(!button.visible);verify(!receipt.visible)
        status.state="unknown";status.busy=false;status.wallet={};dexEngine.litecoinLightStatus=JSON.parse(JSON.stringify(status));wait(0)
        verify(findChild(page,"litecoinLightBackupError").text.indexOf("Preserve any recovered records")>=0);verify(!receipt.visible)
        password.text="PUBLIC PASSWORD";findChild(page,"litecoinLightBackupVerify").clicked()
        status.state="wallet-backup-verified";status.wallet={backupVerified:true,backupSaved:false,walletId:"11111111-2222-4333-8444-555555555555",
            backupDigest:new Array(65).join("a"),receivingAllocated:1,changeAllocated:2}
        dexEngine.litecoinLightStatus=JSON.parse(JSON.stringify(status));wait(0)
        password.text="PUBLIC PASSWORD";consent.checked=true;dexEngine.lightRestoreAccepted=false;button.clicked()
        verify(findChild(page,"litecoinLightBackupError").text.indexOf("Could not start restoration")>=0);compare(password.text,"");verify(!consent.checked)
        dexEngine.lightRestoreAccepted=true;password.text="PUBLIC PASSWORD";findChild(page,"litecoinLightBackupVerify").clicked()
        dexEngine.litecoinLightStatus=JSON.parse(JSON.stringify(status));wait(0);password.text="PUBLIC PASSWORD";consent.checked=true;button.clicked()
        status.state="wallet-restored";status.walletRestoreAvailable=false;status.wallet.restored=true
        delete status.wallet.backupVerified;delete status.wallet.backupSaved
        dexEngine.litecoinLightStatus=JSON.parse(JSON.stringify(status));wait(0)
        verify(receipt.visible);verify(receipt.text.indexOf("restored and verified")>=0);verify(receipt.text.indexOf("active swap recovery are not complete")>=0)
        verify(!button.visible);verify(!findChild(page,"litecoinLightSignPayment").visible)
        directory.text="/PUBLIC-fixture/other";wait(0);verify(!receipt.visible)
        directory.text="";page.selectPair("BCH / XMZ");dexEngine.litecoinLightStatus={configured:false}
    }
    function test_litecoinCreationRequiresReviewPasswordConfirmationAndExplicitOpen() {
        page.selectPair("LTC / XMZ");dexEngine.lightCreateRequest={};dexEngine.lightCreatedOpenDigest="";dexEngine.lightCreatedReviews=0
        var settings={network:"regtest",walletId:"11111111-2222-4333-8444-555555555555",accountIndex:0,maximumHeight:1000,
            setupDirectory:"/PUBLIC-fixture/setup",vaultDirectory:"/PUBLIC-fixture/vault",addressBookDirectory:"/PUBLIC-fixture/book",
            backupDirectory:"/PUBLIC-fixture/backup",spendDirectory:"/PUBLIC-fixture/spends",cacheDirectory:"/PUBLIC-fixture/cache"}
        var status={configured:true,network:"regtest",setupConfigured:true,creation:settings,creationDigest:new Array(65).join("a"),
            walletCreateAvailable:true,walletCreatedReviewAvailable:true,walletCreatedOpenAvailable:false,state:"setup-ready",busy:false,wallet:{}}
        dexEngine.litecoinLightStatus=status;wait(0)
        var password=findChild(page,"litecoinLightCreatePassword"),confirmation=findChild(page,"litecoinLightCreateConfirmation")
        var consent=findChild(page,"litecoinLightCreateConsent"),create=findChild(page,"litecoinLightCreate")
        var receipt=findChild(page,"litecoinLightCreatedReceipt"),open=findChild(page,"litecoinLightCreatedOpen")
        verify(findChild(page,"litecoinLightCreateForm").visible);verify(password.password);verify(confirmation.password)
        verify(findChild(page,"litecoinLightCreateDetails").text.indexOf("/PUBLIC-fixture/backup")>=0)
        compare(Object.keys(dexEngine.lightCreateRequest).length,0);verify(!create.enabled);verify(!open.visible)
        password.text="PUBLIC PASSWORD";confirmation.text="OTHER";consent.checked=true;wait(0);verify(!create.enabled)
        confirmation.text=password.text;wait(0);verify(create.enabled)
        var grabbed=false
        findChild(page,"litecoinLightCreateForm").grabToImage(function(result){verify(result.saveToFile("litecoin-create-approval-panel.png"));grabbed=true})
        tryVerify(function(){return grabbed},5000)
        create.clicked();compare(dexEngine.lightCreateRequest,{digest:status.creationDigest,password:"PUBLIC PASSWORD",confirmation:"PUBLIC PASSWORD"})
        compare(password.text,"");compare(confirmation.text,"");verify(!consent.checked)
        compare(dexEngine.lightCreatedOpenDigest,"")
        status.state="wallet-loading";status.busy=true;status.walletCreateAvailable=false;status.walletCreatedReviewAvailable=false
        dexEngine.litecoinLightStatus=JSON.parse(JSON.stringify(status));wait(0);verify(!receipt.visible);verify(!open.visible)
        status.state="wallet-created";status.busy=false;status.walletCreatedReviewAvailable=true;status.walletCreatedOpenAvailable=true
        status.wallet={walletId:settings.walletId,configurationFile:"/PUBLIC-fixture/setup/litecoin-wallet.json",configurationDigest:new Array(65).join("b"),backupDigest:new Array(65).join("c")}
        dexEngine.litecoinLightStatus=JSON.parse(JSON.stringify(status));wait(0)
        verify(receipt.visible);verify(receipt.text.indexOf("Initial encrypted backup verified")>=0);verify(open.enabled)
        compare(dexEngine.lightCreatedOpenDigest,"");open.clicked();compare(dexEngine.lightCreatedOpenDigest,status.wallet.configurationDigest)
        findChild(page,"litecoinLightCreatedReview").clicked();compare(dexEngine.lightCreatedReviews,1)
        status.state="wallet-created-review";dexEngine.litecoinLightStatus=JSON.parse(JSON.stringify(status));wait(0)
        verify(receipt.text.indexOf("Backup contents have not been rechecked")>=0)
        dexEngine.lightCreatedOpenAccepted=false;open.clicked();verify(findChild(page,"litecoinLightCreateError").visible)
        dexEngine.lightCreatedOpenAccepted=true;dexEngine.lightCreatedReviewAccepted=false;findChild(page,"litecoinLightCreatedReview").clicked()
        verify(findChild(page,"litecoinLightCreateError").text.indexOf("No replacement wallet was created")>=0)
        dexEngine.lightCreatedReviewAccepted=true
        status.state="unknown";status.wallet={};status.walletCreatedOpenAvailable=false;status.walletCreateAvailable=true
        dexEngine.litecoinLightStatus=JSON.parse(JSON.stringify(status));wait(0);verify(!receipt.visible);verify(!open.visible)
        password.text="PUBLIC PASSWORD";confirmation.text=password.text;consent.checked=true
        dexEngine.lightCreateAccepted=false;create.clicked();compare(password.text,"");compare(confirmation.text,"");verify(!consent.checked)
        verify(findChild(page,"litecoinLightCreateError").visible);dexEngine.lightCreateAccepted=true
        password.text="PUBLIC PASSWORD";confirmation.text=password.text;consent.checked=true
        status.creationDigest=new Array(65).join("d");dexEngine.litecoinLightStatus=JSON.parse(JSON.stringify(status));wait(0)
        compare(password.text,"");compare(confirmation.text,"");verify(!consent.checked)
        password.text="PUBLIC PASSWORD";confirmation.text=password.text;consent.checked=true;page.selectPair("BCH / XMZ");wait(0)
        compare(password.text,"");compare(confirmation.text,"");verify(!consent.checked)
        dexEngine.litecoinLightStatus={configured:false}
    }
    function startupReview(commitment) {
        return {commitment:commitment,expiresAtUtc:"2030-01-01T12:00:00Z",
            details:{role:"native-funder",terms:{xmzRefund:"XMZ_TEST_REFUND_ADDRESS",ltcPayout:"LTC_TEST_PAYOUT_ADDRESS",
                ltcRefund:"LTC_TEST_REFUND_ADDRESS",xmzPayout:"XMZ_TEST_PAYOUT_ADDRESS",
                ltcConfirmations:"2",xmzConfirmations:"10",cancelBlocks:"20",punishBlocks:"10",
                ltcFunder:new Array(65).join("a"),xmzFunder:new Array(65).join("b")}},
            amounts:{ltcAtoms:"184467440737.09551615",xmzAtoms:"2",xmzPayoutAtoms:"1",ltcFundingFeeMaxAtoms:"0.001",
                xmzNativeFeeMaxAtoms:"0.1",xmzClaimFeeMaxAtoms:"0.1",xmzRefundFeeMaxAtoms:"0.1",
                ltcRedeemFeeAtoms:"0.0001",ltcCancelFeeAtoms:"0.0001",ltcRefundFeeAtoms:"0.0001",
                ltcEarlyRefundFeeAtoms:"0.0001",ltcPunishFeeAtoms:"0.0001",appFeeXmzAtoms:"0"}};
    }
    function test_bchSigningRecoveryExplicitConsentAndReset() {
        page.selectPair("BCH / XMZ")
        var review=findChild(page,"bchSigningRecoveryReview"),details=findChild(page,"bchSigningRecoveryDetails")
        var consent=findChild(page,"bchSigningRecoveryConsent"),submit=findChild(page,"bchSigningRecoverySubmit")
        var funding=findChild(page,"bchSigningDetails")
        verify(review && details && consent && submit && funding)
        dexEngine.bchPreparationStatus={configured:true,recoveryConfigured:true,recoveryReviewAvailable:true}
        verify(review.enabled);verify(!submit.enabled);review.clicked();compare(dexEngine.bchRecoveryReviews,1)
        var recovered={commitment:"recovery-review",details:{action:"retain-recovered-signature-only",fundingTxid:"recovered-id",contractVout:0,
            originalApprovedAt:100,signingReview:{action:"sign-only",feeAtoms:1000,
                inputs:[{txid:"original-parent",vout:0,atoms:111000,script:"input-script"}],
                outputs:[{vout:0,atoms:100000,script:"original-contract"}]}}}
        dexEngine.bchPreparationStatus={configured:true,recoveryConfigured:true,recoverySubmitAvailable:true,
            recoveryReview:recovered,result:{review:recovered}}
        verify(details.visible);verify(details.text.indexOf("recovered-id:0")>=0)
        verify(funding.visible);verify(funding.text.indexOf("original-parent:0")>=0);verify(funding.text.indexOf("original-contract")>=0)
        verify(!submit.enabled);consent.checked=true;verify(submit.enabled);submit.clicked()
        compare(dexEngine.bchImportedCommitment,"recovery-review");verify(!consent.checked)
        dexEngine.bchPreparationStatus={configured:true,recoveryConfigured:true,busy:true};verify(!submit.enabled);verify(!details.visible)
        dexEngine.bchPreparationStatus={configured:true,recoveryConfigured:true,recoverySubmitAvailable:true,recoveryReview:recovered}
        consent.checked=true;page.selectPair("BTC / XMZ");verify(!consent.checked)
        dexEngine.bchPreparationStatus={configured:false}
    }
    function test_bchSigningConsentAndUncertainResult() {
        page.selectPair("BCH / XMZ")
        var review=findChild(page,"bchSigningReview"),check=findChild(page,"bchSigningCheck")
        var consent=findChild(page,"bchSigningConsent"),submit=findChild(page,"bchSigningSubmit")
        var history=findChild(page,"bchSigningHistory"),details=findChild(page,"bchSigningDetails")
        verify(review && check && consent && submit && history && details)
        dexEngine.bchPreparationStatus={configured:true,signingReviewAvailable:true,signingCheckAvailable:true}
        review.clicked();compare(dexEngine.bchSigningReviews,1);check.clicked();compare(dexEngine.bchSigningChecks,1)
        var shown={commitment:"sign-review",details:{action:"sign-only",swapId:"trade",network:"bch-regtest",feeAtoms:1000,
            inputs:[{txid:"parent",vout:0,atoms:111000,script:"input-script"}],
            outputs:[{vout:0,atoms:100000,script:"contract-script"},{vout:1,atoms:10000,script:"change-script"}]}}
        dexEngine.bchPreparationStatus={configured:true,signingSubmitAvailable:true,signingReview:shown,result:{review:shown}}
        verify(details.visible);verify(details.text.indexOf("parent:0")>=0);verify(details.text.indexOf("Funding fee: 0.00001 BCH")>=0)
        verify(details.text.indexOf("contract-script")>=0);verify(details.text.indexOf("change-script")>=0)
        verify(!submit.enabled);consent.checked=true;verify(submit.enabled);submit.clicked()
        compare(dexEngine.bchSignedCommitment,"sign-review");verify(!consent.checked)
        dexEngine.bchPreparationStatus={configured:true,busy:true,state:"signing"};verify(!submit.enabled);verify(!details.visible)
        dexEngine.bchPreparationStatus={configured:true,signingStatusKnown:true,signingIntent:{commitment:"intent",approvedAt:"100"}}
        verify(history.visible);verify(history.text.indexOf("do not sign again")>=0);verify(!submit.enabled)
        dexEngine.bchPreparationStatus={configured:true,signingStatusKnown:true,signedFunding:{txid:"signed-id",contractVout:0}}
        verify(history.text.indexOf("signed-id:0")>=0);verify(history.text.indexOf("not proof of broadcast")>=0)
        dexEngine.bchPreparationStatus={configured:true,state:"unknown"};verify(!history.visible)
        dexEngine.bchPreparationStatus={configured:true,signingSubmitAvailable:true,signingReview:shown}
        consent.checked=true;page.selectPair("BTC / XMZ");verify(!consent.checked)
        dexEngine.bchPreparationStatus={configured:false}
    }
    function test_bchLockConsentAndHistoricalEvidence() {
        page.selectPair("BCH / XMZ")
        var review=findChild(page,"bchLockReview"),check=findChild(page,"bchLockCheck")
        var consent=findChild(page,"bchLockConsent"),submit=findChild(page,"bchLockSubmit")
        var history=findChild(page,"bchLockHistory"),observation=findChild(page,"bchLockObservation")
        verify(review && check && consent && submit && history && observation)
        dexEngine.bchPreparationStatus={configured:true,lockReviewAvailable:true,lockCheckAvailable:true}
        verify(!submit.enabled);review.clicked();compare(dexEngine.bchLockReviews,1)
        dexEngine.bchPreparationStatus={configured:true,state:"lock-reviewed",lockSubmitAvailable:true,lockReview:{commitment:"approved-lock"}}
        verify(!submit.enabled);consent.checked=true;verify(submit.enabled)
        submit.clicked();compare(dexEngine.bchLockedCommitment,"approved-lock");verify(!consent.checked)
        dexEngine.bchPreparationStatus={configured:true,state:"locking",busy:true};verify(!submit.enabled)
        dexEngine.bchPreparationStatus={configured:true,state:"locks-checked",lockStatusKnown:true,chainObserved:true,lockCheckAvailable:true,
            lockIntent:{state:"recorded",commitment:"original-intent",recordedAt:"100"},
            lockObservation:{walletInputsLocked:true,tip:"checked-tip",observedAt:"101"}}
        verify(history.visible);verify(observation.visible);verify(history.text.indexOf("original-intent")>=0)
        check.clicked();compare(dexEngine.bchLockChecks,1)
        dexEngine.bchPreparationStatus={configured:true,state:"lock-status",lockStatusKnown:true,
            lockIntent:{state:"recorded",commitment:"original-intent",recordedAt:"100"}}
        verify(history.visible);verify(!observation.visible);verify(!submit.enabled)
        dexEngine.bchPreparationStatus={configured:true,state:"unknown"};verify(!history.visible);verify(!observation.visible)
        dexEngine.bchPreparationStatus={configured:true,state:"lock-reviewed",lockSubmitAvailable:true,lockReview:{commitment:"fresh-lock"}}
        consent.checked=true;page.selectPair("BTC / XMZ");verify(!consent.checked)
        dexEngine.bchPreparationStatus={configured:false}
    }
    function test_bchPreparationExplicitActionsAndSavedDetails() {
        page.selectPair("BCH / XMZ")
        var panel=findChild(page,"bchPreparationPanel"),prepare=findChild(page,"bchPrepare")
        var check=findChild(page,"bchPreparationCheck"),details=findChild(page,"bchPreparationDetails")
        verify(panel && prepare && check && details)
        dexEngine.bchPreparationStatus={configured:true,prepareAvailable:true,checkAvailable:true,state:"ready"}
        compare(dexEngine.bchPrepares,0);compare(dexEngine.bchPreparationChecks,0)
        verify(prepare.enabled);verify(check.enabled)
        prepare.clicked();compare(dexEngine.bchPrepares,1)
        check.clicked();compare(dexEngine.bchPreparationChecks,1)
        dexEngine.bchPreparationStatus={configured:true,state:"prepared",checkAvailable:true,prepareAvailable:false,
            result:{review:{details:{preparation:"saved-preparation",swapId:"trade",network:"bch-regtest",feeAtoms:"1000",preparedAt:"100",
                inputs:[{outpoint:"parent:0",valueAtoms:"111000",script:"input-script"}],
                outputs:[{vout:0,valueAtoms:"100000",script:"contract-script"},{vout:1,valueAtoms:"10000",script:"change-script"}]}}}}
        verify(details.visible);verify(details.text.indexOf("parent:0")>=0)
        verify(details.text.indexOf("contract-script")>=0);verify(details.text.indexOf("change-script")>=0)
        verify(!prepare.enabled);verify(check.enabled)
        dexEngine.bchPreparationStatus={configured:true,state:"checking",busy:true}
        verify(!details.visible);verify(!prepare.enabled);verify(!check.enabled)
        dexEngine.bchPreparationStatus={configured:true,state:"unknown"};verify(!details.visible)
        dexEngine.bchPreparationStatus={configured:false};page.selectPair("BTC / XMZ")
    }
    function test_bchFundingChainEvidenceIsSeparateAndCleared() {
        page.selectPair("BCH / XMZ");
        var button=findChild(page,"bchFundingObserve"),details=findChild(page,"bchFundingObservation");
        var submit=findChild(page,"bchFundingSubmit");verify(button && details && submit);
        var history=findChild(page,"bchFundingRelayHistory");verify(history);
        dexEngine.bchFundingStatus={configured:true,submissionConfigured:true,observeAvailable:true,state:"attempted"};
        tryCompare(button,"visible",true);verify(button.enabled);verify(!details.visible);
        button.clicked();compare(dexEngine.bchFundingObservations,1);
        dexEngine.bchFundingStatus={configured:true,submissionConfigured:true,state:"observed",chainObserved:true,statusKnown:true,relayState:"attempted",
            observation:{state:"confirmed-unspent",txid:"FUNDING",tip:"TIP",confirmations:"3",observedAt:"1791636000",lockOutputUnspent:true}};
        tryCompare(details,"visible",true);verify(details.text.indexOf("3 confirmations")>=0);
        verify(details.text.indexOf("Point-in-time")>=0);verify(!submit.enabled);
        verify(history.visible);verify(history.text.indexOf("uncertain")>=0);
        dexEngine.bchFundingStatus={configured:true,submissionConfigured:true,state:"observed",chainObserved:true,statusKnown:true,relayState:"attempted",
            observation:{state:"unobserved",txid:"FUNDING",tip:"TIP",confirmations:null,observedAt:"1791636001",lockOutputUnspent:null}};
        verify(details.text.indexOf("does not prove it was never sent")>=0);verify(!submit.enabled);
        dexEngine.bchFundingStatus={configured:true,submissionConfigured:true,state:"observing",busy:true};
        tryCompare(details,"visible",false);verify(!button.enabled);
        dexEngine.bchFundingStatus={configured:true,submissionConfigured:true,state:"unknown"};
        verify(!details.visible);verify(!submit.enabled);
        dexEngine.bchFundingStatus={configured:false};page.selectPair("BTC / XMZ");
    }
    function test_bchFundingExplicitConsentAndSavedStatus() {
        page.selectPair("BCH / XMZ");
        var consent=findChild(page,"bchFundingConsent"),submit=findChild(page,"bchFundingSubmit");
        var check=findChild(page,"bchFundingCheck"),details=findChild(page,"bchFundingDetails");
        var outcome=findChild(page,"bchFundingOutcome");
        verify(consent && submit && check && details && outcome);
        var review={commitment:"approved-funding",details:{network:"bch-regtest",fundingTxid:"FUNDING_TXID",
            feeAtoms:1000,contractVout:0,cancelTxid:"CANCEL_TXID",refundTxid:"REFUND_TXID",punishTxid:"PUNISH_TXID",
            expiresAt:"1893499200",swapId:"TRADE",outputs:[{atoms:100000,script:"51"}]}};
        dexEngine.bchFundingStatus={configured:true,submissionConfigured:true,state:"reviewed",checkAvailable:true,
            reviewAvailable:true,submitAvailable:true,review:review};
        tryCompare(consent,"visible",true);verify(!submit.enabled);consent.checked=true;tryCompare(submit,"enabled",true);
        submit.clicked();compare(dexEngine.bchFundingSubmitted,"approved-funding");verify(!consent.checked);
        consent.checked=true;page.selectPair("LTC / XMZ");verify(!consent.checked);page.selectPair("BCH / XMZ");
        consent.checked=true;
        dexEngine.bchFundingStatus={configured:true,submissionConfigured:true,state:"unknown",checkAvailable:true,submitAttempted:true};
        tryCompare(consent,"checked",false);verify(!submit.enabled);verify(!details.visible);
        check.clicked();compare(dexEngine.bchFundingChecks,1);
        dexEngine.bchFundingStatus={configured:true,submissionConfigured:true,state:"attempted",checkAvailable:true,
            savedReview:review,submitAttempted:true};
        tryCompare(details,"visible",true);verify(details.text.indexOf("FUNDING_TXID")>=0);
        verify(outcome.text.indexOf("uncertain")>=0);verify(!submit.enabled);verify(!consent.visible);
        dexEngine.bchFundingStatus={configured:true,submissionConfigured:true,state:"acknowledged",checkAvailable:true,savedReview:review};
        verify(outcome.text.indexOf("confirmation has not been checked")>=0);verify(!submit.enabled);
        dexEngine.bchFundingStatus={configured:false};page.selectPair("BTC / XMZ");
    }
    function test_bchFundingReviewAndUnknownState() {
        page.selectPair("BCH / XMZ");
        var panel=findChild(page,"bchFundingPanel"),button=findChild(page,"bchFundingReview");
        var details=findChild(page,"bchFundingDetails"),outcome=findChild(page,"bchFundingOutcome");
        verify(panel && button && details && outcome);
        dexEngine.bchFundingStatus={configured:true,state:"ready",reviewAvailable:true};
        tryCompare(panel,"visible",true);verify(!details.visible);button.clicked();compare(dexEngine.bchFundingReviews,1);
        dexEngine.bchFundingStatus={configured:true,state:"reviewed",reviewAvailable:true,review:{commitment:"funding-review",
            details:{network:"bch-regtest",fundingTxid:"FUNDING_TXID",feeAtoms:1000,contractVout:0,
                cancelTxid:"CANCEL_TXID",refundTxid:"REFUND_TXID",punishTxid:"PUNISH_TXID",expiresAt:"1893499200",
                swapId:"BCH_TRADE",outputs:[{atoms:100000,script:"51"},{atoms:10000,script:"52"}]}}};
        tryCompare(details,"visible",true);verify(details.text.indexOf("FUNDING_TXID")>=0);
        verify(details.text.indexOf("0.00001 BCH")>=0);verify(details.text.indexOf("REFUND_TXID")>=0);
        verify(details.text.indexOf("undefined")<0);verify(outcome.text.indexOf("does not check the chain")>=0);
        page.selectPair("LTC / XMZ");tryCompare(panel,"visible",false);page.selectPair("BCH / XMZ");
        dexEngine.bchFundingStatus={configured:true,state:"reviewing",busy:true,reviewAvailable:false};
        tryCompare(details,"visible",false);verify(!button.enabled);
        dexEngine.bchFundingStatus={configured:true,state:"unknown",reviewAvailable:true};
        verify(outcome.text.indexOf("unavailable")>=0);verify(!details.visible);
        dexEngine.bchFundingStatus={configured:false};page.selectPair("BTC / XMZ");
    }
    function test_bchAdmissionConsentAndUnknownStatus() {
        page.selectPair("BCH / XMZ");
        var panel=findChild(page,"bchAdmissionPanel"),consent=findChild(page,"bchAdmissionConsent");
        var review=findChild(page,"bchAdmissionReview"),submit=findChild(page,"bchAdmissionSubmit");
        var check=findChild(page,"bchAdmissionCheck"),outcome=findChild(page,"bchAdmissionOutcome");
        verify(panel && consent && review && submit && check && outcome);
        dexEngine.bchAdmissionStatus={configured:true,state:"ready",reviewAvailable:true,checkAvailable:true};
        tryCompare(panel,"visible",true);verify(!submit.enabled);
        review.clicked();compare(dexEngine.bchReviews,1);
        var quote={commitment:"bch-approved",details:{role:"native-funder",terms:{
            bchNetwork:"bch-regtest",xmzNetwork:"xmz-mainnet",bchAtoms:"18446744073709551615",
            xmzAtoms:"200000000000",xmzPayoutAtoms:"100000000000",xmzPayout:"XMZ_PAYOUT_ADDRESS",
            xmzRefund:"XMZ_REFUND_ADDRESS",bchPayoutScript:"BCH_PAYOUT_SCRIPT",bchRefundScript:"BCH_REFUND_SCRIPT",
            bchFundingFeeMaxAtoms:"5000",bchLockFeeAtoms:"1000",bchRecoveryFeeAtoms:"2000",
            xmzFundingFeeMaxAtoms:"1000",xmzClaimFeeMaxAtoms:"1000",xmzRefundFeeMaxAtoms:"1000",
            bchConfirmations:"2",xmzConfirmations:"10",cancelBlocks:"20",punishBlocks:"30",
            expiresAt:"1893499200",swapId:"BCH_TRADE"}}};
        dexEngine.bchAdmissionStatus={configured:true,state:"reviewed",submitAvailable:true,checkAvailable:true,review:quote};
        tryCompare(consent,"visible",true);
        var terms=findChild(page,"bchAdmissionTerms");
        verify(terms.text.indexOf("184467440737.09551615 BCH")>=0);
        verify(terms.text.indexOf("XMZ_REFUND_ADDRESS")>=0);
        verify(terms.text.indexOf("You provide XMZ and receive BCH")>=0);
        verify(terms.text.indexOf("undefined")<0);
        compare(page.exactAtomicText("18446744073709551615",11),"184467440.73709551615");
        compare(page.exactAtomicText("1",8),"0.00000001");compare(page.exactAtomicText("0",8),"0");
        verify(!submit.enabled);consent.checked=true;tryCompare(submit,"enabled",true);
        submit.clicked();compare(dexEngine.bchSubmitted,"bch-approved");verify(!consent.checked);
        consent.checked=true;page.selectPair("LTC / XMZ");verify(!consent.checked);tryCompare(panel,"visible",false);
        page.selectPair("BCH / XMZ");consent.checked=true;
        dexEngine.bchAdmissionStatus={configured:true,state:"unknown",checkAvailable:true,submitAttempted:true};
        tryCompare(consent,"checked",false);verify(!submit.enabled);
        verify(outcome.text.indexOf("unknown")>=0);check.clicked();compare(dexEngine.bchChecks,1);
        dexEngine.bchAdmissionStatus={configured:true,state:"admitted",checkAvailable:true};
        verify(outcome.text.indexOf("No funding or chain confirmation")>=0);verify(!submit.enabled);
        dexEngine.bchAdmissionStatus={configured:false};page.selectPair("BTC / XMZ");
    }
    function test_litecoinPrivateNegotiationStatus() {
        page.selectPair("LTC / XMZ");
        var label = findChild(page, "litecoinNegotiationStatus");
        verify(label !== null);
        dexEngine.litecoinNegotiationStatus = {configured:true, attached:false, state:"configured", executable:false};
        tryCompare(label, "visible", true);
        verify(label.text.indexOf("after the reviewed trade is saved") >= 0);
        dexEngine.litecoinNegotiationStatus = {configured:true, attached:false, state:"waiting-for-connection", executable:false};
        tryVerify(function() { return label.text.indexOf("connect privately") >= 0; });
        dexEngine.litecoinNegotiationStatus = {attached:true, state:"waiting-for-peer", executable:false};
        tryCompare(label, "visible", true);
        verify(label.text.indexOf("signed key message") >= 0);
        dexEngine.litecoinNegotiationStatus = {attached:true, state:"complete", executable:false};
        tryVerify(function() { return label.text.indexOf("separate review and approval") >= 0; });
        dexEngine.litecoinNegotiationStatus = {attached:true, state:"unknown", executable:false};
        tryVerify(function() { return label.text.indexOf("saved trade") >= 0; });
        page.selectPair("DOGE / XMZ");
        tryCompare(label, "visible", false);
        dexEngine.litecoinNegotiationStatus = {attached:false, state:"unattached", executable:false};
    }
    function test_litecoinRedemptionTransactionConsentAndConfirmation() {
        page.selectPair("LTC / XMZ");
        dexEngine.litecoinRedemptionStatus={configured:true,reviewAvailable:true,checkAvailable:true};
        findChild(page,"litecoinTransactionReview").clicked();compare(dexEngine.redemptionReviews,1);
        var consent=findChild(page,"litecoinTransactionConsent"),submit=findChild(page,"litecoinTransactionSubmit");
        dexEngine.litecoinRedemptionStatus={configured:true,submissionAvailable:true,review:{commitment:"first"}};
        verify(!consent.checked);verify(!submit.enabled);consent.checked=true;
        dexEngine.litecoinRedemptionStatus={configured:true,submissionAvailable:true,review:{commitment:"retry"}};
        verify(!consent.checked);verify(!submit.enabled);consent.checked=true;submit.clicked();
        compare(dexEngine.submittedRedemption,"retry");verify(!consent.checked);
        dexEngine.litecoinRedemptionStatus={configured:true,state:"mempool",checkAvailable:true};
        verify(findChild(page,"litecoinTransactionStatus").text.indexOf("not yet confirmed")>=0);
        findChild(page,"litecoinTransactionCheck").clicked();compare(dexEngine.redemptionChecks,1);
        var label=findChild(page,"litecoinTransactionConfirmation");
        dexEngine.litecoinRedemptionStatus={configured:true,checkState:"confirmed",observation:{txid:"saved-tx",block:"saved-block",tip:"checked-tip",confirmations:"3",requiredConfirmations:"2"}};
        verify(label.text.indexOf("saved-tx")>=0);verify(label.text.indexOf("does not confirm the XMZ payout")>=0);
        dexEngine.litecoinRedemptionStatus={configured:true,checkState:"checking",checkAvailable:false};
        verify(label.text.indexOf("saved-tx")<0);verify(!findChild(page,"litecoinTransactionCheck").enabled);
        dexEngine.litecoinRedemptionStatus={configured:true,checkState:"unknown"};
        verify(label.text.indexOf("No current verified")>=0);verify(label.text.indexOf("saved-block")<0);
        dexEngine.litecoinRedemptionStatus={configured:false};
    }
    function test_litecoinSharedSetupKeepsPhaseApprovalsSeparate() {
        page.selectPair("LTC / XMZ");
        dexEngine.litecoinAppStatus={configured:true,state:"configured",busy:false,fundingConfigured:true,paymentConfigured:true};
        findChild(page,"litecoinFundingPhaseCheck").clicked();compare(dexEngine.requestedPhase,"funding");
        findChild(page,"litecoinPaymentPhaseCheck").clicked();compare(dexEngine.requestedPhase,"payment");
        dexEngine.litecoinFundingStatus={attached:true,valid:false,busy:false,fundingSubmissionConfigured:true,fundingSubmissionAvailable:true,fundingReview:{commitment:"funding-phase"}};
        dexEngine.litecoinPaymentStatus={attached:true,valid:false,busy:false,refundSubmissionConfigured:true,refundSubmissionAvailable:true,refundReview:{commitment:"refund-phase"}};
        var fund=findChild(page,"litecoinFundingConsent"),refund=findChild(page,"litecoinRefundConsent");
        verify(!fund.checked);verify(!refund.checked);fund.checked=true;refund.checked=true;
        findChild(page,"litecoinFundingSubmit").clicked();compare(dexEngine.submittedFunding,"funding-phase");
        findChild(page,"litecoinRefundSubmit").clicked();compare(dexEngine.submittedRefund,"refund-phase");
        dexEngine.litecoinAppStatus={configured:true,state:"configured",busy:true,fundingConfigured:true,paymentConfigured:true};
        verify(!findChild(page,"litecoinFundingPhaseCheck").enabled);verify(!findChild(page,"litecoinPaymentPhaseCheck").enabled);
        dexEngine.litecoinAppStatus={configured:true,state:"unknown",busy:false,fundingConfigured:true,paymentConfigured:true};
        verify(!findChild(page,"litecoinFundingPhaseCheck").enabled);
        verify(findChild(page,"litecoinAppStatus").text.indexOf("same saved trade")>=0);
        dexEngine.litecoinFundingStatus=Qt.binding(function(){return dexEngine.litecoinJournalStatus;});
        dexEngine.litecoinPaymentStatus=Qt.binding(function(){return dexEngine.litecoinJournalStatus;});
        dexEngine.litecoinAppStatus={configured:false};
    }
    function test_litecoinRedemptionExchangeConsent() {
        page.selectPair("LTC / XMZ");
        dexEngine.litecoinNegotiationStatus={settlementConfigured:true,role:"native-funder",canShareFunding:true};
        var share=findChild(page,"litecoinShareFundingProof"),release=findChild(page,"litecoinReleaseRedemption");
        var consent=findChild(page,"litecoinRedemptionConsent"),label=findChild(page,"litecoinRedemptionSessionStatus");
        verify(share.enabled);share.clicked();compare(dexEngine.redemptionApproval,"");
        dexEngine.litecoinNegotiationStatus={settlementConfigured:true,role:"litecoin-funder",canReleaseRedemption:true,nativeFundingTxid:"first"};
        verify(!consent.checked);verify(!release.enabled);consent.checked=true;
        dexEngine.litecoinNegotiationStatus={settlementConfigured:true,role:"litecoin-funder",canReleaseRedemption:true,nativeFundingTxid:"second"};
        verify(!consent.checked);consent.checked=true;release.clicked();compare(dexEngine.redemptionApproval,"second");verify(!consent.checked);
        dexEngine.litecoinNegotiationStatus={settlementConfigured:true,settlementState:"complete"};
        verify(label.text.indexOf("No transaction was broadcast")>=0);verify(label.text.indexOf("separate approval")>=0);
        dexEngine.litecoinNegotiationStatus={settlementConfigured:true,settlementState:"unknown"};
        verify(label.text.indexOf("same saved trade")>=0);
        dexEngine.litecoinNegotiationStatus={attached:false,state:"unattached",executable:false};
    }
    function test_litecoinRecoveryConfirmation() {
        page.selectPair("LTC / XMZ");
        dexEngine.litecoinReservationStatus={configured:true,recoveryCheckAvailable:true};
        var cancel=findChild(page,"litecoinCancelCheck"),refund=findChild(page,"litecoinRefundSpendCheck");
        cancel.clicked();compare(dexEngine.recoveryCheckKind,"cancel");
        refund.clicked();compare(dexEngine.recoveryCheckKind,"refund");
        var label=findChild(page,"litecoinRecoveryConfirmation");
        dexEngine.litecoinReservationStatus={configured:true,recoveryCheckState:"confirmed",recoveryObservation:{operation:"cancel",confirmations:"3",requiredConfirmations:"2",txid:"saved-cancel",block:"saved-block",tip:"checked-tip"}};
        verify(label.text.indexOf("not a wallet refund")>=0);verify(label.text.indexOf("saved-cancel")>=0);
        verify(label.text.indexOf("does not confirm the XMZ payment")>=0);
        dexEngine.litecoinReservationStatus={configured:true,recoveryCheckState:"checking",recoveryCheckAvailable:false};
        verify(!cancel.enabled);verify(!refund.enabled);verify(label.text.indexOf("saved-cancel")<0);
        dexEngine.litecoinReservationStatus={configured:true,recoveryCheckState:"unknown",recoveryCheckAvailable:true};
        verify(label.text.indexOf("No current verified")>=0);verify(label.text.indexOf("saved-block")<0);
        dexEngine.litecoinReservationStatus={configured:false};
    }
    function test_bchLightFundingSeparatesRecoveryAndSendConsent() {
        page.selectPair("BCH / XMZ");var previous=dexEngine.bchLightFundingStatus;
        try {
            dexEngine.bchLightFundingStatus={configured:true,contextReady:true,valid:true,busy:false,prepareAvailable:true,
                terms:{contractAddress:"bchreg:contract",refundAddress:"bchreg:refund",amountAtomic:"100000000",maximumFeeAtomic:"100000",lockConfirmations:2}};
            findChild(page,"bchLightDexSelectSource").clicked();compare(dexEngine.bchLightAction.operation,"select");
            var fee=findChild(page,"bchLightDexFee"),prepare=findChild(page,"bchLightDexPrepare");
            fee.text="0.00010000";verify(prepare.enabled);prepare.clicked();
            compare(dexEngine.bchLightAction.fee,"10000");compare(dexEngine.bchLightAction.confirmations,2);
            var payment={address:"bchreg:contract",amountAtomic:"100000000",feeAtomic:"10000"};
            dexEngine.bchLightFundingStatus={configured:true,contextReady:true,valid:true,busy:false,approvalAvailable:true,retainedPayment:payment,
                reviewOperation:"review",review:{commitment:"sign",details:{unsignedTxid:"unsigned"}}};
            var password=findChild(page,"bchLightDexPassword"),consent=findChild(page,"bchLightDexApproval"),approve=findChild(page,"bchLightDexApprove");
            verify(!approve.enabled);password.text="fixture";consent.checked=true;verify(approve.enabled);approve.clicked();
            compare(dexEngine.bchLightAction.operation,"sign");verify(dexEngine.bchLightAction.passwordProvided);compare(password.text,"");verify(!consent.checked);
            var operations=["recovery-review","send-review","retry-review"],expected=["recovery-retain","send","retry"];
            for(var i=0;i<operations.length;i++) {
                dexEngine.bchLightFundingStatus={configured:true,contextReady:true,valid:true,busy:false,approvalAvailable:true,
                    retainedPayment:payment,retainedTxid:"signed",reviewOperation:operations[i],review:{commitment:operations[i],details:{txid:"signed"}}};
                verify(!consent.checked);verify(!approve.enabled);consent.checked=true;verify(approve.enabled);approve.clicked();
                compare(dexEngine.bchLightAction.operation,expected[i]);compare(dexEngine.bchLightAction.commitment,operations[i]);
            }
            consent.checked=true;password.text="must-clear";
            dexEngine.bchLightFundingStatus={configured:true,contextReady:false,valid:true,busy:false,approvalAvailable:false,
                retainedPayment:payment,reviewOperation:"retry-review",review:{commitment:"retry-review",details:{txid:"signed"}}};
            verify(!approve.enabled);verify(!consent.checked);compare(password.text,"");verify(!findChild(page,"bchLightDexObserve").enabled);
            dexEngine.bchLightFundingStatus={configured:true,contextReady:true,valid:true,busy:false,approvalAvailable:false,
                retainedPayment:payment,reviewOperation:"retry-review",review:{commitment:"retry-review",details:{txid:"signed"}}};
            consent.checked=true;verify(!approve.enabled); // Expired/stale approval cannot be used.
            dexEngine.bchLightFundingStatus={configured:true,contextReady:true,valid:true,busy:false,state:"outcome-unconfirmed",retainedPayment:payment,retainedTxid:"signed"};
            verify(findChild(page,"bchLightDexOutcome").text.indexOf("could not be verified")>=0);
            findChild(page,"bchLightDexReconcile").clicked();compare(dexEngine.bchLightAction.operation,"ack");
        } finally {dexEngine.bchLightFundingStatus=previous;}
    }
    function test_lightDexFundingControlsSeparateApprovals() {
        page.selectPair("LTC / XMZ");
        var previous=dexEngine.litecoinLightFundingStatus, previousCore=dexEngine.litecoinReservationStatus;
        try {
            dexEngine.litecoinReservationStatus={configured:true,role:"litecoin-funder",canPrepare:true};
            dexEngine.litecoinLightFundingStatus={configured:true,valid:true,busy:false,prepareAvailable:true,peerReady:true,
                terms:{contractAddress:"rltc-contract",refundAddress:"rltc-refund",amountAtomic:"100000000",maximumFeeAtomic:"100000",lockConfirmations:2}};
            verify(!findChild(page,"litecoinReservationPrepare").visible);
            var fee=findChild(page,"lightDexFee"),prepare=findChild(page,"lightDexPrepare");
            fee.text="0.00010000";verify(prepare.enabled);prepare.clicked();
            compare(dexEngine.lightDexAction.operation,"prepare");compare(dexEngine.lightDexAction.fee,"10000");
            var payment={address:"rltc-contract",amountAtomic:"100000000",feeAtomic:"10000"};
            dexEngine.litecoinLightFundingStatus={configured:true,valid:true,busy:false,retainedPayment:payment,
                reviewOperation:"review",review:{commitment:"sign-only",details:{txid:"saved-tx"}}};
            var password=findChild(page,"lightDexPassword"),consent=findChild(page,"lightDexApproval"),approve=findChild(page,"lightDexApprove");
            verify(!approve.enabled);password.text="fixture-password";consent.checked=true;verify(approve.enabled);approve.clicked();
            compare(dexEngine.lightDexAction.operation,"sign");verify(dexEngine.lightDexAction.passwordProvided);
            compare(password.text,"");verify(!consent.checked);
            password.text="clear-on-new-review";consent.checked=true;
            dexEngine.litecoinLightFundingStatus={configured:true,valid:true,busy:false,retainedPayment:payment,
                reviewOperation:"send-review",review:{commitment:"send-only",details:{txid:"saved-tx",endpoint:{host:"localhost",port:12345}}}};
            compare(password.text,"");verify(!consent.checked);verify(!approve.enabled);
            consent.checked=true;approve.clicked();compare(dexEngine.lightDexAction.operation,"send");compare(dexEngine.lightDexAction.commitment,"send-only");
            dexEngine.litecoinLightFundingStatus={configured:true,valid:true,busy:false,retainedPayment:payment,
                reviewOperation:"retry-review",review:{commitment:"retry-only",details:{txid:"saved-tx"}}};
            verify(!consent.checked);consent.checked=true;approve.clicked();compare(dexEngine.lightDexAction.operation,"retry");
            dexEngine.litecoinLightFundingStatus={configured:true,valid:true,busy:true,retainedPayment:payment};
            verify(!findChild(page,"lightDexObserve").enabled);
            dexEngine.litecoinLightFundingStatus={configured:true,valid:true,busy:false,retainedPayment:payment,
                result:{txid:"saved-tx",observation:{position:"included",confirmations:3}}};
            verify(findChild(page,"lightDexOutcome").text.indexOf("Funding confirmed: 3")>=0);
        } finally {dexEngine.litecoinLightFundingStatus=previous;dexEngine.litecoinReservationStatus=previousCore;}
    }
    function test_lightDexLostReplyRequiresObservation() {
        page.selectPair("LTC / XMZ");var previous=dexEngine.litecoinLightFundingStatus;
        try {
            dexEngine.litecoinLightFundingStatus={configured:true,valid:true,busy:false,state:"outcome-unconfirmed",
                retainedPayment:{address:"saved-contract"},result:{}};
            verify(findChild(page,"lightDexOutcome").text.indexOf("could not be verified")>=0);
            verify(findChild(page,"lightDexObserve").enabled);
            verify(!findChild(page,"lightDexApprove").enabled);
            dexEngine.litecoinLightFundingStatus={configured:true,valid:true,busy:false,state:"complete",
                retainedPayment:{address:"saved-contract"},result:{broadcastStatus:"outcome-unknown",observation:{position:"reported-pending"}}};
            verify(findChild(page,"lightDexOutcome").text.indexOf("awaiting confirmation")>=0);
            verify(!findChild(page,"lightDexApprove").enabled);
        } finally {dexEngine.litecoinLightFundingStatus=previous;}
    }
    function test_lightDexRecoveryResumesWithoutFreshTerms() {
        page.selectPair("LTC / XMZ");
        var previous=dexEngine.litecoinLightFundingStatus;
        try {
            dexEngine.litecoinLightFundingStatus={configured:true,valid:true,busy:false,peerReady:true,
                recoveryComplete:false,retainedPayment:{address:"saved-contract",amountAtomic:"100000000",feeAtomic:"1000"}};
            var resume=findChild(page,"lightDexResumeRecovery");
            verify(resume.visible && resume.enabled);verify(!findChild(page,"lightDexReopen").enabled);
            resume.clicked();compare(dexEngine.lightDexAction.operation,"resume-recovery");
            dexEngine.litecoinLightFundingStatus={configured:true,valid:true,busy:true,peerReady:true,
                recoveryComplete:false,retainedPayment:{address:"saved-contract"}};
            verify(!resume.enabled);
            dexEngine.litecoinLightFundingStatus={configured:true,valid:true,busy:false,peerReady:true,
                recoveryComplete:true,retainedPayment:{address:"saved-contract"}};
            verify(!resume.visible);
        } finally {dexEngine.litecoinLightFundingStatus=previous;}
    }
    function test_litecoinNativeRecoveryControls() {
        page.selectPair("LTC / XMZ");
        var original = dexEngine.litecoinReservationStatus;
        var punish = findChild(page, "litecoinPunishReview");
        var check = findChild(page, "litecoinPunishCheck");
        var refund = findChild(page, "litecoinRefundSpendReview");
        var funding = findChild(page, "litecoinLockFundingReviewCheck");
        dexEngine.litecoinReservationStatus = {configured:true,role:"native-funder",
            recoveryReviewAvailable:true,recoveryCheckAvailable:true};
        try {
            verify(punish.visible && punish.enabled && check.visible && check.enabled);
            verify(!refund.visible && !funding.visible);
            verify(!findChild(page, "litecoinReservationPrepare").visible);
            punish.clicked(); compare(dexEngine.recoveryKind, "punish");
            check.clicked(); compare(dexEngine.recoveryCheckKind, "punish");
            dexEngine.litecoinReservationStatus = {configured:true,role:"native-funder",recoverySubmissionAvailable:true,
                recoveryReview:{commitment:"punish-review",details:{operation:"punish",action:"send",destination:"signed-payout",txid:"saved"},
                    amounts:{amountAtoms:"0.97",feeAtoms:"0.02"}}};
            var terms = findChild(page, "litecoinRecoverySpendTerms");
            var consent = findChild(page, "litecoinRecoverySpendConsent");
            var submit = findChild(page, "litecoinRecoverySpendSubmit");
            verify(terms.text.indexOf("Punishment") >= 0 && terms.text.indexOf("signed-payout") >= 0);
            verify(!submit.enabled);
            consent.checked = true; verify(submit.enabled);
            submit.clicked(); compare(dexEngine.recoveryToken, "punish-review");
            verify(!consent.checked);
            dexEngine.litecoinReservationStatus = {configured:true,role:"litecoin-funder",recoveryReviewAvailable:true};
            verify(!punish.visible && !check.visible && refund.visible);
        } finally { dexEngine.litecoinReservationStatus = original; }
    }
    function test_litecoinRecoverySpendConsent() {
        page.selectPair("LTC / XMZ");
        var consent=findChild(page,"litecoinRecoverySpendConsent"),submit=findChild(page,"litecoinRecoverySpendSubmit");
        dexEngine.litecoinReservationStatus={configured:true,recoveryReviewAvailable:true};
        findChild(page,"litecoinCancelReview").clicked();compare(dexEngine.recoveryKind,"cancel");
        findChild(page,"litecoinRefundSpendReview").clicked();compare(dexEngine.recoveryKind,"refund");
        dexEngine.litecoinReservationStatus={configured:true,recoverySubmissionAvailable:true,recoveryReview:{commitment:"cancel"}};
        verify(!consent.checked);verify(!submit.enabled);consent.checked=true;
        dexEngine.litecoinReservationStatus={configured:true,recoverySubmissionAvailable:true,recoveryReview:{commitment:"refund"}};
        tryCompare(consent,"checked",false);verify(!submit.enabled);consent.checked=true;submit.clicked();
        compare(dexEngine.recoveryToken,"refund");verify(!consent.checked);
        dexEngine.litecoinReservationStatus={configured:true,recoveryState:"mempool"};
        verify(findChild(page,"litecoinRecoverySpendOutcome").text.indexOf("No retry was sent")>=0);
        dexEngine.litecoinReservationStatus={configured:false};
    }
    function test_litecoinLockFundingConsent() {
        page.selectPair("LTC / XMZ");
        var button=findChild(page,"litecoinLockFundingSubmit"),consent=findChild(page,"litecoinLockFundingConsent");
        dexEngine.litecoinReservationStatus={configured:true,fundingSubmissionAvailable:true,fundingReview:{commitment:"first"}};
        verify(!consent.checked); verify(!button.enabled);
        consent.checked=true; tryCompare(button,"enabled",true);
        dexEngine.litecoinReservationStatus={configured:true,fundingSubmissionAvailable:true,fundingReview:{commitment:"second"}};
        tryCompare(consent,"checked",false);verify(!button.enabled);
        consent.checked=true;button.clicked();compare(dexEngine.submittedLock,"second");verify(!consent.checked);
        dexEngine.litecoinReservationStatus={configured:true,fundingState:"submitted"};
        verify(findChild(page,"litecoinLockFundingOutcome").text.indexOf("Confirmation is still required") >= 0);
        dexEngine.litecoinReservationStatus={configured:false};
    }
    function test_litecoinRecoveredWalletControls() {
        page.selectPair("LTC / XMZ");
        var restore=findChild(page,"litecoinWalletRestore"),open=findChild(page,"litecoinWalletOpen"),check=findChild(page,"litecoinWalletCheck");
        dexEngine.litecoinJournalStatus={walletRecoveryConfigured:true,walletRestoreAvailable:true,walletOpenAvailable:true,walletCheckAvailable:true};
        tryCompare(restore,"enabled",true);restore.clicked();compare(dexEngine.walletRestores,1);
        open.clicked();compare(dexEngine.walletOpens,1);check.clicked();compare(dexEngine.walletChecks,1);
        dexEngine.litecoinJournalStatus={walletRecoveryConfigured:true,walletRecoveryState:"unknown",walletRestoreAvailable:false};
        tryCompare(restore,"enabled",false);verify(findChild(page,"litecoinWalletRecoveryStatus").text.indexOf("Do not create a replacement")>=0);
        dexEngine.litecoinJournalStatus={walletRecoveryConfigured:true,walletRecoveryState:"verified",recoveredWallet:{address:"SAVED_WALLET"}};
        verify(findChild(page,"litecoinWalletRecoveryStatus").text.indexOf("No payment was prepared or sent")>=0);
        dexEngine.litecoinJournalStatus={attached:false,busy:false,valid:false};
    }
    function test_litecoinPaymentPreparation() {
        page.selectPair("LTC / XMZ");
        var payout=findChild(page,"litecoinPayoutPrepare"),refund=findChild(page,"litecoinRefundPrepare");
        dexEngine.litecoinJournalStatus={paymentPreparationConfigured:true,payoutPreparationAvailable:true,refundPreparationAvailable:false};
        tryCompare(payout,"enabled",true);verify(!refund.enabled);payout.clicked();compare(dexEngine.payoutPreparations,1);
        dexEngine.litecoinJournalStatus={paymentPreparationConfigured:true,payoutPreparationAvailable:false,refundPreparationAvailable:true};
        tryCompare(refund,"enabled",true);verify(!payout.enabled);refund.clicked();compare(dexEngine.refundPreparations,1);
        dexEngine.litecoinJournalStatus={paymentPreparationConfigured:true,paymentPreparationState:"prepared"};
        verify(findChild(page,"litecoinPaymentPreparationStatus").text.indexOf("without broadcasting")>=0);
        dexEngine.litecoinJournalStatus={attached:false,busy:false,valid:false};
    }
    function test_litecoinReservationControl() {
        page.selectPair("LTC / XMZ");
        var button=findChild(page,"litecoinReservationPrepare"),label=findChild(page,"litecoinReservationStatus");
        dexEngine.litecoinReservationStatus={configured:true,canPrepare:false,state:"ready"};
        tryCompare(button,"visible",true);verify(!button.enabled);
        dexEngine.litecoinReservationStatus={configured:true,canPrepare:true,state:"ready"};
        tryCompare(button,"enabled",true);button.clicked();compare(dexEngine.reservationRequests,1);
        dexEngine.litecoinReservationStatus={configured:true,canPrepare:false,state:"reserved",review:{amountAtomsDisplay:"1",feeAtomsDisplay:"0.000001",maximumFeeAtomsDisplay:"0.001",wallet:"fixture",txid:"saved-lock"}};
        verify(label.text.indexOf("without signing or broadcasting funding") >= 0);verify(!button.enabled);
        dexEngine.litecoinReservationStatus={configured:true,canPrepare:false,state:"unknown"};
        verify(label.text.indexOf("same reservation") >= 0);
        dexEngine.litecoinReservationStatus={configured:false};
    }
    function test_litecoinPrivateRecoveryStatus() {
        page.selectPair("LTC / XMZ");
        var label=findChild(page,"litecoinNegotiationStatus");
        dexEngine.litecoinNegotiationStatus={attached:true,state:"complete",recoveryStarted:true,recoveryState:"waiting-for-peer",executable:false};
        tryCompare(label,"visible",true);
        verify(label.text.indexOf("Recovery message saved and sent") >= 0);
        dexEngine.litecoinNegotiationStatus={attached:true,state:"complete",recoveryStarted:true,recoveryState:"complete",executable:false};
        verify(label.text.indexOf("Funding still requires separate review and approval") >= 0);
        dexEngine.litecoinNegotiationStatus={attached:true,state:"unknown",recoveryStarted:true,recoveryState:"unknown",executable:false};
        verify(label.text.indexOf("do not fund a replacement lock") >= 0);
        dexEngine.litecoinNegotiationStatus={attached:false,state:"unattached",executable:false};
    }
    function test_tradeFoldersListAndExplicitClose() {
        page.selectPair("LTC / XMZ");
        var panel = findChild(page, "tradeFolderPanel");
        var consent = findChild(page, "closeTradeConsent");
        var close = findChild(page, "closeTradeButton");
        verify(panel && consent && close);
        verify(!panel.visible);
        dexEngine.tradeFolders = [
            {tradeId:"11111111-1111-4111-8111-111111111111",pair:"LTC/XMZ",network:"ltc-testnet",state:"active",loaded:true},
            {tradeId:"22222222-2222-4222-8222-222222222222",pair:"LTC/XMZ",network:"ltc-testnet",state:"load-failed",loaded:false},
            {tradeId:"33333333-3333-4333-8333-333333333333",pair:"",network:"",state:"invalid",loaded:false}];
        dexEngine.loadedTradeId = "11111111-1111-4111-8111-111111111111";
        tryCompare(panel, "visible", true);
        verify(findChild(page, "tradeFolder_11111111-1111-4111-8111-111111111111").text.indexOf("open now") >= 0);
        verify(findChild(page, "tradeFolder_22222222-2222-4222-8222-222222222222").text.indexOf("recovery runbook") >= 0);
        verify(findChild(page, "tradeFolder_33333333-3333-4333-8333-333333333333").text.indexOf("unreadable") >= 0);
        verify(close.visible && !close.enabled);
        var before = dexEngine.closeRequests;
        consent.checked = true;
        tryCompare(close, "enabled", true);
        close.clicked();
        compare(dexEngine.closeRequests, before + 1);
        verify(!consent.checked);
        dexEngine.loadedTradeId = "";
        dexEngine.tradeFolders = [];
        tryCompare(panel, "visible", false);
    }
    function test_litecoinStartBlockedByOperatorLimits() {
        page.selectPair("LTC / XMZ");
        var review = findChild(page, "litecoinStartReviewCheck");
        var submit = findChild(page, "litecoinStartSubmit");
        var consent = findChild(page, "litecoinStartConsent");
        var refusal = findChild(page, "litecoinStartRefusal");
        verify(review && submit && consent && refusal);
        dexEngine.litecoinJournalStatus = {startConfigured:true,startReviewAvailable:true,startSubmissionAvailable:true,
            startState:"reviewed",startReview:startupReview("limited")};
        tryCompare(review, "enabled", true);
        verify(!refusal.visible);
        dexEngine.litecoinStartRefusal = "New trades are paused by the operator.";
        tryCompare(refusal, "visible", true);
        verify(refusal.text.indexOf("paused by the operator") >= 0);
        verify(refusal.text.indexOf("already started can still") >= 0);
        verify(!review.enabled);
        consent.checked = true;
        verify(!submit.enabled);
        dexEngine.litecoinStartRefusal = "";
        tryCompare(submit, "enabled", true);
        consent.checked = false;
        dexEngine.litecoinJournalStatus = {startConfigured:true,startReviewAvailable:false,startSubmissionAvailable:false,startState:"unknown"};
    }
    function test_litecoinStartupConsent() {
        page.selectPair("LTC / XMZ");
        var panel = findChild(page, "litecoinStartPanel");
        var review = findChild(page, "litecoinStartReviewCheck");
        var submit = findChild(page, "litecoinStartSubmit");
        var consent = findChild(page, "litecoinStartConsent");
        var outcome = findChild(page, "litecoinStartOutcome");
        verify(panel && review && submit && consent && outcome);
        dexEngine.litecoinJournalStatus = {startConfigured:true,startReviewAvailable:true,startSubmissionAvailable:false,startState:"ready"};
        tryCompare(panel, "visible", true);
        verify(!submit.enabled);
        var previous = dexEngine.startReviews;
        review.clicked();
        compare(dexEngine.startReviews, previous+1);
        dexEngine.litecoinJournalStatus = {startConfigured:true,startReviewAvailable:true,startSubmissionAvailable:true,
            startState:"reviewed",startReview:startupReview("start-one")};
        tryCompare(consent, "visible", true);
        var terms = findChild(page, "litecoinStartTerms");
        verify(terms.text.indexOf("184467440737.09551615 LTC") >= 0);
        verify(terms.text.indexOf("XMZ_TEST_REFUND_ADDRESS") >= 0);
        verify(terms.text.indexOf("2030-01-01T12:00:00Z") >= 0);
        verify(terms.text.indexOf("Pay XMZ, receive LTC") >= 0);
        verify(terms.text.indexOf("undefined") < 0);
        verify(!consent.checked && !submit.enabled);
        consent.checked = true;
        tryCompare(submit, "enabled", true);
        submit.clicked();
        compare(dexEngine.submittedStart, "start-one");
        verify(!consent.checked);
        consent.checked = true;
        dexEngine.litecoinJournalStatus = {startConfigured:true,startReviewAvailable:true,startSubmissionAvailable:true,
            startState:"reviewed",startReview:startupReview("start-two")};
        tryCompare(consent, "checked", false);
        verify(!submit.enabled);
        dexEngine.litecoinJournalStatus = {startConfigured:true,startReviewAvailable:false,startSubmissionAvailable:false,startState:"unknown"};
        tryVerify(function() { return outcome.text.indexOf("unknown") >= 0; });
        verify(!submit.enabled && !review.enabled);
        dexEngine.litecoinJournalStatus = {startConfigured:true,startReviewAvailable:false,startSubmissionAvailable:false,startState:"saved"};
        tryVerify(function() { return outcome.text.indexOf("Funding requires separate approval") >= 0; });
        page.selectPair("DOGE / XMZ");
        tryCompare(panel, "visible", false);
        dexEngine.litecoinJournalStatus = {attached:false,busy:false,valid:false};
        page.selectPair("BTC / XMZ");
    }
    function test_litecoinMarketIsolation() {
        page.selectPair("LTC / XMZ");
        var original = dexEngine.litecoinJournalStatus;
        var submitted = dexEngine.submittedFunding;
        var requests = dexEngine.journalRequests;
        var panel = findChild(page, "litecoinTradePanel");
        var submit = findChild(page, "litecoinFundingSubmit");
        var check = findChild(page, "litecoinJournalCheck");
        var names = ["litecoinStartConsent", "litecoinLockFundingConsent",
            "litecoinRecoverySpendConsent", "litecoinRedemptionConsent",
            "litecoinTransactionConsent", "litecoinFundingConsent",
            "litecoinRefundConsent", "litecoinPayoutConsent"];
        dexEngine.litecoinJournalStatus = {attached:true,busy:false,valid:true,
            fundingSubmissionConfigured:true,fundingSubmissionAvailable:true,
            snapshot:{tradeId:"market-isolation",role:"native-funder",nativeFunding:{prepared:true},nativeRefundPayout:{}},
            fundingReview:{commitment:"market-bound-review",details:{action:"send"},amounts:{}}};
        try {
            var pairs = dexEngine.supportedPairs();
            for (var i = 0; i < pairs.length; ++i) {
                if (pairs[i] === "LTC / XMZ") continue;
                page.selectPair("LTC / XMZ");
                verify(panel.visible && panel.enabled);
                for (var j = 0; j < names.length; ++j)
                    findChild(page, names[j]).checked = true;
                verify(submit.visible && submit.enabled);
                page.selectPair(pairs[i]);
                verify(!panel.visible && !panel.enabled);
                verify(!submit.visible && !submit.enabled);
                verify(!check.visible && !check.enabled);
                for (j = 0; j < names.length; ++j)
                    compare(findChild(page, names[j]).checked, false);
                page.selectPair("LTC / XMZ");
                verify(submit.visible && !submit.enabled);
                compare(dexEngine.litecoinJournalStatus.fundingReview.commitment, "market-bound-review");
            }
            compare(dexEngine.submittedFunding, submitted);
            compare(dexEngine.journalRequests, requests);
        } finally {
            dexEngine.litecoinJournalStatus = original;
            page.selectPair("BTC / XMZ");
        }
    }
    function test_bitcoinQuoteMarketIsolation() {
        page.selectPair("BTC / XMZ");
        var original = dexEngine.nativeQuoteReview;
        var approved = dexEngine.approvedCommitment;
        var panel = findChild(page, "nativeQuoteReviewPanel");
        var consent = findChild(page, "nativeQuoteAcknowledgement");
        var approve = findChild(page, "nativeQuoteApprove");
        dexEngine.nativeQuoteReview = {commitment:"bitcoin-only-review"};
        try {
            var pairs = dexEngine.supportedPairs();
            for (var i = 1; i < pairs.length; ++i) {
                page.selectPair("BTC / XMZ");
                consent.checked = true;
                verify(panel.visible && approve.enabled);
                page.selectPair(pairs[i]);
                verify(!panel.visible && !approve.enabled);
                compare(consent.checked, false);
                page.selectPair("BTC / XMZ");
                verify(panel.visible && !approve.enabled);
            }
            compare(dexEngine.approvedCommitment, approved);
        } finally {
            dexEngine.nativeQuoteReview = original;
            page.selectPair("BTC / XMZ");
        }
    }
    function test_marketSelection() {
        var blocker = findChild(page, "dexSettlementBlocker");
        verify(blocker !== null);
        for (var i = 0; i < dexEngine.supportedPairs().length; ++i) {
            var pair = dexEngine.supportedPairs()[i];
            page.selectPair(pair);
            compare(page.pairName, pair);
            compare(page.marketDecimals(), dexEngine.marketInfo(pair).decimals);
            verify(blocker.text.indexOf(page.baseCoin + " settlement unavailable") >= 0);
            verify(!page.marketInfo.executable);
        }
        page.selectPair("BTC / XMZ");
    }
    function test_quoteReview() {
        page.selectPair("BTC / XMZ");
        var panel = findChild(page, "nativeQuoteReviewPanel")
        var acknowledgement = findChild(page, "nativeQuoteAcknowledgement")
        var approval = findChild(page, "nativeQuoteApprove")
        verify(panel !== null && acknowledgement !== null && approval !== null)
        verify(!panel.visible)
        dexEngine.nativeQuoteReview = {commitment:"quote-one", terms:{btcSatoshis:"500000", xmzAtoms:"200000000000"},
            amounts:{btcAmount:"0.005",xmzAmount:"2",btcPerXmz:"0.0025",btcFeeLimit:"0.0001",xmzFeeLimit:"0.01"},executable:false}
        tryCompare(panel, "visible", true)
        var summary = findChild(page, "nativeQuoteAmountSummary")
        verify(summary !== null)
        verify(summary.text.indexOf("0.005 BTC for 2 XMZ") >= 0)
        verify(summary.text.indexOf("0.0025 BTC per XMZ") >= 0)
        verify(summary.text.indexOf("not market valuation") >= 0)
        verify(!approval.enabled)
        acknowledgement.checked = true
        tryCompare(approval, "enabled", true)
        approval.clicked()
        compare(dexEngine.approvedCommitment, "quote-one")
        dexEngine.nativeQuoteReview = {commitment:"quote-two", terms:{btcSatoshis:"600000"}, executable:false}
        tryCompare(acknowledgement, "checked", false)
        tryCompare(summary, "text", "")
        verify(!approval.enabled)
        dexEngine.nativeQuoteReview = {}
        tryCompare(panel, "visible", false)
    }
    function test_quoteOutcome() {
        page.selectPair("BTC / XMZ");
        var outcome = findChild(page, "nativeQuoteOutcome")
        verify(outcome !== null)
        verify(!outcome.visible)
        dexEngine.nativeQuoteStatus = {state:"reserved",executable:false}
        tryVerify(function() { return outcome.text.indexOf("No trade has started") >= 0 })
        verify(outcome.visible)
        dexEngine.nativeQuoteStatus = {state:"failed",executable:false}
        tryVerify(function() { return outcome.text.indexOf("do not delete records") >= 0 })
        dexEngine.nativeQuoteStatus = {state:"cancelled",executable:false}
        tryVerify(function() { return outcome.text.indexOf("cancelled") >= 0 })
        dexEngine.nativeQuoteStatus = {state:"unattached",executable:false}
        tryCompare(outcome, "visible", false)
    }
    function test_litecoinSavedStateDoesNotClaimConfirmation() {
        page.selectPair("LTC / XMZ");
        var label = findChild(page, "litecoinJournalEvidence")
        var check = findChild(page, "litecoinJournalCheck")
        var terms = findChild(page, "litecoinJournalTerms")
        verify(label !== null && check !== null && terms !== null)
        verify(!label.visible)
        dexEngine.litecoinJournalStatus = {attached:true,busy:false,valid:true,
            amounts:{ltc:"184467440737.09551615",xmzLock:"184467440.73709551615"},snapshot:{
            tradeId:"saved-ltc-trade",role:"native-funder",ltcPayout:"ltc-destination",ltcRefund:"ltc-return",
            xmzPayout:"xmz-destination",xmzRefund:"xmz-return",
            nativeRefundPayout:{prepared:true,attempted:true,acknowledged:true}}}
        tryCompare(label, "visible", true)
        verify(label.text.indexOf("not current chain confirmations") >= 0)
        verify(terms.text.indexOf("184467440737.09551615 LTC") >= 0)
        verify(terms.text.indexOf("184467440.73709551615 XMZ") >= 0)
        verify(terms.text.indexOf("includes fee reserve") >= 0)
        verify(terms.text.indexOf("XMZ refund: xmz-return") >= 0)
        check.clicked()
        compare(dexEngine.journalRequests, 1)
        dexEngine.litecoinJournalStatus = {attached:true,busy:true,valid:false}
        tryCompare(check, "enabled", false)
        verify(label.text.indexOf("saved-ltc-trade") < 0)
        compare(terms.text, "")
        verify(!terms.visible)
        dexEngine.litecoinJournalStatus = {attached:false,busy:false,valid:false}
    }
    function test_litecoinRefundReviewIsInspectionOnly() {
        page.selectPair("LTC / XMZ");
        var inspect = findChild(page, "litecoinRefundReviewCheck")
        var review = findChild(page, "litecoinRefundReview")
        verify(inspect !== null && review !== null)
        dexEngine.litecoinJournalStatus = {attached:true,busy:false,valid:true,amounts:{ltc:"1",xmzLock:"2"},
            snapshot:{role:"native-funder",nativeRefundPayout:{prepared:true,attempted:true,acknowledged:true}},
            refundReview:{commitment:"bound-review",details:{action:"retransmit",destination:"signed-refund-address"},
                amounts:{refundAtoms:"1.9903552",feeAtoms:"0.0096448",maximumFeeAtoms:"0.1"}}}
        tryCompare(review, "visible", true)
        verify(review.text.indexOf("1.9903552 XMZ") >= 0)
        verify(review.text.indexOf("signed-refund-address") >= 0)
        verify(review.text.indexOf("retransmission of the same payment") >= 0)
        verify(review.text.indexOf("No payment was sent") >= 0)
        inspect.clicked()
        compare(dexEngine.refundReviewRetransmit, true)
        dexEngine.litecoinJournalStatus = {attached:true,busy:true,valid:false}
        tryCompare(review, "visible", false)
        compare(review.text, "")
        dexEngine.litecoinJournalStatus = {attached:false,busy:false,valid:false}
    }
    function test_litecoinFundingReviewShowsSourceDebitAndSafetyWindow() {
        page.selectPair("LTC / XMZ");
        var inspect = findChild(page, "litecoinFundingReviewCheck")
        var review = findChild(page, "litecoinFundingReview")
        verify(inspect !== null && review !== null)
        dexEngine.litecoinJournalStatus = {attached:true,busy:false,valid:true,amounts:{ltc:"1",xmzLock:"2"},
            snapshot:{role:"native-funder",nativeFunding:{prepared:true,attempted:true},nativeRefundPayout:{}},
            fundingReview:{commitment:"funding-review",expiresAtUtc:"2033-05-18T03:33:20Z",
                details:{action:"retransmit",wallet:"source-wallet",account:0,destination:"shared-lock",ltcContract:"litecoin-lock",safetyBlocks:"2"},
                amounts:{amountAtoms:"2",feeAtoms:"0.01",maximumFeeAtoms:"0.1",totalDebitAtoms:"2.01"}}}
        tryCompare(review, "visible", true)
        verify(review.text.indexOf("source-wallet") >= 0)
        verify(review.text.indexOf("shared-lock") >= 0)
        verify(review.text.indexOf("2.01 XMZ") >= 0)
        verify(review.text.indexOf("2 Litecoin blocks") >= 0)
        verify(review.text.indexOf("2033-05-18") >= 0)
        verify(review.text.indexOf("No payment was sent") >= 0)
        inspect.clicked()
        compare(dexEngine.fundingReviewRetransmit, true)
        dexEngine.litecoinJournalStatus = {attached:true,busy:true,valid:false}
        tryCompare(review, "visible", false)
        compare(review.text, "")
        dexEngine.litecoinJournalStatus = {attached:false,busy:false,valid:false}
    }
    function test_litecoinPayoutReviewShowsPrincipalAndReserve() {
        page.selectPair("LTC / XMZ");
        var inspect = findChild(page, "litecoinPayoutReviewCheck")
        var review = findChild(page, "litecoinPayoutReview")
        verify(inspect !== null && review !== null)
        dexEngine.litecoinJournalStatus = {attached:true,busy:false,valid:true,amounts:{ltc:"1",xmzLock:"2"},
            snapshot:{role:"litecoin-funder",nativePayout:{prepared:true,attempted:true},nativeRefundPayout:{}},
            payoutReview:{commitment:"payout-bound-review",details:{action:"retransmit",destination:"principal-address",reserveDestination:"reserve-address"},
                amounts:{payoutAtoms:"1",reserveReturnAtoms:"0.99",feeAtoms:"0.01",maximumFeeAtoms:"0.1"}}}
        tryCompare(review, "visible", true)
        verify(review.text.indexOf("1 XMZ") >= 0)
        verify(review.text.indexOf("0.99 XMZ") >= 0)
        verify(review.text.indexOf("principal-address") >= 0)
        verify(review.text.indexOf("reserve-address") >= 0)
        verify(review.text.indexOf("No payment was sent") >= 0)
        verify(inspect.visible)
        inspect.clicked()
        compare(dexEngine.payoutReviewRetransmit, true)
        dexEngine.litecoinJournalStatus = {attached:true,busy:true,valid:false}
        tryCompare(review, "visible", false)
        compare(review.text, "")
        dexEngine.litecoinJournalStatus = {attached:false,busy:false,valid:false}
    }
    function test_litecoinRefundSubmissionNeedsConsent() {
        page.selectPair("LTC / XMZ");
        var consent = findChild(page, "litecoinRefundConsent")
        var submit = findChild(page, "litecoinRefundSubmit")
        var outcome = findChild(page, "litecoinRefundSubmissionOutcome")
        verify(consent !== null && submit !== null && outcome !== null)
        verify(!submit.visible)
        var status = {attached:true,busy:false,valid:true,refundSubmissionConfigured:true,refundSubmissionAvailable:true,
            amounts:{ltc:"1",xmzLock:"2"},snapshot:{role:"native-funder",nativeRefundPayout:{prepared:true,attempted:false}},
            refundReview:{commitment:"review-one",details:{action:"send",destination:"signed-refund-address"},
                amounts:{refundAtoms:"1.99",feeAtoms:"0.01",maximumFeeAtoms:"0.1"}}}
        dexEngine.litecoinJournalStatus = status
        tryCompare(submit, "visible", true)
        verify(!submit.enabled)
        consent.checked = true
        tryCompare(submit, "enabled", true)
        submit.clicked()
        compare(dexEngine.submittedRefund, "review-one")
        compare(consent.checked, false)
        consent.checked = true
        dexEngine.litecoinJournalStatus = {attached:true,busy:true,valid:false,refundSubmissionConfigured:true,
            refundSubmissionAvailable:false,refundSubmission:{state:"submitting"}}
        tryCompare(consent, "checked", false)
        verify(!submit.enabled)
        dexEngine.litecoinJournalStatus = {attached:true,busy:false,valid:false,refundSubmission:{state:"unknown"}}
        verify(outcome.text.indexOf("reconcile") >= 0)
        dexEngine.litecoinJournalStatus = {attached:false,busy:false,valid:false}
    }
    function test_litecoinConfirmationIsDatedAndClearedOnFailure() {
        page.selectPair("LTC / XMZ");
        var check = findChild(page, "litecoinRefundConfirmationCheck")
        var label = findChild(page, "litecoinRefundConfirmation")
        verify(check !== null && label !== null)
        dexEngine.litecoinJournalStatus = {attached:true,busy:false,valid:false,refundSubmissionConfigured:true,
            refundCheckAvailable:true,refundCheckState:"confirmed",refundConfirmation:{confirmations:"10",
                refundAmount:"1.99",destination:"refund-address",txid:"confirmed-transaction",checkedAt:"2026-09-27T12:00:00Z"}}
        tryCompare(check, "enabled", true)
        verify(label.text.indexOf("At last check") >= 0)
        verify(label.text.indexOf("confirmed-transaction") >= 0)
        verify(label.text.indexOf("past observation") >= 0)
        check.clicked()
        compare(dexEngine.refundChecks, 1)
        dexEngine.litecoinJournalStatus = {attached:true,busy:true,valid:false,refundSubmissionConfigured:true,
            refundCheckAvailable:false,refundCheckState:"checking",refundConfirmation:{}}
        tryCompare(check, "enabled", false)
        verify(label.text.indexOf("confirmed-transaction") < 0)
        dexEngine.litecoinJournalStatus = {attached:true,busy:false,valid:false,refundSubmissionConfigured:true,
            refundCheckAvailable:false,refundCheckState:"unverified",refundConfirmation:{}}
        verify(label.text.indexOf("no payment was sent by this check") >= 0)
        verify(label.text.indexOf("confirmed-transaction") < 0)
        dexEngine.litecoinJournalStatus = {attached:false,busy:false,valid:false}
    }
    function test_litecoinFundingPreparationDoesNotApproveSubmission() {
        page.selectPair("LTC / XMZ");
        var prepare = findChild(page, "litecoinFundingPrepare")
        var status = findChild(page, "litecoinFundingPreparationStatus")
        var consent = findChild(page, "litecoinFundingConsent")
        verify(prepare !== null && status !== null)
        dexEngine.litecoinJournalStatus = {attached:true,busy:false,valid:false,
            fundingPreparationConfigured:true,fundingPreparationAvailable:false,
            fundingPreparationState:"not-prepared",fundingPreparationSource:{wallet:"source-wallet",account:2,priority:1}}
        tryCompare(prepare,"visible",true)
        verify(!prepare.enabled)
        verify(status.text.indexOf("source-wallet") >= 0)
        dexEngine.litecoinJournalStatus = {attached:true,busy:false,valid:false,
            fundingPreparationConfigured:true,fundingPreparationAvailable:true,
            fundingPreparationState:"not-prepared",fundingPreparationSource:{wallet:"source-wallet",account:2,priority:1}}
        tryCompare(prepare,"enabled",true)
        prepare.clicked()
        compare(dexEngine.fundingPreparations,1)
        verify(!consent.checked)
        compare(dexEngine.submittedFunding,"")
        dexEngine.litecoinJournalStatus = {attached:false,busy:false,valid:false}
    }
    function test_litecoinFundingSubmissionNeedsConsent() {
        page.selectPair("LTC / XMZ");
        var consent = findChild(page, "litecoinFundingConsent")
        var submit = findChild(page, "litecoinFundingSubmit")
        var outcome = findChild(page, "litecoinFundingSubmissionOutcome")
        verify(consent !== null && submit !== null && outcome !== null)
        verify(!submit.visible)
        var status = {attached:true,busy:false,valid:true,fundingSubmissionConfigured:true,fundingSubmissionAvailable:true,
            amounts:{ltc:"1",xmzLock:"2"},snapshot:{role:"native-funder",nativeFunding:{prepared:true,attempted:false},nativeRefundPayout:{}},
            fundingReview:{commitment:"review-one",details:{action:"send",destination:"signed-funding-address",reserveDestination:"signed-reserve-address"},
                amounts:{amountAtoms:"2",totalDebitAtoms:"2.01",feeAtoms:"0.01",maximumFeeAtoms:"0.1"}}}
        dexEngine.litecoinJournalStatus = status
        tryCompare(submit, "visible", true)
        verify(!submit.enabled)
        consent.checked = true
        tryCompare(submit, "enabled", true)
        submit.clicked()
        compare(dexEngine.submittedFunding, "review-one")
        compare(consent.checked, false)
        consent.checked = true
        dexEngine.litecoinJournalStatus = {attached:true,busy:true,valid:false,fundingSubmissionConfigured:true,
            fundingSubmissionAvailable:false,fundingSubmission:{state:"submitting"}}
        tryCompare(consent, "checked", false)
        verify(!submit.enabled)
        dexEngine.litecoinJournalStatus = {attached:true,busy:false,valid:false,fundingSubmission:{state:"unknown"}}
        verify(outcome.text.indexOf("reconcile") >= 0)
        dexEngine.litecoinJournalStatus = {attached:false,busy:false,valid:false}
    }
    function test_litecoinPayoutSubmissionNeedsConsent() {
        page.selectPair("LTC / XMZ");
        var consent = findChild(page, "litecoinPayoutConsent")
        var submit = findChild(page, "litecoinPayoutSubmit")
        var outcome = findChild(page, "litecoinPayoutSubmissionOutcome")
        verify(consent !== null && submit !== null && outcome !== null)
        verify(!submit.visible)
        var status = {attached:true,busy:false,valid:true,payoutSubmissionConfigured:true,payoutSubmissionAvailable:true,
            amounts:{ltc:"1",xmzLock:"2"},snapshot:{role:"litecoin-funder",nativePayout:{prepared:true,attempted:false},nativeRefundPayout:{}},
            payoutReview:{commitment:"review-one",details:{action:"send",destination:"signed-payout-address",reserveDestination:"signed-reserve-address"},
                amounts:{payoutAtoms:"1",reserveReturnAtoms:"0.99",feeAtoms:"0.01",maximumFeeAtoms:"0.1"}}}
        dexEngine.litecoinJournalStatus = status
        tryCompare(submit, "visible", true)
        verify(!submit.enabled)
        consent.checked = true
        tryCompare(submit, "enabled", true)
        submit.clicked()
        compare(dexEngine.submittedPayout, "review-one")
        compare(consent.checked, false)
        consent.checked = true
        dexEngine.litecoinJournalStatus = {attached:true,busy:true,valid:false,payoutSubmissionConfigured:true,
            payoutSubmissionAvailable:false,payoutSubmission:{state:"submitting"}}
        tryCompare(consent, "checked", false)
        verify(!submit.enabled)
        dexEngine.litecoinJournalStatus = {attached:true,busy:false,valid:false,payoutSubmission:{state:"unknown"}}
        verify(outcome.text.indexOf("reconcile") >= 0)
        dexEngine.litecoinJournalStatus = {attached:false,busy:false,valid:false}
    }
    function test_litecoinFundingConfirmationIsDatedAndClearedOnFailure() {
        page.selectPair("LTC / XMZ");
        var check = findChild(page, "litecoinFundingConfirmationCheck")
        var label = findChild(page, "litecoinFundingConfirmation")
        verify(check !== null && label !== null)
        dexEngine.litecoinJournalStatus = {attached:true,busy:false,valid:false,fundingSubmissionConfigured:true,
            fundingCheckAvailable:true,fundingCheckState:"confirmed",fundingConfirmation:{confirmations:"10",
                fundingAmount:"1",reserveAmount:"0.99",reserveDestination:"reserve-address",destination:"funding-address",txid:"confirmed-transaction",checkedAt:"2026-09-27T12:00:00Z"}}
        tryCompare(check, "enabled", true)
        verify(label.text.indexOf("At last check") >= 0)
        verify(label.text.indexOf("confirmed-transaction") >= 0)
        verify(label.text.indexOf("past observation") >= 0)
        check.clicked()
        compare(dexEngine.fundingChecks, 1)
        dexEngine.litecoinJournalStatus = {attached:true,busy:true,valid:false,fundingSubmissionConfigured:true,
            fundingCheckAvailable:false,fundingCheckState:"checking",fundingConfirmation:{}}
        tryCompare(check, "enabled", false)
        verify(label.text.indexOf("confirmed-transaction") < 0)
        dexEngine.litecoinJournalStatus = {attached:true,busy:false,valid:false,fundingSubmissionConfigured:true,
            fundingCheckAvailable:false,fundingCheckState:"unverified",fundingConfirmation:{}}
        verify(label.text.indexOf("no payment was sent by this check") >= 0)
        verify(label.text.indexOf("confirmed-transaction") < 0)
        dexEngine.litecoinJournalStatus = {attached:false,busy:false,valid:false}
    }
    function test_litecoinPayoutConfirmationIsDatedAndClearedOnFailure() {
        page.selectPair("LTC / XMZ");
        var check = findChild(page, "litecoinPayoutConfirmationCheck")
        var label = findChild(page, "litecoinPayoutConfirmation")
        verify(check !== null && label !== null)
        dexEngine.litecoinJournalStatus = {attached:true,busy:false,valid:false,payoutSubmissionConfigured:true,
            payoutCheckAvailable:true,payoutCheckState:"confirmed",payoutConfirmation:{confirmations:"10",
                payoutAmount:"1",reserveAmount:"0.99",reserveDestination:"reserve-address",destination:"payout-address",txid:"confirmed-transaction",checkedAt:"2026-09-27T12:00:00Z"}}
        tryCompare(check, "enabled", true)
        verify(label.text.indexOf("At last check") >= 0)
        verify(label.text.indexOf("confirmed-transaction") >= 0)
        verify(label.text.indexOf("past observation") >= 0)
        check.clicked()
        compare(dexEngine.payoutChecks, 1)
        dexEngine.litecoinJournalStatus = {attached:true,busy:true,valid:false,payoutSubmissionConfigured:true,
            payoutCheckAvailable:false,payoutCheckState:"checking",payoutConfirmation:{}}
        tryCompare(check, "enabled", false)
        verify(label.text.indexOf("confirmed-transaction") < 0)
        dexEngine.litecoinJournalStatus = {attached:true,busy:false,valid:false,payoutSubmissionConfigured:true,
            payoutCheckAvailable:false,payoutCheckState:"unverified",payoutConfirmation:{}}
        verify(label.text.indexOf("no payment was sent by this check") >= 0)
        verify(label.text.indexOf("confirmed-transaction") < 0)
        dexEngine.litecoinJournalStatus = {attached:false,busy:false,valid:false}
    }
    function test_multiCoinWalletCreationAndMarketIsolation() {
        page.baseCoin="BTC"
        dexEngine.lightWallets={btc:{configured:true,network:"regtest",state:"setup-ready",setupConfigured:true,walletCreateAvailable:true,creationDigest:"btc-settings",creation:{walletId:"test-wallet",vaultDirectory:"/local/btc/vault"}}}
        var panel=findChild(page,"multiCoinLightWalletPanel"),password=findChild(page,"multiCoinWalletPassword")
        var repeat=findChild(page,"multiCoinWalletConfirmation"),consent=findChild(page,"multiCoinWalletConsent"),create=findChild(page,"multiCoinWalletCreate")
        tryCompare(panel,"visible",true);compare(create.enabled,false)
        password.text="test-only password";repeat.text=password.text;consent.checked=true;tryCompare(create,"enabled",true)
        create.clicked();compare(dexEngine.multiCoinCreation.coin,"btc");compare(dexEngine.multiCoinCreation.digest,"btc-settings")
        compare(password.text,"");compare(repeat.text,"");compare(consent.checked,false)
        password.text="must-clear";repeat.text=password.text;consent.checked=true
        page.baseCoin="DOGE";tryCompare(panel,"visible",false);compare(password.text,"");compare(consent.checked,false)
        dexEngine.lightWallets={};dexEngine.multiCoinCreation={};page.baseCoin="BTC"
    }
    function test_multiCoinPaymentApprovalAndAmounts() {
        page.baseCoin="BTC"
        dexEngine.lightWallets={btc:{coin:"btc",configured:true,network:"regtest",state:"ready",walletPrepareConfigured:true,walletPrepareAvailable:true}}
        var panel=findChild(page,"multiCoinPaymentPanel"),recipient=findChild(page,"multiCoinPaymentRecipient")
        var amount=findChild(page,"multiCoinPaymentAmount"),fee=findChild(page,"multiCoinPaymentFee"),change=findChild(page,"multiCoinMinimumChange")
        var prepare=findChild(page,"multiCoinPaymentPrepare")
        tryCompare(panel,"visible",true);compare(prepare.enabled,false)
        compare(panel.parseAmount("184467440737.09551615"),"18446744073709551615")
        compare(panel.parseAmount("184467440737.09551616"),"");compare(panel.parseAmount("1e2"),"")
        recipient.text="test-only-recipient";amount.text="0.004";fee.text="0.00002";change.text="0.00001"
        tryCompare(prepare,"enabled",true);prepare.clicked()
        compare(dexEngine.multiCoinPayment.coin,"btc");compare(dexEngine.multiCoinPayment.payment.amountAtomic,"400000")
        var review={paymentAddress:recipient.text,paymentAtomic:"400000",feeAtomic:"2000",changeAtomic:"598000",changeAddress:"test-change",signingCommitment:"test-commitment",txid:"unsigned-test-txid"}
        dexEngine.lightWallets={btc:{coin:"btc",configured:true,network:"regtest",state:"wallet-prepared",walletPrepareConfigured:true,requestId:"test-request",wallet:review}}
        tryCompare(panel,"requestId","test-request")
        dexEngine.lightWallets={btc:{coin:"btc",configured:true,network:"regtest",state:"wallet-reopened",walletPrepareConfigured:true,walletSignAvailable:true,wallet:review}}
        var sign=findChild(page,"multiCoinPaymentSign"),consent=findChild(page,"multiCoinPaymentSignConsent"),password=findChild(page,"multiCoinPaymentPassword")
        tryCompare(sign,"visible",true);compare(sign.enabled,false)
        password.text="test-only-password";consent.checked=true;tryCompare(sign,"enabled",true);sign.clicked()
        compare(dexEngine.multiCoinSigning.coin,"btc");compare(password.text,"");compare(consent.checked,false)
        var signed={signingCommitment:"test-commitment",txid:"signed-test-txid",broadcastStatus:"no-local-attempt",broadcastEndpoint:{host:"localhost",port:1234}}
        dexEngine.lightWallets={btc:{coin:"btc",configured:true,network:"regtest",state:"wallet-broadcast-status",walletPrepareConfigured:true,walletBroadcastAvailable:true,wallet:signed}}
        var send=findChild(page,"multiCoinPaymentSend"),sendConsent=findChild(page,"multiCoinPaymentSendConsent")
        tryCompare(send,"visible",true);compare(send.enabled,false);sendConsent.checked=true;tryCompare(send,"enabled",true);send.clicked()
        compare(dexEngine.multiCoinSend.txid,"signed-test-txid");compare(sendConsent.checked,false)
        sendConsent.checked=true;page.baseCoin="DOGE";compare(panel.requestId,"");compare(sendConsent.checked,false)
        dexEngine.lightWallets={};dexEngine.multiCoinPayment={};dexEngine.multiCoinSigning={};dexEngine.multiCoinSend={};page.baseCoin="BTC"
    }
    function test_mnemonicPhraseAndPassphraseDisappearOnMarketChange() {
        page.baseCoin="BTC"
        dexEngine.lightWallets={btc:{configured:true,walletBackupConfigured:true,recoveryEncoding:"bip39-english-v1",recoveryVisible:true}}
        var panel=findChild(findChild(page,"multiCoinLightWalletPanel"),"lightWalletRecoveryPanel")
        var phrase=findChild(panel,"lightRecoverySeed"),passphrase=findChild(panel,"lightRecoveryPassphrase")
        tryCompare(panel,"visible",true);tryCompare(phrase,"visible",true);tryCompare(passphrase,"visible",true)
        compare(phrase.text,"test-only-recovery-value");compare(passphrase.text,"test-only-passphrase")
        compare(findChild(panel,"lightRecoveryImportSeed").maximumLength,1024)
        page.baseCoin="DOGE";tryCompare(phrase,"text","");tryCompare(passphrase,"text","")
        compare(dexEngine.recoveryHiddenCoin,"btc");dexEngine.lightWallets={};page.baseCoin="BTC"
    }
    function test_mnemonicConfigurationSwitchesRecoveryInputFormat() {
        page.baseCoin="BTC"
        dexEngine.lightWallets={btc:{configured:true,walletBackupConfigured:true,recoveryAvailable:true,recoveryEncoding:"raw-bip32-seed-hex-v1"}}
        var panel=findChild(findChild(page,"multiCoinLightWalletPanel"),"lightWalletRecoveryPanel")
        tryCompare(panel,"visible",true)
        var seed=findChild(panel,"lightRecoveryImportSeed");seed.text=new Array(65).join("a")
        dexEngine.lightWallets={btc:{configured:true,walletBackupConfigured:true,recoveryAvailable:false,recoveryEncoding:"bip39-english-v1"}}
        tryCompare(seed,"maximumLength",1024);verify(panel.visible);compare(seed.text,"")
        dexEngine.lightWallets={}
    }
    function test_mnemonicImportRequiresPhraseApprovalAndMatchingPasswords() {
        page.baseCoin="BTC";dexEngine.mnemonicRestoreRequest={}
        dexEngine.lightWallets={btc:{configured:true,walletBackupConfigured:true,recoveryAvailable:true,recoveryEncoding:"bip39-english-v1",configurationDigest:"mnemonic-config"}}
        var panel=findChild(findChild(page,"multiCoinLightWalletPanel"),"lightWalletRecoveryPanel")
        var words=findChild(panel,"lightRecoveryImportSeed"),extra=findChild(panel,"lightRecoveryImportPassphrase")
        var password=findChild(panel,"lightRecoveryImportPassword"),confirmation=findChild(panel,"lightRecoveryImportConfirmation")
        var consent=findChild(panel,"lightRecoveryImportConsent"),submit=findChild(panel,"lightRecoveryImport")
        tryCompare(panel,"visible",true);verify(extra.visible);verify(!submit.enabled)
        words.text="abandon";password.text="disposable";confirmation.text=password.text;consent.checked=true;verify(!submit.enabled)
        words.text=new Array(12).join("abandon ")+"about";extra.text="additional test passphrase"
        confirmation.text="mismatch";verify(!submit.enabled);confirmation.text=password.text;verify(submit.enabled)
        submit.clicked();compare(dexEngine.mnemonicRestoreRequest.coin,"btc");compare(dexEngine.mnemonicRestoreRequest.digest,"mnemonic-config")
        compare(words.text,"");compare(extra.text,"");compare(password.text,"");compare(confirmation.text,"");verify(!consent.checked)
        dexEngine.lightWallets={}
    }
    function test_multiCoinRecoveryImportApproval() {
        page.baseCoin="BTC";dexEngine.recoveryRestoreRequest={}
        dexEngine.lightWallets={btc:{configured:true,walletBackupConfigured:true,recoveryAvailable:true,configurationDigest:"local-config"}}
        var panel=findChild(findChild(page,"multiCoinLightWalletPanel"),"lightWalletRecoveryPanel")
        var seed=findChild(panel,"lightRecoveryImportSeed"),password=findChild(panel,"lightRecoveryImportPassword")
        var confirmation=findChild(panel,"lightRecoveryImportConfirmation"),consent=findChild(panel,"lightRecoveryImportConsent")
        var submit=findChild(panel,"lightRecoveryImport")
        tryCompare(panel,"visible",true);verify(!submit.enabled)
        seed.text=new Array(65).join("a");password.text="disposable";confirmation.text="wrong";consent.checked=true;verify(!submit.enabled)
        confirmation.text=password.text;verify(submit.enabled);submit.clicked()
        compare(dexEngine.recoveryRestoreRequest.coin,"btc");compare(dexEngine.recoveryRestoreRequest.digest,"local-config")
        compare(seed.text,"");compare(password.text,"");compare(confirmation.text,"");verify(!consent.checked)
        seed.text=new Array(65).join("a");password.text="disposable";page.baseCoin="DOGE"
        compare(seed.text,"");compare(password.text,"")
        dexEngine.lightWallets={};page.baseCoin="BTC"
    }
    function test_multiCoinRecoveryRequiresApprovalAndClearsOnMarketChange() {
        page.baseCoin="BTC"
        dexEngine.recoveryRevealRequest={}
        dexEngine.lightWallets={btc:{configured:true,walletBackupConfigured:true,recoveryAvailable:true,configurationDigest:"btc-local-config"}}
        var parent=findChild(page,"multiCoinLightWalletPanel")
        var panel=findChild(parent,"lightWalletRecoveryPanel")
        var password=findChild(panel,"lightRecoveryPassword"),consent=findChild(panel,"lightRecoveryConsent")
        var reveal=findChild(panel,"lightRecoveryReveal"),seed=findChild(panel,"lightRecoverySeed")
        tryCompare(panel,"visible",true);verify(!reveal.enabled);verify(!seed.visible);compare(seed.text,"")
        password.text="disposable-password";verify(!reveal.enabled)
        consent.checked=true;verify(reveal.enabled);reveal.clicked()
        compare(dexEngine.recoveryRevealRequest.coin,"btc");compare(dexEngine.recoveryRevealRequest.digest,"btc-local-config")
        compare(dexEngine.recoveryRevealRequest.approved,true);compare(password.text,"");compare(consent.checked,false)
        dexEngine.lightWallets={btc:{configured:true,walletBackupConfigured:true,recoveryVisible:true}}
        tryCompare(seed,"visible",true);compare(seed.text,"test-only-recovery-value")
        page.baseCoin="DOGE";tryCompare(seed,"text","");compare(dexEngine.recoveryHiddenCoin,"btc")
        dexEngine.lightWallets={};dexEngine.recoveryRevealRequest={};page.baseCoin="BTC"
    }
    function test_multiCoinReceiveUsesSelectedCoin() {
        page.baseCoin="DOGE";dexEngine.lightWallets={doge:{configured:true,network:"regtest",state:"ready",walletReceiveAvailable:true}}
        var receive=findChild(page,"multiCoinWalletReceive");tryCompare(receive,"visible",true)
        receive.clicked();compare(dexEngine.multiCoinReceived,"doge")
        dexEngine.lightWallets={doge:{configured:true,network:"regtest",state:"wallet-address",wallet:{address:"test-only-address"}}}
        var address=findChild(page,"multiCoinWalletAddress");tryCompare(address,"text","test-only-address")
        page.baseCoin="BTC";tryCompare(address,"visible",false);compare(address.text,"")
        dexEngine.lightWallets={};dexEngine.multiCoinReceived=""
    }
    function test_recoveryStatus() {
        var label = findChild(page, "nativeRecoveryEvidence")
        verify(label !== null)
        verify(label.visible)
        verify(label.text.indexOf("no current verified result") >= 0)
        var check = findChild(page, "nativeRecoveryCheck")
        verify(check !== null && !check.enabled)
        dexEngine.nativeRecoveryStatus = {valid:false,retryAllowed:false,checkAvailable:true}
        tryCompare(check, "enabled", true)
        check.clicked()
        compare(dexEngine.recoveryRequests, 1)
        dexEngine.nativeRecoveryStatus = {valid:true,txid:"test-transaction",confirmations:"10",retryAllowed:false}
        tryVerify(function() { return label.text.indexOf("test-transaction") >= 0 })
        verify(label.text.indexOf("does not authorize") >= 0)
        verify(label.text.indexOf("all swap funds") >= 0)
        tryCompare(check, "enabled", false)
        dexEngine.nativeRecoveryStatus = {valid:false,retryAllowed:false}
        tryVerify(function() { return label.text.indexOf("no current verified result") >= 0 })
        verify(label.text.indexOf("test-transaction") < 0)
        dexEngine.nativeWalletStatus = {attached:false,state:"unattached",readyForTrading:false}
        tryCompare(label, "visible", false)
    }
    function test_guidedSetupRequiresReviewAndResetsConsent() {
        page.baseCoin="BTC";dexEngine.lightWallets={};dexEngine.lightWalletSetupAvailable=true;dexEngine.setupRequest={}
        var panel=findChild(page,"lightWalletSetupPanel");tryCompare(panel,"visible",true)
        var folder=findChild(panel,"lightSetupDirectory"),consent=findChild(panel,"lightSetupConsent"),button=findChild(panel,"lightSetupPrepare")
        var host=findChild(panel,"lightSetupHost"),port=findChild(panel,"lightSetupPort")
        folder.text="/local/wallets";verify(!button.enabled)
        consent.checked=true;verify(button.enabled)
        host.text="localhost";verify(!consent.checked);consent.checked=true;verify(!button.enabled)
        port.text="50002";findChild(panel,"lightSetupAccountIndex").value=3;consent.checked=true;verify(button.enabled);button.clicked()
        compare(dexEngine.setupRequest.coin,"btc");compare(dexEngine.setupRequest.network,"regtest");compare(dexEngine.setupRequest.port,50002);compare(dexEngine.setupRequest.accountIndex,3);verify(!consent.checked)
        consent.checked=true;page.baseCoin="BCH";tryCompare(consent,"checked",false)
        compare(findChild(panel,"lightSetupNetwork").count,1)
        dexEngine.lightWallets={bch:{configured:true}};tryCompare(panel,"visible",false)
        dexEngine.lightWalletSetupAvailable=false;dexEngine.lightWallets={};page.baseCoin="BTC";host.text="";port.text="";folder.text=""
    }

    function test_backupRecoveryRequiresSeparatePreviewAndRestoreConsent() {
        page.baseCoin="BTC"
        dexEngine.lightWallets={btc:{backupSetupConfigured:true,backupImportPreviewAvailable:true,backupSetup:{setupDirectory:"/private/restored"}}}
        var panel=findChild(page,"lightWalletBackupRestorePanel"),password=findChild(panel,"lightBackupRestorePassword")
        var preview=findChild(panel,"lightBackupRestorePreview"),apply=findChild(panel,"lightBackupRestoreApply"),consent=findChild(panel,"lightBackupRestoreConsent")
        tryCompare(panel,"visible",true);verify(!preview.enabled);password.text="disposable secret";verify(preview.enabled);preview.clicked()
        compare(password.text,"");verify(dexEngine.backupPreviewRequest.passwordProvided)
        dexEngine.lightWallets={btc:{backupSetupConfigured:true,backupImportPreviewAvailable:true,backupImportAvailable:true,
            backupSetup:{setupDirectory:"/private/restored"},backupImportReview:{reviewDigest:"backup-review",review:{network:"regtest",configurationFile:"/private/restored/wallet.json",backupDigest:"fingerprint",allocated:[3,1],configuration:{wallet:{account:{account:2}}}}}}}
        verify(!apply.enabled);password.text="disposable secret";verify(!apply.enabled);consent.checked=true;verify(apply.enabled);apply.clicked()
        compare(dexEngine.backupImportRequest.digest,"backup-review");verify(dexEngine.backupImportRequest.approved);compare(password.text,"");verify(!consent.checked)
        password.text="clear on coin change";consent.checked=true;page.baseCoin="DOGE";compare(password.text,"");verify(!consent.checked)
        dexEngine.lightWallets={}
    }
    function test_incompleteBackupReviewCannotAuthorizeRestore() {
        page.baseCoin="BTC"
        dexEngine.backupImportRequest={}
        dexEngine.lightWallets={btc:{backupSetupConfigured:true,backupImportAvailable:true,
            backupSetup:{setupDirectory:"/private/restored"},backupImportReview:{reviewDigest:"incomplete",review:{network:"regtest"}}}}
        var panel=findChild(page,"lightWalletBackupRestorePanel"),password=findChild(panel,"lightBackupRestorePassword")
        var apply=findChild(panel,"lightBackupRestoreApply"),consent=findChild(panel,"lightBackupRestoreConsent")
        compare(panel.reviewReady,false)
        compare(findChild(panel,"lightBackupRestoreReview").text,"")
        password.text="disposable secret";consent.checked=true
        verify(!apply.enabled);apply.clicked();verify(dexEngine.backupImportRequest.digest === undefined)
        dexEngine.lightWallets={};page.baseCoin="BTC"
    }

    function test_backupFolderSetupDoesNotCreateNewKeys() {
        page.baseCoin="BTC";dexEngine.lightWallets={};dexEngine.lightWalletSetupAvailable=true;dexEngine.backupSetupRequest={}
        var panel=findChild(page,"lightWalletSetupPanel"),source=findChild(panel,"lightSetupBackupSource"),folder=findChild(panel,"lightSetupDirectory"),consent=findChild(panel,"lightSetupConsent")
        var button=findChild(panel,"lightSetupRestoreBackup"),create=findChild(panel,"lightSetupPrepare")
        findChild(panel,"lightSetupHost").text="";folder.text="/private/wallets";source.text="/private/backup";verify(!button.enabled)
        consent.checked=true;verify(button.enabled);verify(!create.enabled);button.clicked()
        compare(dexEngine.backupSetupRequest.coin,"btc");compare(dexEngine.backupSetupRequest.backup,"/private/backup");verify(!consent.checked)
        source.text="";folder.text="";dexEngine.lightWalletSetupAvailable=false
    }

    function test_helperUpgradeReviewClearsWhenFolderOrCoinChanges() {
        page.baseCoin="BTC";dexEngine.lightWallets={};dexEngine.lightWalletSetupAvailable=true;dexEngine.helperUpgradeRequest={}
        var panel=findChild(page,"lightWalletSetupPanel"),folder=findChild(panel,"lightSetupExistingFolder")
        var review=findChild(panel,"lightHelperReview"),consent=findChild(panel,"lightHelperConsent"),apply=findChild(panel,"lightHelperApply")
        folder.text="/local/saved-wallet";review.clicked();verify(!apply.enabled)
        consent.checked=true;verify(apply.enabled)
        folder.text="/local/other-wallet";verify(!apply.enabled);verify(!consent.checked)
        review.clicked();consent.checked=true;apply.clicked()
        compare(dexEngine.helperUpgradeRequest.coin,"btc");compare(dexEngine.helperUpgradeRequest.directory,"/local/other-wallet")
        compare(dexEngine.helperUpgradeRequest.commitment,"reviewed-helper");verify(!apply.enabled)
        review.clicked();consent.checked=true;page.baseCoin="DOGE";verify(!apply.enabled);verify(!consent.checked)
        folder.text="";dexEngine.lightWalletSetupAvailable=false
    }

    function test_savedWalletFolderOpenDoesNotCreateWallet() {
        page.baseCoin="BTC";dexEngine.lightWallets={};dexEngine.lightWalletSetupAvailable=true
        dexEngine.openFolderRequest={};dexEngine.setupRequest={}
        var panel=findChild(page,"lightWalletSetupPanel"),folder=findChild(panel,"lightSetupExistingFolder"),button=findChild(panel,"lightSetupOpenExisting")
        folder.text="relative/path";verify(!button.enabled)
        folder.text="/local/saved-wallet";verify(button.enabled);button.clicked()
        compare(dexEngine.openFolderRequest.coin,"btc");compare(dexEngine.openFolderRequest.directory,"/local/saved-wallet")
        compare(JSON.stringify(dexEngine.setupRequest),"{}")
        folder.text="";dexEngine.lightWalletSetupAvailable=false
    }

    function test_rememberedWalletFailureNoticeFollowsSelectedCoin() {
        page.baseCoin="BTC";dexEngine.lightWalletSetupNotices={btc:"Saved wallet could not be verified"}
        var notice=findChild(page,"lightWalletSetupNotice");tryCompare(notice,"visible",true)
        compare(notice.text,"Saved wallet could not be verified")
        page.baseCoin="LTC";tryCompare(notice,"visible",false)
        dexEngine.lightWalletSetupNotices={};page.baseCoin="BTC"
    }

    function test_mnemonicImportReviewApprovalAndSecretClearing() {
        page.baseCoin="BTC";dexEngine.importPreviewRequest={};dexEngine.importCommitRequest={}
        var state={configured:true,setupConfigured:true,walletImportAvailable:true,creationDigest:"setup-digest",creation:{seedEncoding:"bip39-english-v1"}}
        dexEngine.lightWallets={btc:state}
        var panel=findChild(page,"lightWalletImportPanel");tryCompare(panel,"visible",true)
        var words=findChild(panel,"lightImportPhrase"),additional=findChild(panel,"lightImportPassphrase")
        var password=findChild(panel,"lightImportPassword"),confirmation=findChild(panel,"lightImportConfirmation")
        var consent=findChild(panel,"lightImportConsent"),commit=findChild(panel,"lightImportCommit"),preview=findChild(panel,"lightImportPreview")
        words.text="abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon about";additional.text="fixture"
        verify(!commit.enabled);preview.clicked();compare(dexEngine.importPreviewRequest.coin,"btc")
        state.importReview={reviewDigest:"review-digest",review:{account:{account:0},network:"regtest",receivePath:"84'/1'/0'/0/0",receiveAddress:"fixture-address",additionalPassphraseUsed:true}}
        dexEngine.lightWallets={btc:state}
        password.text="password";confirmation.text="wrong";consent.checked=true;verify(!commit.enabled)
        confirmation.text="password";consent.checked=true;verify(commit.enabled)
        additional.text="changed";verify(!consent.checked);consent.checked=true;verify(!commit.enabled)
        preview.clicked();consent.checked=true;verify(commit.enabled);commit.clicked()
        compare(dexEngine.importCommitRequest.coin,"btc");compare(dexEngine.importCommitRequest.digest,"review-digest")
        compare(words.text,"");compare(additional.text,"");compare(password.text,"");compare(confirmation.text,"");verify(!consent.checked)
        words.text="temporary recovery material";page.baseCoin="DOGE";tryCompare(words,"text","")
        dexEngine.lightWallets={};page.baseCoin="BTC"
    }
    function test_pendingMnemonicImportDoesNotOfferAnotherImportForm() {
        page.baseCoin="BTC";dexEngine.lightWallets={btc:{configured:true,setupConfigured:true,creation:{seedEncoding:"bip39-english-v1"},state:"wallet-import-pending",walletImportPending:true,wallet:{imported:true}}}
        var panel=findChild(page,"lightWalletImportPanel");tryCompare(panel,"completed",true)
        verify(!findChild(panel,"lightImportPhrase").visible);verify(!findChild(panel,"lightImportCommit").visible)
        dexEngine.lightWallets={}
    }

    function test_importDiscoveryConsentRangesAndProvisionalResult() {
        page.baseCoin="BTC";dexEngine.discoveryRequest={}
        var state={configured:true,setupConfigured:true,walletImportPending:true,importDiscoveryAvailable:true,
            state:"wallet-import-pending",creationDigest:"pending-setup",creation:{seedEncoding:"bip39-english-v1",endpoint:{host:"local-fixture"}}}
        dexEngine.lightWallets={btc:state}
        var panel=findChild(page,"lightWalletDiscoveryPanel");tryCompare(panel,"visible",true)
        var consent=findChild(panel,"lightDiscoveryConsent"),button=findChild(panel,"lightDiscoveryStart")
        var receive=findChild(panel,"lightDiscoveryReceiveMinimum"),change=findChild(panel,"lightDiscoveryChangeMinimum")
        verify(!button.enabled);consent.checked=true;verify(button.enabled)
        receive.value=12;verify(!consent.checked);change.value=4;consent.checked=true;button.clicked()
        compare(dexEngine.discoveryRequest.coin,"btc");compare(dexEngine.discoveryRequest.receiveMinimum,12)
        compare(dexEngine.discoveryRequest.changeMinimum,4);verify(dexEngine.discoveryRequest.approved);verify(!consent.checked)
        state.state="wallet-import-discovered";state.importDiscovery={reviewDigest:"digest",review:{observedConfirmedAtomic:"1000001",coverage:{allocatedAfter:[32,24]}}}
        dexEngine.lightWallets={btc:state}
        var result=findChild(panel,"lightDiscoveryResult");tryCompare(result,"visible",true)
        verify(result.text.indexOf("0.01000001 BTC")>=0);verify(result.text.indexOf("provisional")>=0)
        verify(result.text.indexOf("activation")>=0);verify(!findChild(page,"lightImportPhrase").visible)
        consent.checked=true;page.baseCoin="DOGE";tryCompare(consent,"checked",false);compare(receive.value,0)
        dexEngine.lightWallets={};page.baseCoin="BTC"
    }

    function test_importRecoveryConsentSecretsAndHistoricalRestart() {
        page.baseCoin="BTC";dexEngine.importRecoveryRequest={}
        var state={configured:true,setupConfigured:true,walletImportPending:true,importRecoveryAvailable:true,
            state:"wallet-import-pending",creationDigest:"recover-setup",creation:{seedEncoding:"bip39-english-v1",setupDirectory:"/local/setup"},
            importRecoveryRequests:[{reviewDigest:"saved-digest",height:42,backupVerified:false}]}
        dexEngine.lightWallets={btc:state}
        var panel=findChild(page,"lightWalletImportRecoveryPanel");tryCompare(panel,"visible",true)
        var consent=findChild(panel,"lightImportRecoveryConsent"),button=findChild(panel,"lightImportRecoveryStart"),password=findChild(panel,"lightImportRecoveryPassword")
        verify(!button.enabled);password.text="temporary password";verify(!button.enabled);consent.checked=true;verify(button.enabled)
        button.clicked();compare(dexEngine.importRecoveryRequest.digest,"saved-digest");verify(dexEngine.importRecoveryRequest.approved)
        compare(password.text,"");verify(!consent.checked);verify(!findChild(panel,"lightImportRecoveryResult").visible)
        state.importRecovery={backupVerified:true};state.state="wallet-import-recovered";dexEngine.lightWallets={btc:state}
        var result=findChild(panel,"lightImportRecoveryResult");tryCompare(result,"visible",true);verify(result.text.indexOf("activation")>=0)
        password.text="temporary again";consent.checked=true;page.baseCoin="DOGE";tryCompare(password,"text","");verify(!consent.checked)
        dexEngine.lightWallets={};page.baseCoin="BTC"
    }

    function test_importActivationRequiresFreshVerificationAndConsent() {
        page.baseCoin="BTC";dexEngine.importActivationRequest={}
        var state={configured:true,walletImportPending:true,importActivationAvailable:false,state:"wallet-import-pending"}
        dexEngine.lightWallets={btc:state}
        var panel=findChild(page,"lightWalletImportActivationPanel");tryCompare(panel,"visible",true)
        var password=findChild(panel,"lightImportActivationPassword"),consent=findChild(panel,"lightImportActivationConsent"),button=findChild(panel,"lightImportActivationStart")
        verify(!button.enabled);verify(!password.enabled)
        state.importActivationAvailable=true;state.importActivationDigest="recovery-digest";state.state="wallet-import-recovered";dexEngine.lightWallets={btc:state}
        password.text="temporary password";verify(!button.enabled);consent.checked=true;verify(button.enabled);button.clicked()
        compare(dexEngine.importActivationRequest.digest,"recovery-digest");verify(dexEngine.importActivationRequest.approved)
        compare(password.text,"");verify(!consent.checked)
        dexEngine.lightWallets={btc:{configured:true,importActivated:true,walletImportPending:false,state:"ready"}}
        tryCompare(panel,"activated",true);verify(!button.visible)
        verify(findChild(panel,"lightImportActivationStatus").text.indexOf("not enabled")>=0)
        state.state="wallet-import-activation-incomplete";state.importActivationAvailable=false;dexEngine.lightWallets={btc:state}
        tryCompare(panel,"activated",false);verify(!button.enabled)
        verify(findChild(panel,"lightImportActivationStatus").text.indexOf("interrupted")>=0)
        state.state="wallet-import-activation-invalid";dexEngine.lightWallets={btc:state}
        verify(findChild(panel,"lightImportActivationStatus").text.indexOf("could not be verified")>=0)
        dexEngine.lightWallets={};page.baseCoin="BTC"
    }

}
