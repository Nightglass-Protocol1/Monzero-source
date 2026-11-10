#include "DexEngine.h"
#include <sodium.h>
#include "LightWalletSetup.h"
#include "LightWalletHelperUpgrade.h"
#include <QSettings>
#include "BchAdmissionJob.h"
#include "BchRefundProtectionJob.h"
#include "BchFundingReceiptJob.h"
#include "BchNativePreparationJob.h"
#include "BchNativeRelayJob.h"
#include "BchNativeProofJob.h"
#include "BchNativeObservationJob.h"
#include "EvmTradeJob.h"
#include "EvmMarketJob.h"
#include "BtcMarketJob.h"
#include "DexWalletService.h"
#include "BchSuccessReleaseReviewJob.h"
#include "BchSuccessPreparationJob.h"
#include "BchPublicationRepairJob.h"
#include <type_traits>
#include "BchRecoveryRetentionJob.h"
#include "BchCancellationJob.h"
#include "BchPunishmentJob.h"
#include "BchRefundWalletJob.h"
#include "DexRiskControl.h"
#include "DexDeployment.h"
#include <QJsonArray>
#include "BtcTradeSetup.h"
#include <QCoreApplication>
#include <QNetworkReply>
#include <QNetworkAccessManager>
#include "BchAdmissionProtocol.h"
#include "LitecoinJournalProtocol.h"
#include "SwapWalletRpcProcess.h"
#include <QDir>
#include <QEventLoop>
#include <QFileInfo>
#include <QTimer>
#include "BchRefundReleaseJob.h"
#include "BchFundingJob.h"
#include "BchPreparationJob.h"
#include "LitecoinLightJob.h"
#include "LitecoinLightFundingController.h"
#include "BchLightFundingController.h"
#include "LitecoinJournalJob.h"
#include "LitecoinNegotiationSession.h"
#include "LitecoinReservationJob.h"
#include "LitecoinRedemptionJob.h"
#include "DexUtils.h"
#include "MarketRegistry.h"
#include <boost/multiprecision/cpp_int.hpp>
#include "OfferBookStore.h"
#include "NativeFeePreviewController.h"
#include "SwapWalletSession.h"
#include "NativeQuoteCoordinator.h"

#include <QRegularExpression>
#include <QFile>
#include <QJsonDocument>
#include <QHash>
#include <QDateTime>
#include <QUuid>
#include <limits>
#include <QtConcurrent/QtConcurrentRun>
#include <QPointer>

namespace {
constexpr int xmz_decimals = 11;
constexpr quint64 xmz_network_fee = 250000000ULL; // 0.0025 XMZ
constexpr quint64 minimum_net_xmz = 100000000000ULL; // 1 XMZ
constexpr quint64 protocol_fee_bps = 0; // First-release unsigned previews: no app trading fee.
}

DexEngine::DexEngine(QObject *parent) : QObject(parent),
    m_bchFundingReceipt(new BchFundingReceiptJob(this)),
    m_bchNativePreparation(new BchNativePreparationJob(this)),
    m_bchNativeRelay(new BchNativeRelayJob(this)),
    m_bchNativeProof(new BchNativeProofJob(this)),
    m_bchNativeObservation(new BchNativeObservationJob(this)),
    m_evmTrade(new EvmTradeJob(this)),
    m_evmTicker(new QTimer(this)),
    m_bchSuccessReleaseReview(new BchSuccessReleaseReviewJob(this)),
    m_bchSuccessSignedReleaseReview(new BchSuccessReleaseReviewJob(this)),
    m_bchSuccessSignatureReview(new BchSuccessReleaseReviewJob(this)),
    m_bchSuccessPreparation(new BchSuccessPreparationJob(this)),
    m_bchPublicationRepair(new BchPublicationRepairJob(this)),
    m_bchRefundProtection(new BchRefundProtectionJob(this)),
    m_bchRecoveryRetention(new BchRecoveryRetentionJob(this)),
    m_bchCancellation(new BchCancellationJob(this)),
    m_bchPunishment(new BchPunishmentJob(this)),
    m_bchRefundWallet(new BchRefundWalletJob(this)),
    m_bchRefundRelease(new BchRefundReleaseJob(this)),
    m_bchAdmission(new BchAdmissionJob(this)),
    m_bchLightFunding(new BchLightFundingController(this)),
    m_ltcLight(new LitecoinLightJob(this)),
    m_bchPreparation(new BchPreparationJob(this)),
    m_bchFunding(new BchFundingJob(this)),
    m_ltcJournal(new LitecoinJournalJob(this)),
    m_ltcReservation(new LitecoinReservationJob(this)),
    m_ltcLightFunding(new LitecoinLightFundingController(this)),
    m_ltcRedemption(new LitecoinRedemptionJob(this)),
    m_feePreviewController(new NativeFeePreviewController(this)),
    m_researchWatcher(new QFutureWatcher<QVariantMap>(this))
{
    m_riskControl=new DexRiskControl(this);m_riskControl->setNetwork(DexDeployment::litecoinNetwork());
    // A second kill switch / caps control for XMZ<->EVM trades. Its network is bound to the
    // trade's chain when the EVM trade is configured (evmControlNetwork), with its own ledger.
    m_evmRiskControl=new DexRiskControl(this);
    connect(m_evmRiskControl,&DexRiskControl::changed,this,&DexEngine::dexRiskChanged);
    connect(m_riskControl,&DexRiskControl::changed,this,&DexEngine::dexRiskChanged);
    connect(m_ltcJournal,&LitecoinJournalJob::changed,this,&DexEngine::dexRiskChanged);
    connect(m_ltcLight,&LitecoinLightJob::changed,this,&DexEngine::litecoinLightChanged);
    m_lightWallets.insert("ltc",m_ltcLight);
    for (const auto &coin:QStringList{"btc","bch","doge"}) m_lightWallets.insert(coin,new LitecoinLightJob(coin,this));
    for (auto *job:m_lightWallets) connect(job,&LitecoinLightJob::changed,this,&DexEngine::lightWalletsChanged);
    connect(this,&DexEngine::bchFundingReceiptChanged,this,&DexEngine::advanceBchStartupChecks,Qt::QueuedConnection);
    connect(m_bchFundingReceipt,&BchFundingReceiptJob::changed,this,&DexEngine::bchFundingReceiptChanged);
    connect(m_bchFundingReceipt,&BchFundingReceiptJob::changed,this,&DexEngine::bchRefundProtectionChanged);
    connect(m_bchRefundProtection,&BchRefundProtectionJob::changed,this,&DexEngine::bchFundingReceiptChanged);
    connect(m_bchFundingReceipt,&BchFundingReceiptJob::changed,this,&DexEngine::bchRecoveryRetentionChanged);
    connect(m_bchRecoveryRetention,&BchRecoveryRetentionJob::changed,this,&DexEngine::bchFundingReceiptChanged);
    connect(m_bchFundingReceipt,&BchFundingReceiptJob::changed,this,&DexEngine::bchCancellationChanged);
    connect(m_bchCancellation,&BchCancellationJob::changed,this,&DexEngine::bchFundingReceiptChanged);
    connect(m_bchFundingReceipt,&BchFundingReceiptJob::changed,this,&DexEngine::bchRefundReleaseChanged);
    connect(m_bchRefundRelease,&BchRefundReleaseJob::changed,this,&DexEngine::bchFundingReceiptChanged);
    connect(m_bchFundingReceipt,&BchFundingReceiptJob::changed,this,&DexEngine::bchAdmissionChanged);
    connect(m_bchAdmission,&BchAdmissionJob::changed,this,&DexEngine::bchFundingReceiptChanged);
    connect(m_bchFundingReceipt,&BchFundingReceiptJob::changed,this,&DexEngine::bchPreparationChanged);
    connect(m_bchPreparation,&BchPreparationJob::changed,this,&DexEngine::bchFundingReceiptChanged);
    connect(m_bchFundingReceipt,&BchFundingReceiptJob::changed,this,&DexEngine::bchFundingChanged);
    connect(m_bchFunding,&BchFundingJob::changed,this,&DexEngine::bchFundingReceiptChanged);
    connect(this,&DexEngine::bchNativePreparationChanged,this,&DexEngine::advanceBchStartupChecks,Qt::QueuedConnection);
    connect(m_bchNativePreparation,&BchNativePreparationJob::changed,this,&DexEngine::bchNativePreparationChanged);
    connect(m_bchNativePreparation,&BchNativePreparationJob::changed,this,&DexEngine::bchRefundProtectionChanged);
    connect(m_bchRefundProtection,&BchRefundProtectionJob::changed,this,&DexEngine::bchNativePreparationChanged);
    connect(m_bchNativePreparation,&BchNativePreparationJob::changed,this,&DexEngine::bchRecoveryRetentionChanged);
    connect(m_bchRecoveryRetention,&BchRecoveryRetentionJob::changed,this,&DexEngine::bchNativePreparationChanged);
    connect(m_bchNativePreparation,&BchNativePreparationJob::changed,this,&DexEngine::bchCancellationChanged);
    connect(m_bchCancellation,&BchCancellationJob::changed,this,&DexEngine::bchNativePreparationChanged);
    connect(m_bchNativePreparation,&BchNativePreparationJob::changed,this,&DexEngine::bchRefundReleaseChanged);
    connect(m_bchRefundRelease,&BchRefundReleaseJob::changed,this,&DexEngine::bchNativePreparationChanged);
    connect(m_bchNativePreparation,&BchNativePreparationJob::changed,this,&DexEngine::bchAdmissionChanged);
    connect(m_bchAdmission,&BchAdmissionJob::changed,this,&DexEngine::bchNativePreparationChanged);
    connect(m_bchNativePreparation,&BchNativePreparationJob::changed,this,&DexEngine::bchPreparationChanged);
    connect(m_bchPreparation,&BchPreparationJob::changed,this,&DexEngine::bchNativePreparationChanged);
    connect(m_bchNativePreparation,&BchNativePreparationJob::changed,this,&DexEngine::bchFundingChanged);
    connect(m_bchFunding,&BchFundingJob::changed,this,&DexEngine::bchNativePreparationChanged);
    connect(m_bchFundingReceipt,&BchFundingReceiptJob::changed,this,&DexEngine::bchNativePreparationChanged);
    connect(m_bchNativePreparation,&BchNativePreparationJob::changed,this,&DexEngine::bchFundingReceiptChanged);
    connect(this,&DexEngine::bchNativeRelayChanged,this,&DexEngine::advanceBchStartupChecks,Qt::QueuedConnection);
    connect(m_bchNativeRelay,&BchNativeRelayJob::changed,this,&DexEngine::bchNativeRelayChanged);
    connect(m_bchNativeRelay,&BchNativeRelayJob::changed,this,&DexEngine::bchRefundProtectionChanged);
    connect(m_bchRefundProtection,&BchRefundProtectionJob::changed,this,&DexEngine::bchNativeRelayChanged);
    connect(m_bchNativeRelay,&BchNativeRelayJob::changed,this,&DexEngine::bchRecoveryRetentionChanged);
    connect(m_bchRecoveryRetention,&BchRecoveryRetentionJob::changed,this,&DexEngine::bchNativeRelayChanged);
    connect(m_bchNativeRelay,&BchNativeRelayJob::changed,this,&DexEngine::bchCancellationChanged);
    connect(m_bchCancellation,&BchCancellationJob::changed,this,&DexEngine::bchNativeRelayChanged);
    connect(m_bchNativeRelay,&BchNativeRelayJob::changed,this,&DexEngine::bchRefundReleaseChanged);
    connect(m_bchRefundRelease,&BchRefundReleaseJob::changed,this,&DexEngine::bchNativeRelayChanged);
    connect(m_bchNativeRelay,&BchNativeRelayJob::changed,this,&DexEngine::bchAdmissionChanged);
    connect(m_bchAdmission,&BchAdmissionJob::changed,this,&DexEngine::bchNativeRelayChanged);
    connect(m_bchNativeRelay,&BchNativeRelayJob::changed,this,&DexEngine::bchPreparationChanged);
    connect(m_bchPreparation,&BchPreparationJob::changed,this,&DexEngine::bchNativeRelayChanged);
    connect(m_bchNativeRelay,&BchNativeRelayJob::changed,this,&DexEngine::bchFundingChanged);
    connect(m_bchFunding,&BchFundingJob::changed,this,&DexEngine::bchNativeRelayChanged);
    connect(m_bchFundingReceipt,&BchFundingReceiptJob::changed,this,&DexEngine::bchNativeRelayChanged);
    connect(m_bchNativeRelay,&BchNativeRelayJob::changed,this,&DexEngine::bchFundingReceiptChanged);
    connect(m_bchNativeRelay,&BchNativeRelayJob::changed,this,&DexEngine::bchNativePreparationChanged);
    connect(m_bchNativePreparation,&BchNativePreparationJob::changed,this,&DexEngine::bchNativeRelayChanged);
    connect(this,&DexEngine::bchNativeProofChanged,this,&DexEngine::advanceBchStartupChecks,Qt::QueuedConnection);
    connect(m_bchNativeProof,&BchNativeProofJob::changed,this,&DexEngine::bchNativeProofChanged);
    connect(m_bchNativeProof,&BchNativeProofJob::changed,this,&DexEngine::bchRefundProtectionChanged);
    connect(m_bchRefundProtection,&BchRefundProtectionJob::changed,this,&DexEngine::bchNativeProofChanged);
    connect(m_bchNativeProof,&BchNativeProofJob::changed,this,&DexEngine::bchRecoveryRetentionChanged);
    connect(m_bchRecoveryRetention,&BchRecoveryRetentionJob::changed,this,&DexEngine::bchNativeProofChanged);
    connect(m_bchNativeProof,&BchNativeProofJob::changed,this,&DexEngine::bchCancellationChanged);
    connect(m_bchCancellation,&BchCancellationJob::changed,this,&DexEngine::bchNativeProofChanged);
    connect(m_bchNativeProof,&BchNativeProofJob::changed,this,&DexEngine::bchRefundReleaseChanged);
    connect(m_bchRefundRelease,&BchRefundReleaseJob::changed,this,&DexEngine::bchNativeProofChanged);
    connect(m_bchNativeProof,&BchNativeProofJob::changed,this,&DexEngine::bchAdmissionChanged);
    connect(m_bchAdmission,&BchAdmissionJob::changed,this,&DexEngine::bchNativeProofChanged);
    connect(m_bchNativeProof,&BchNativeProofJob::changed,this,&DexEngine::bchPreparationChanged);
    connect(m_bchPreparation,&BchPreparationJob::changed,this,&DexEngine::bchNativeProofChanged);
    connect(m_bchNativeProof,&BchNativeProofJob::changed,this,&DexEngine::bchFundingChanged);
    connect(m_bchFunding,&BchFundingJob::changed,this,&DexEngine::bchNativeProofChanged);
    connect(m_bchFundingReceipt,&BchFundingReceiptJob::changed,this,&DexEngine::bchNativeProofChanged);
    connect(m_bchNativeProof,&BchNativeProofJob::changed,this,&DexEngine::bchFundingReceiptChanged);
    connect(m_bchNativeProof,&BchNativeProofJob::changed,this,&DexEngine::bchNativePreparationChanged);
    connect(m_bchNativePreparation,&BchNativePreparationJob::changed,this,&DexEngine::bchNativeProofChanged);
    connect(m_bchNativeProof,&BchNativeProofJob::changed,this,&DexEngine::bchNativeRelayChanged);
    connect(m_bchNativeRelay,&BchNativeRelayJob::changed,this,&DexEngine::bchNativeProofChanged);
    connect(this,&DexEngine::bchNativeObservationChanged,this,&DexEngine::advanceBchStartupChecks,Qt::QueuedConnection);
    connect(m_bchNativeObservation,&BchNativeObservationJob::changed,this,&DexEngine::bchNativeObservationChanged);
    connect(m_evmTrade,&EvmTradeJob::changed,this,&DexEngine::evmTradeChanged);
    m_evmTicker->setInterval(20000);
    connect(m_evmTicker,&QTimer::timeout,this,&DexEngine::stepEvmTrade);
    m_marketTicker=new QTimer(this);
    m_marketTicker->setInterval(20000);
    connect(m_marketTicker,&QTimer::timeout,this,&DexEngine::stepEvmMarket);
    m_btcMarket=new BtcMarketJob(this);m_btcTicker=new QTimer(this);
    m_dexWallets=new DexWalletService(this);
    connect(m_dexWallets,&DexWalletService::changed,this,&DexEngine::dexWalletChanged);
    connect(m_dexWallets,&DexWalletService::changed,this,&DexEngine::setUpEvmMarkets);
    connect(m_dexWallets,&DexWalletService::changed,this,&DexEngine::setUpUtxoMarket);
    connect(m_btcMarket,&BtcMarketJob::changed,this,&DexEngine::btcMarketChanged);
    connect(m_btcMarket,&BtcMarketJob::agreed,this,[this](const QString &app){
        m_btcMessage=tr("Trade agreed through the relay. Review it below and start it: %1").arg(app);emit btcMarketChanged();
    });
    m_btcTicker->setInterval(15000);
    connect(m_btcTicker,&QTimer::timeout,this,[this]{m_btcMarket->step();});
    connect(m_bchNativeObservation,&BchNativeObservationJob::changed,this,&DexEngine::bchRefundProtectionChanged);
    connect(m_bchRefundProtection,&BchRefundProtectionJob::changed,this,&DexEngine::bchNativeObservationChanged);
    connect(m_bchNativeObservation,&BchNativeObservationJob::changed,this,&DexEngine::bchRecoveryRetentionChanged);
    connect(m_bchRecoveryRetention,&BchRecoveryRetentionJob::changed,this,&DexEngine::bchNativeObservationChanged);
    connect(m_bchNativeObservation,&BchNativeObservationJob::changed,this,&DexEngine::bchCancellationChanged);
    connect(m_bchCancellation,&BchCancellationJob::changed,this,&DexEngine::bchNativeObservationChanged);
    connect(m_bchNativeObservation,&BchNativeObservationJob::changed,this,&DexEngine::bchRefundReleaseChanged);
    connect(m_bchRefundRelease,&BchRefundReleaseJob::changed,this,&DexEngine::bchNativeObservationChanged);
    connect(m_bchNativeObservation,&BchNativeObservationJob::changed,this,&DexEngine::bchAdmissionChanged);
    connect(m_bchAdmission,&BchAdmissionJob::changed,this,&DexEngine::bchNativeObservationChanged);
    connect(m_bchNativeObservation,&BchNativeObservationJob::changed,this,&DexEngine::bchPreparationChanged);
    connect(m_bchPreparation,&BchPreparationJob::changed,this,&DexEngine::bchNativeObservationChanged);
    connect(m_bchNativeObservation,&BchNativeObservationJob::changed,this,&DexEngine::bchFundingChanged);
    connect(m_bchFunding,&BchFundingJob::changed,this,&DexEngine::bchNativeObservationChanged);
    connect(m_bchFundingReceipt,&BchFundingReceiptJob::changed,this,&DexEngine::bchNativeObservationChanged);
    connect(m_bchNativeObservation,&BchNativeObservationJob::changed,this,&DexEngine::bchFundingReceiptChanged);
    connect(m_bchNativeObservation,&BchNativeObservationJob::changed,this,&DexEngine::bchNativePreparationChanged);
    connect(m_bchNativePreparation,&BchNativePreparationJob::changed,this,&DexEngine::bchNativeObservationChanged);
    connect(m_bchNativeObservation,&BchNativeObservationJob::changed,this,&DexEngine::bchNativeRelayChanged);
    connect(m_bchNativeRelay,&BchNativeRelayJob::changed,this,&DexEngine::bchNativeObservationChanged);
    connect(m_bchNativeProof,&BchNativeProofJob::changed,m_bchNativeObservation,&BchNativeObservationJob::invalidate);
    connect(m_bchNativeProof,&BchNativeProofJob::changed,this,&DexEngine::bchNativeObservationChanged);
    connect(m_bchNativeObservation,&BchNativeObservationJob::changed,this,&DexEngine::bchNativeProofChanged);
    connect(this,&DexEngine::bchSuccessReleaseReviewChanged,this,&DexEngine::advanceBchStartupChecks,Qt::QueuedConnection);
    connect(m_bchSuccessReleaseReview,&BchSuccessReleaseReviewJob::changed,this,&DexEngine::bchSuccessReleaseReviewChanged);
    connect(m_bchSuccessReleaseReview,&BchSuccessReleaseReviewJob::changed,this,&DexEngine::bchRefundProtectionChanged);
    connect(m_bchRefundProtection,&BchRefundProtectionJob::changed,this,&DexEngine::bchSuccessReleaseReviewChanged);
    connect(m_bchSuccessReleaseReview,&BchSuccessReleaseReviewJob::changed,this,&DexEngine::bchRecoveryRetentionChanged);
    connect(m_bchRecoveryRetention,&BchRecoveryRetentionJob::changed,this,&DexEngine::bchSuccessReleaseReviewChanged);
    connect(m_bchSuccessReleaseReview,&BchSuccessReleaseReviewJob::changed,this,&DexEngine::bchCancellationChanged);
    connect(m_bchCancellation,&BchCancellationJob::changed,this,&DexEngine::bchSuccessReleaseReviewChanged);
    connect(m_bchSuccessReleaseReview,&BchSuccessReleaseReviewJob::changed,this,&DexEngine::bchRefundReleaseChanged);
    connect(m_bchRefundRelease,&BchRefundReleaseJob::changed,this,&DexEngine::bchSuccessReleaseReviewChanged);
    connect(m_bchSuccessReleaseReview,&BchSuccessReleaseReviewJob::changed,this,&DexEngine::bchAdmissionChanged);
    connect(m_bchAdmission,&BchAdmissionJob::changed,this,&DexEngine::bchSuccessReleaseReviewChanged);
    connect(m_bchSuccessReleaseReview,&BchSuccessReleaseReviewJob::changed,this,&DexEngine::bchPreparationChanged);
    connect(m_bchPreparation,&BchPreparationJob::changed,this,&DexEngine::bchSuccessReleaseReviewChanged);
    connect(m_bchSuccessReleaseReview,&BchSuccessReleaseReviewJob::changed,this,&DexEngine::bchFundingChanged);
    connect(m_bchFunding,&BchFundingJob::changed,this,&DexEngine::bchSuccessReleaseReviewChanged);
    connect(m_bchFundingReceipt,&BchFundingReceiptJob::changed,this,&DexEngine::bchSuccessReleaseReviewChanged);
    connect(m_bchSuccessReleaseReview,&BchSuccessReleaseReviewJob::changed,this,&DexEngine::bchFundingReceiptChanged);
    connect(m_bchSuccessReleaseReview,&BchSuccessReleaseReviewJob::changed,this,&DexEngine::bchNativePreparationChanged);
    connect(m_bchNativePreparation,&BchNativePreparationJob::changed,this,&DexEngine::bchSuccessReleaseReviewChanged);
    connect(m_bchSuccessReleaseReview,&BchSuccessReleaseReviewJob::changed,this,&DexEngine::bchNativeRelayChanged);
    connect(m_bchNativeRelay,&BchNativeRelayJob::changed,this,&DexEngine::bchSuccessReleaseReviewChanged);
    connect(m_bchNativeProof,&BchNativeProofJob::changed,m_bchSuccessReleaseReview,&BchSuccessReleaseReviewJob::invalidate);
    connect(m_bchNativeProof,&BchNativeProofJob::changed,this,&DexEngine::bchSuccessReleaseReviewChanged);
    connect(m_bchSuccessReleaseReview,&BchSuccessReleaseReviewJob::changed,this,&DexEngine::bchNativeProofChanged);
    connect(m_bchSuccessReleaseReview,&BchSuccessReleaseReviewJob::changed,this,&DexEngine::bchNativeObservationChanged);
    connect(m_bchNativeObservation,&BchNativeObservationJob::changed,this,&DexEngine::bchSuccessReleaseReviewChanged);
    connect(m_bchPunishment,&BchPunishmentJob::changed,this,&DexEngine::bchPunishmentChanged);
    connect(m_bchPunishment,&BchPunishmentJob::changed,this,&DexEngine::advanceBchStartupChecks,Qt::QueuedConnection);
    const auto watchPunishmentAvailability=[this](auto *job,auto signal) {
        using Job=std::remove_pointer_t<decltype(job)>;
        connect(job,&Job::changed,this,&DexEngine::bchPunishmentChanged);
        connect(m_bchPunishment,&BchPunishmentJob::changed,this,signal);
    };
    watchPunishmentAvailability(m_bchFundingReceipt,&DexEngine::bchFundingReceiptChanged);
    watchPunishmentAvailability(m_bchNativePreparation,&DexEngine::bchNativePreparationChanged);
    watchPunishmentAvailability(m_bchNativeRelay,&DexEngine::bchNativeRelayChanged);
    watchPunishmentAvailability(m_bchNativeProof,&DexEngine::bchNativeProofChanged);
    watchPunishmentAvailability(m_bchNativeObservation,&DexEngine::bchNativeObservationChanged);
    watchPunishmentAvailability(m_bchSuccessReleaseReview,&DexEngine::bchSuccessReleaseReviewChanged);
    watchPunishmentAvailability(m_bchSuccessSignedReleaseReview,&DexEngine::bchSuccessSignedReleaseReviewChanged);
    watchPunishmentAvailability(m_bchSuccessSignatureReview,&DexEngine::bchSuccessSignatureReviewChanged);
    watchPunishmentAvailability(m_bchSuccessPreparation,&DexEngine::bchSuccessPreparationChanged);
    watchPunishmentAvailability(m_bchPublicationRepair,&DexEngine::bchPublicationRepairChanged);
    watchPunishmentAvailability(m_bchRefundProtection,&DexEngine::bchRefundProtectionChanged);
    watchPunishmentAvailability(m_bchRecoveryRetention,&DexEngine::bchRecoveryRetentionChanged);
    watchPunishmentAvailability(m_bchCancellation,&DexEngine::bchCancellationChanged);
    watchPunishmentAvailability(m_bchRefundRelease,&DexEngine::bchRefundReleaseChanged);
    watchPunishmentAvailability(m_bchAdmission,&DexEngine::bchAdmissionChanged);
    watchPunishmentAvailability(m_bchLightFunding,&DexEngine::bchLightFundingChanged);
    watchPunishmentAvailability(m_bchPreparation,&DexEngine::bchPreparationChanged);
    watchPunishmentAvailability(m_bchFunding,&DexEngine::bchFundingChanged);
    connect(m_bchRefundWallet,&BchRefundWalletJob::changed,this,&DexEngine::bchRefundWalletChanged);
    connect(m_bchRefundWallet,&BchRefundWalletJob::changed,this,&DexEngine::advanceBchStartupChecks,Qt::QueuedConnection);
    const auto watchRefundWalletAvailability=[this](auto *job,auto signal) {
        using Job=std::remove_pointer_t<decltype(job)>;
        connect(job,&Job::changed,this,&DexEngine::bchRefundWalletChanged);
        connect(m_bchRefundWallet,&BchRefundWalletJob::changed,this,signal);
    };
    watchRefundWalletAvailability(m_bchFundingReceipt,&DexEngine::bchFundingReceiptChanged);
    watchRefundWalletAvailability(m_bchNativePreparation,&DexEngine::bchNativePreparationChanged);
    watchRefundWalletAvailability(m_bchNativeRelay,&DexEngine::bchNativeRelayChanged);
    watchRefundWalletAvailability(m_bchNativeProof,&DexEngine::bchNativeProofChanged);
    watchRefundWalletAvailability(m_bchNativeObservation,&DexEngine::bchNativeObservationChanged);
    watchRefundWalletAvailability(m_bchSuccessReleaseReview,&DexEngine::bchSuccessReleaseReviewChanged);
    watchRefundWalletAvailability(m_bchSuccessSignedReleaseReview,&DexEngine::bchSuccessSignedReleaseReviewChanged);
    watchRefundWalletAvailability(m_bchSuccessSignatureReview,&DexEngine::bchSuccessSignatureReviewChanged);
    watchRefundWalletAvailability(m_bchSuccessPreparation,&DexEngine::bchSuccessPreparationChanged);
    watchRefundWalletAvailability(m_bchPublicationRepair,&DexEngine::bchPublicationRepairChanged);
    watchRefundWalletAvailability(m_bchRefundProtection,&DexEngine::bchRefundProtectionChanged);
    watchRefundWalletAvailability(m_bchRecoveryRetention,&DexEngine::bchRecoveryRetentionChanged);
    watchRefundWalletAvailability(m_bchCancellation,&DexEngine::bchCancellationChanged);
    watchRefundWalletAvailability(m_bchRefundRelease,&DexEngine::bchRefundReleaseChanged);
    watchRefundWalletAvailability(m_bchAdmission,&DexEngine::bchAdmissionChanged);
    watchRefundWalletAvailability(m_bchLightFunding,&DexEngine::bchLightFundingChanged);
    watchRefundWalletAvailability(m_bchPreparation,&DexEngine::bchPreparationChanged);
    watchRefundWalletAvailability(m_bchFunding,&DexEngine::bchFundingChanged);
    watchRefundWalletAvailability(m_bchPunishment,&DexEngine::bchPunishmentChanged);
    // Repair notifications change availability only. Real worker changes clear
    // repair consent, without feeding engine property notifications back to jobs.
    connect(m_bchPublicationRepair,&BchPublicationRepairJob::changed,this,&DexEngine::bchPublicationRepairChanged);
    connect(m_bchPublicationRepair,&BchPublicationRepairJob::changed,this,&DexEngine::bchLightFundingChanged);
    connect(m_bchPublicationRepair,&BchPublicationRepairJob::changed,this,&DexEngine::advanceBchStartupChecks,Qt::QueuedConnection);
    for (auto signal:{&DexEngine::bchRefundProtectionChanged,&DexEngine::bchRecoveryRetentionChanged,
        &DexEngine::bchCancellationChanged,&DexEngine::bchRefundReleaseChanged,&DexEngine::bchAdmissionChanged,
        &DexEngine::bchPreparationChanged,&DexEngine::bchFundingChanged,&DexEngine::bchFundingReceiptChanged,
        &DexEngine::bchNativePreparationChanged,&DexEngine::bchNativeRelayChanged,&DexEngine::bchNativeProofChanged,
        &DexEngine::bchNativeObservationChanged,&DexEngine::bchSuccessPreparationChanged,&DexEngine::bchSuccessReleaseReviewChanged})
        connect(m_bchPublicationRepair,&BchPublicationRepairJob::changed,this,signal);
    const auto watchRepairDependency=[this](auto *job) {
        using Job=std::remove_pointer_t<decltype(job)>;
        connect(job,&Job::changed,this,[this]{
            m_bchPublicationRepair->invalidate();
            // Availability can change even when consent was already empty.
            emit bchPublicationRepairChanged();
        });
    };
    watchRepairDependency(m_bchAdmission);watchRepairDependency(m_bchPreparation);watchRepairDependency(m_bchFunding);
    watchRepairDependency(m_bchRefundProtection);watchRepairDependency(m_bchRecoveryRetention);
    watchRepairDependency(m_bchPunishment);
    watchRepairDependency(m_bchRefundWallet);
    watchRepairDependency(m_bchCancellation);watchRepairDependency(m_bchRefundRelease);watchRepairDependency(m_bchFundingReceipt);
    watchRepairDependency(m_bchNativePreparation);watchRepairDependency(m_bchNativeRelay);watchRepairDependency(m_bchNativeProof);
    watchRepairDependency(m_bchNativeObservation);watchRepairDependency(m_bchSuccessPreparation);
    watchRepairDependency(m_bchSuccessReleaseReview);watchRepairDependency(m_bchSuccessSignatureReview);
    watchRepairDependency(m_bchSuccessSignedReleaseReview);watchRepairDependency(m_bchLightFunding);
    // Availability notifications never invalidate state; only actual dependency
    // worker changes do. This avoids feedback through engine property signals.
    for (auto *job:{m_bchSuccessSignatureReview,m_bchSuccessSignedReleaseReview}) {
        connect(m_bchNativeProof,&BchNativeProofJob::changed,job,&BchSuccessReleaseReviewJob::invalidate);
        connect(m_bchSuccessPreparation,&BchSuccessPreparationJob::changed,job,&BchSuccessReleaseReviewJob::invalidate);
        for (auto signal:{&DexEngine::bchRefundProtectionChanged,&DexEngine::bchRecoveryRetentionChanged,
            &DexEngine::bchCancellationChanged,&DexEngine::bchRefundReleaseChanged,&DexEngine::bchAdmissionChanged,
            &DexEngine::bchPreparationChanged,&DexEngine::bchFundingChanged,&DexEngine::bchFundingReceiptChanged,
            &DexEngine::bchNativePreparationChanged,&DexEngine::bchNativeRelayChanged,&DexEngine::bchNativeProofChanged,
            &DexEngine::bchNativeObservationChanged,&DexEngine::bchSuccessPreparationChanged,&DexEngine::bchSuccessReleaseReviewChanged})
            connect(job,&BchSuccessReleaseReviewJob::changed,this,signal);
    }
    connect(this,&DexEngine::bchSuccessReleaseReviewChanged,this,&DexEngine::bchSuccessSignatureReviewChanged);
    connect(this,&DexEngine::bchSuccessReleaseReviewChanged,this,&DexEngine::bchSuccessSignedReleaseReviewChanged);
    connect(this,&DexEngine::bchSuccessPreparationChanged,this,&DexEngine::advanceBchStartupChecks,Qt::QueuedConnection);
    connect(m_bchSuccessPreparation,&BchSuccessPreparationJob::changed,this,&DexEngine::bchSuccessPreparationChanged);
    connect(m_bchSuccessPreparation,&BchSuccessPreparationJob::changed,this,&DexEngine::bchRefundProtectionChanged);
    connect(m_bchRefundProtection,&BchRefundProtectionJob::changed,this,&DexEngine::bchSuccessPreparationChanged);
    connect(m_bchSuccessPreparation,&BchSuccessPreparationJob::changed,this,&DexEngine::bchRecoveryRetentionChanged);
    connect(m_bchRecoveryRetention,&BchRecoveryRetentionJob::changed,this,&DexEngine::bchSuccessPreparationChanged);
    connect(m_bchSuccessPreparation,&BchSuccessPreparationJob::changed,this,&DexEngine::bchCancellationChanged);
    connect(m_bchCancellation,&BchCancellationJob::changed,this,&DexEngine::bchSuccessPreparationChanged);
    connect(m_bchSuccessPreparation,&BchSuccessPreparationJob::changed,this,&DexEngine::bchRefundReleaseChanged);
    connect(m_bchRefundRelease,&BchRefundReleaseJob::changed,this,&DexEngine::bchSuccessPreparationChanged);
    connect(m_bchSuccessPreparation,&BchSuccessPreparationJob::changed,this,&DexEngine::bchAdmissionChanged);
    connect(m_bchAdmission,&BchAdmissionJob::changed,this,&DexEngine::bchSuccessPreparationChanged);
    connect(m_bchSuccessPreparation,&BchSuccessPreparationJob::changed,this,&DexEngine::bchPreparationChanged);
    connect(m_bchPreparation,&BchPreparationJob::changed,this,&DexEngine::bchSuccessPreparationChanged);
    connect(m_bchSuccessPreparation,&BchSuccessPreparationJob::changed,this,&DexEngine::bchFundingChanged);
    connect(m_bchFunding,&BchFundingJob::changed,this,&DexEngine::bchSuccessPreparationChanged);
    connect(m_bchFundingReceipt,&BchFundingReceiptJob::changed,this,&DexEngine::bchSuccessPreparationChanged);
    connect(m_bchSuccessPreparation,&BchSuccessPreparationJob::changed,this,&DexEngine::bchFundingReceiptChanged);
    connect(m_bchSuccessPreparation,&BchSuccessPreparationJob::changed,this,&DexEngine::bchNativePreparationChanged);
    connect(m_bchNativePreparation,&BchNativePreparationJob::changed,this,&DexEngine::bchSuccessPreparationChanged);
    connect(m_bchSuccessPreparation,&BchSuccessPreparationJob::changed,this,&DexEngine::bchNativeRelayChanged);
    connect(m_bchNativeRelay,&BchNativeRelayJob::changed,this,&DexEngine::bchSuccessPreparationChanged);
    connect(m_bchNativeProof,&BchNativeProofJob::changed,m_bchSuccessPreparation,&BchSuccessPreparationJob::invalidate);
    connect(m_bchNativeProof,&BchNativeProofJob::changed,this,&DexEngine::bchSuccessPreparationChanged);
    connect(m_bchSuccessPreparation,&BchSuccessPreparationJob::changed,this,&DexEngine::bchNativeProofChanged);
    connect(m_bchSuccessPreparation,&BchSuccessPreparationJob::changed,this,&DexEngine::bchNativeObservationChanged);
    connect(m_bchNativeObservation,&BchNativeObservationJob::changed,this,&DexEngine::bchSuccessPreparationChanged);
    connect(m_bchSuccessReleaseReview,&BchSuccessReleaseReviewJob::changed,m_bchSuccessPreparation,&BchSuccessPreparationJob::invalidate);
    connect(m_bchSuccessReleaseReview,&BchSuccessReleaseReviewJob::changed,this,&DexEngine::bchSuccessPreparationChanged);
    connect(m_bchSuccessPreparation,&BchSuccessPreparationJob::changed,this,&DexEngine::bchSuccessReleaseReviewChanged);
    connect(this,&DexEngine::bchRefundProtectionChanged,this,&DexEngine::advanceBchStartupChecks,Qt::QueuedConnection);
    connect(this,&DexEngine::bchRecoveryRetentionChanged,this,&DexEngine::advanceBchStartupChecks,Qt::QueuedConnection);
    connect(this,&DexEngine::bchRefundReleaseChanged,this,&DexEngine::advanceBchStartupChecks,Qt::QueuedConnection);
    connect(m_bchRefundRelease,&BchRefundReleaseJob::changed,this,&DexEngine::bchRecoveryRetentionChanged);
    connect(m_bchRecoveryRetention,&BchRecoveryRetentionJob::changed,this,&DexEngine::bchRefundReleaseChanged);
    connect(m_bchRefundRelease,&BchRefundReleaseJob::changed,this,&DexEngine::bchRefundProtectionChanged);
    connect(m_bchRefundProtection,&BchRefundProtectionJob::changed,this,&DexEngine::bchRefundReleaseChanged);
    connect(m_bchRefundRelease,&BchRefundReleaseJob::changed,this,&DexEngine::bchAdmissionChanged);
    connect(m_bchAdmission,&BchAdmissionJob::changed,this,&DexEngine::bchRefundReleaseChanged);
    connect(m_bchRefundRelease,&BchRefundReleaseJob::changed,this,&DexEngine::bchPreparationChanged);
    connect(m_bchPreparation,&BchPreparationJob::changed,this,&DexEngine::bchRefundReleaseChanged);
    connect(m_bchRefundRelease,&BchRefundReleaseJob::changed,this,&DexEngine::bchFundingChanged);
    connect(m_bchFunding,&BchFundingJob::changed,this,&DexEngine::bchRefundReleaseChanged);
    connect(m_bchRefundRelease,&BchRefundReleaseJob::changed,this,&DexEngine::bchRefundReleaseChanged);
    connect(m_bchRefundRelease,&BchRefundReleaseJob::changed,this,&DexEngine::bchCancellationChanged);
    connect(m_bchCancellation,&BchCancellationJob::changed,this,&DexEngine::bchRefundReleaseChanged);
    connect(this,&DexEngine::bchCancellationChanged,this,&DexEngine::advanceBchStartupChecks,Qt::QueuedConnection);
    connect(m_bchCancellation,&BchCancellationJob::changed,this,&DexEngine::bchRecoveryRetentionChanged);
    connect(m_bchRecoveryRetention,&BchRecoveryRetentionJob::changed,this,&DexEngine::bchCancellationChanged);
    connect(m_bchCancellation,&BchCancellationJob::changed,this,&DexEngine::bchRefundProtectionChanged);
    connect(m_bchRefundProtection,&BchRefundProtectionJob::changed,this,&DexEngine::bchCancellationChanged);
    connect(m_bchCancellation,&BchCancellationJob::changed,this,&DexEngine::bchAdmissionChanged);
    connect(m_bchAdmission,&BchAdmissionJob::changed,this,&DexEngine::bchCancellationChanged);
    connect(m_bchCancellation,&BchCancellationJob::changed,this,&DexEngine::bchPreparationChanged);
    connect(m_bchPreparation,&BchPreparationJob::changed,this,&DexEngine::bchCancellationChanged);
    connect(m_bchCancellation,&BchCancellationJob::changed,this,&DexEngine::bchFundingChanged);
    connect(m_bchFunding,&BchFundingJob::changed,this,&DexEngine::bchCancellationChanged);
    connect(m_bchCancellation,&BchCancellationJob::changed,this,&DexEngine::bchCancellationChanged);
    connect(m_bchRecoveryRetention,&BchRecoveryRetentionJob::changed,this,&DexEngine::bchRefundProtectionChanged);
    connect(m_bchRefundProtection,&BchRefundProtectionJob::changed,this,&DexEngine::bchRecoveryRetentionChanged);
    connect(m_bchRecoveryRetention,&BchRecoveryRetentionJob::changed,this,&DexEngine::bchAdmissionChanged);
    connect(m_bchAdmission,&BchAdmissionJob::changed,this,&DexEngine::bchRecoveryRetentionChanged);
    connect(m_bchRecoveryRetention,&BchRecoveryRetentionJob::changed,this,&DexEngine::bchPreparationChanged);
    connect(m_bchPreparation,&BchPreparationJob::changed,this,&DexEngine::bchRecoveryRetentionChanged);
    connect(m_bchRecoveryRetention,&BchRecoveryRetentionJob::changed,this,&DexEngine::bchFundingChanged);
    connect(m_bchFunding,&BchFundingJob::changed,this,&DexEngine::bchRecoveryRetentionChanged);
    connect(m_bchRecoveryRetention,&BchRecoveryRetentionJob::changed,this,&DexEngine::bchRecoveryRetentionChanged);
    connect(m_bchRefundProtection,&BchRefundProtectionJob::changed,this,&DexEngine::bchRefundProtectionChanged);
    connect(m_bchRefundProtection,&BchRefundProtectionJob::changed,this,&DexEngine::bchAdmissionChanged);
    connect(m_bchRefundProtection,&BchRefundProtectionJob::changed,this,&DexEngine::bchPreparationChanged);
    connect(m_bchRefundProtection,&BchRefundProtectionJob::changed,this,&DexEngine::bchFundingChanged);
    connect(m_bchAdmission,&BchAdmissionJob::changed,this,&DexEngine::bchRefundProtectionChanged);
    connect(m_bchPreparation,&BchPreparationJob::changed,this,&DexEngine::bchRefundProtectionChanged);
    connect(m_bchFunding,&BchFundingJob::changed,this,&DexEngine::bchRefundProtectionChanged);
    connect(this,&DexEngine::bchPreparationChanged,this,&DexEngine::advanceBchStartupChecks,Qt::QueuedConnection);
    connect(this,&DexEngine::bchFundingChanged,this,&DexEngine::advanceBchStartupChecks,Qt::QueuedConnection);
    connect(this,&DexEngine::bchAdmissionChanged,this,&DexEngine::advanceBchStartupChecks,Qt::QueuedConnection);
    connect(m_bchPreparation,&BchPreparationJob::changed,this,&DexEngine::bchPreparationChanged);
    connect(m_bchPreparation,&BchPreparationJob::changed,this,&DexEngine::bchFundingChanged);
    connect(m_bchPreparation,&BchPreparationJob::changed,this,&DexEngine::bchAdmissionChanged);
    connect(m_bchFunding,&BchFundingJob::changed,this,&DexEngine::bchPreparationChanged);
    connect(m_bchFunding,&BchFundingJob::changed,this,&DexEngine::bchAdmissionChanged);
    connect(m_bchAdmission,&BchAdmissionJob::changed,this,&DexEngine::bchPreparationChanged);
    connect(m_bchAdmission,&BchAdmissionJob::changed,this,&DexEngine::bchFundingChanged);
    connect(m_bchFunding,&BchFundingJob::changed,this,&DexEngine::bchFundingChanged);
    connect(m_bchAdmission,&BchAdmissionJob::changed,this,&DexEngine::bchAdmissionChanged);
    connect(m_bchLightFunding,&BchLightFundingController::changed,this,&DexEngine::bchLightFundingChanged);
    connect(this,&DexEngine::bchAdmissionChanged,this,&DexEngine::bchLightFundingChanged);
    connect(this,&DexEngine::lightWalletsChanged,this,&DexEngine::bchLightFundingChanged);
    connect(m_bchLightFunding,&BchLightFundingController::changed,this,&DexEngine::bchAdmissionChanged);
    connect(m_bchLightFunding,&BchLightFundingController::changed,this,&DexEngine::bchPreparationChanged);
    connect(m_bchLightFunding,&BchLightFundingController::changed,this,&DexEngine::bchFundingChanged);
    connect(this,&DexEngine::litecoinJournalChanged,this,&DexEngine::litecoinStateChanged);
    connect(this,&DexEngine::litecoinNegotiationChanged,this,&DexEngine::litecoinStateChanged);
    connect(this,&DexEngine::litecoinReservationChanged,this,&DexEngine::litecoinStateChanged);
    connect(this,&DexEngine::litecoinRedemptionChanged,this,&DexEngine::litecoinStateChanged);
    connect(m_ltcRedemption,&LitecoinRedemptionJob::changed,this,&DexEngine::litecoinRedemptionChanged);
    connect(m_ltcReservation,&LitecoinReservationJob::changed,this,&DexEngine::litecoinReservationChanged);
    connect(m_ltcLightFunding,&LitecoinLightFundingController::changed,this,&DexEngine::litecoinStateChanged);
    connect(this,&DexEngine::lightWalletsChanged,this,&DexEngine::litecoinStateChanged);
    // The recovery exchange must follow every prepared funding: without it the funding review has
    // nothing to check. Another job may be busy at this instant, so keep the PSBT and retry.
    connect(m_ltcLightFunding,&LitecoinLightFundingController::prepared,this,[this](const QByteArray &psbt){
        m_ltcRecoveryPending=psbt;retryLitecoinRecoveryStart(60);});
    connect(this,&DexEngine::litecoinNegotiationChanged,this,&DexEngine::litecoinReservationChanged);
    connect(m_ltcReservation,&LitecoinReservationJob::prepared,this,[this](const QByteArray &psbt){startLitecoinRecovery(psbt);});
    connect(m_ltcJournal,&LitecoinJournalJob::changed,this,&DexEngine::litecoinJournalChanged);
    connect(m_feePreviewController, &NativeFeePreviewController::previewChanged,
        this, &DexEngine::nativeFeePreviewChanged);
    connect(m_researchWatcher, &QFutureWatcher<QVariantMap>::finished, this, [this] {
        const auto snapshot = m_researchWatcher->result();
        QPointer<DexEngine> guard(this);
        emit researchMarketReady(snapshot);
        if (!guard) return;
        m_researchBusy = false;
        emit researchMarketBusyChanged();
    });
}

QVariantMap DexEngine::litecoinNegotiationStatus() const {
    return litecoinAppView(m_ltcNegotiation?m_ltcNegotiation->status():QVariantMap{{"attached",false},{"state","unattached"},{"executable",false}});
}
bool DexEngine::startLitecoinRedemption(const QString &txid) {
    if (!litecoinOperationAllowed() || !m_ltcNegotiation) return false;
    // The XMZ seller shares the funding proof only once this app has seen the funding confirmed:
    // sent earlier, the proof cannot be made and the session with the peer fails.
    if (txid.isEmpty() && m_ltcNegotiation->status().value("role")=="native-funder") {
        const auto funding=litecoinFundingStatus();
        if (funding.value("fundingSubmissionConfigured").toBool() && funding.value("fundingCheckState")!="confirmed") return false;
    }
    return m_ltcNegotiation->startRedemption(txid);
}
bool DexEngine::configureLightWalletSetup(const QString &helper,const QString &digest) {
    if (!m_lightSetupHelper.isEmpty() || !BackendExecutable::matches(helper,digest)) return false;
    m_lightSetupHelper=helper;m_lightSetupDigest=digest;
    QSettings settings(QSettings::IniFormat,QSettings::UserScope,"Monzero","LightWalletFolders");
    settings.sync();
    for (auto it=m_lightWallets.cbegin();it!=m_lightWallets.cend();++it) {
        if (it.value()->status().value("configured").toBool()) continue;
        const auto key="wallets/"+it.key();
        if (settings.status()!=QSettings::NoError) {
            m_lightSetupNotices.insert(it.key(),tr("Saved wallet folders could not be read. Open the wallet folder manually."));continue;
        }
        if (!settings.contains(key)) continue;
        const auto stored=settings.value(key);
        const auto directory=stored.type()==QVariant::String?stored.toString():QString{};
        const auto launcher=directory.size()<=2048?LightWalletHelperUpgrade::selected(directory,m_lightSetupHelper,m_lightSetupDigest):QString{};
        if (launcher.isEmpty() || !it.value()->configure(launcher,helper,digest))
            m_lightSetupNotices.insert(it.key(),tr("The remembered wallet could not be verified. Open its saved folder manually; no replacement wallet was created."));
    }
    emit lightWalletsChanged();return true;
}
void DexEngine::rememberLightWallet(const QString &coin,const QString &directory) {
    QSettings settings(QSettings::IniFormat,QSettings::UserScope,"Monzero","LightWalletFolders");
    settings.setValue("wallets/"+coin,directory);settings.sync();
    if (settings.status()!=QSettings::NoError || settings.value("wallets/"+coin).toString()!=directory)
        m_lightSetupNotices.insert(coin,tr("The wallet is open, but its folder could not be remembered. Keep the folder path for reopening."));
    else m_lightSetupNotices.remove(coin);
    emit lightWalletsChanged();
}
bool DexEngine::openLocalLightWallet(const QString &coin,const QString &directory) {
    auto *job=m_lightWallets.value(coin,nullptr);
    if (!job || job->status().value("configured").toBool() || !lightWalletSetupAvailable()) return false;
    const auto launcher=LightWalletHelperUpgrade::selected(directory,m_lightSetupHelper,m_lightSetupDigest);
    if (launcher.isEmpty() || !job->configure(launcher,m_lightSetupHelper,m_lightSetupDigest)) return false;
    rememberLightWallet(coin,QFileInfo(launcher).absolutePath());return true;
}
QVariantMap DexEngine::reviewLightWalletHelperUpgrade(const QString &coin,const QString &directory) const {
    auto *job=m_lightWallets.value(coin,nullptr);
    if (!job || job->status().value("configured").toBool() || !lightWalletSetupAvailable()) return {};
    return LightWalletHelperUpgrade::review(coin,directory,m_lightSetupHelper,m_lightSetupDigest).toVariantMap();
}
bool DexEngine::upgradeLightWalletHelper(const QString &coin,const QString &directory,const QString &commitment) {
    auto *job=m_lightWallets.value(coin,nullptr);
    if (!job || job->status().value("configured").toBool() || !lightWalletSetupAvailable()) return false;
    const auto launcher=LightWalletHelperUpgrade::apply(coin,directory,m_lightSetupHelper,m_lightSetupDigest,commitment);
    if (launcher.isEmpty() || !job->configure(launcher,m_lightSetupHelper,m_lightSetupDigest)) return false;
    rememberLightWallet(coin,directory);return true;
}
bool DexEngine::prepareLocalBackupRestore(const QString &coin,const QString &network,const QString &directory,const QString &backup,const QString &host,int port) {
    auto *job=m_lightWallets.value(coin,nullptr);
    if (!job || job->status().value("configured").toBool() || !lightWalletSetupAvailable() || backup.isEmpty())return false;
    const auto launcher=LightWalletSetup::prepare(coin,network,directory,m_lightSetupHelper,m_lightSetupDigest,host,port,0,backup);
    if(launcher.isEmpty() || !job->configure(launcher,m_lightSetupHelper,m_lightSetupDigest))return false;
    rememberLightWallet(coin,QFileInfo(launcher).absolutePath());return true;
}
bool DexEngine::previewLightBackupImport(const QString &coin,const QString &password) {
    auto *job=m_lightWallets.value(coin,nullptr);return job && job->previewBackupImport(password);
}
bool DexEngine::importLightBackup(const QString &coin,const QString &digest,const QString &password,bool approved) {
    auto *job=m_lightWallets.value(coin,nullptr);return job && job->importBackup(digest,password,approved);
}
bool DexEngine::prepareLocalLightWallet(const QString &coin,const QString &network,const QString &directory,const QString &host,int port,int accountIndex) {
    auto *job=m_lightWallets.value(coin,nullptr);
    if (!job || job->status().value("configured").toBool() || !lightWalletSetupAvailable()) return false;
    const auto launcher=LightWalletSetup::prepare(coin,network,directory,m_lightSetupHelper,m_lightSetupDigest,host,port,accountIndex);
    if (launcher.isEmpty() || !job->configure(launcher,m_lightSetupHelper,m_lightSetupDigest)) return false;
    rememberLightWallet(coin,QFileInfo(launcher).absolutePath());return true;
}
bool DexEngine::configureLightWallet(const QString &coin,const QString &path) {
    auto *job=m_lightWallets.value(coin,nullptr);return job && job->configure(path);
}
QVariantMap DexEngine::lightWallets() const {
    QVariantMap result;for (auto it=m_lightWallets.cbegin();it!=m_lightWallets.cend();++it) result.insert(it.key(),it.value()->status());return result;
}
bool DexEngine::prepareLightPayment(const QString &coin,const QVariantMap &payment) {
    auto *job=m_lightWallets.value(coin,nullptr);return job && job->prepareCurrentWallet(QJsonObject::fromVariantMap(payment),20,500,1000);
}
bool DexEngine::reopenLightPayment(const QString &coin,const QString &requestId,const QVariantMap &payment,const QVariantMap &review) {
    auto *job=m_lightWallets.value(coin,nullptr);return job && job->reopenWallet(requestId,QJsonObject::fromVariantMap(payment),QJsonObject::fromVariantMap(review),20,500,1000);
}
bool DexEngine::signLightPayment(const QString &coin,const QString &commitment,const QString &password) {
    auto *job=m_lightWallets.value(coin,nullptr);return job && job->signReviewedWallet(commitment,password);
}
bool DexEngine::checkLightPaymentSend(const QString &coin,const QString &requestId,const QVariantMap &payment,const QVariantMap &review) {
    auto *job=m_lightWallets.value(coin,nullptr);return job && job->checkBroadcastWallet(requestId,QJsonObject::fromVariantMap(payment),QJsonObject::fromVariantMap(review),20,500,1000);
}
bool DexEngine::sendLightPayment(const QString &coin,const QString &commitment,const QString &txid,const QVariantMap &endpoint) {
    auto *job=m_lightWallets.value(coin,nullptr);return job && job->broadcastReviewedWallet(commitment,txid,QJsonObject::fromVariantMap(endpoint));
}
bool DexEngine::reconcileLightPayment(const QString &coin,const QString &requestId,const QVariantMap &payment,const QVariantMap &review) {
    auto *job=m_lightWallets.value(coin,nullptr);return job && job->reconcileWallet(requestId,QJsonObject::fromVariantMap(payment),QJsonObject::fromVariantMap(review),10000,1000);
}
bool DexEngine::loadLightPaymentRequests(const QString &coin) {auto *job=m_lightWallets.value(coin,nullptr);return job && job->loadPaymentRequests();}
bool DexEngine::activateLightWalletImport(const QString &coin,const QString &digest,const QString &password,bool approved) {
    auto *job=m_lightWallets.value(coin,nullptr);return job && job->activateImportedWallet(digest,password,approved);
}
bool DexEngine::recoverLightWalletImport(const QString &coin,const QString &digest,const QString &password,bool approved) {
    auto *job=m_lightWallets.value(coin,nullptr);return job && job->recoverImportedWallet(digest,password,approved);
}
bool DexEngine::discoverLightWalletImport(const QString &coin,int receiveMinimum,int changeMinimum,bool approved) {
    auto *job=m_lightWallets.value(coin,nullptr);
    return job && receiveMinimum>=0 && changeMinimum>=0 && job->discoverImportedWallet(receiveMinimum,changeMinimum,20,500,1000,10000,approved);
}
bool DexEngine::previewLightWalletImport(const QString &coin,const QString &digest,const QString &phrase,const QString &passphrase) {
    auto *job=m_lightWallets.value(coin,nullptr);return job && job->previewMnemonicImport(digest,phrase,passphrase);
}
bool DexEngine::importLightWalletMnemonic(const QString &coin,const QString &digest,const QString &phrase,const QString &passphrase,const QString &password,const QString &confirmation,bool approved) {
    auto *job=m_lightWallets.value(coin,nullptr);return job && job->importMnemonic(digest,phrase,passphrase,password,confirmation,approved);
}
bool DexEngine::restoreLightWalletMnemonic(const QString &coin,const QString &digest,const QString &phrase,const QString &passphrase,const QString &password,const QString &confirmation,bool approved) {
    auto *job=m_lightWallets.value(coin,nullptr);return job && job->restoreMnemonicRecovery(digest,phrase,passphrase,password,confirmation,approved);
}
bool DexEngine::restoreLightWalletRecovery(const QString &coin,const QString &digest,const QString &seed,const QString &password,const QString &confirmation,bool approved) {
    auto *job=m_lightWallets.value(coin,nullptr);return job && job->restoreRecovery(digest,seed,password,confirmation,approved);
}
bool DexEngine::revealLightWalletRecovery(const QString &coin,const QString &digest,const QString &password,bool approved) {
    auto *job=m_lightWallets.value(coin,nullptr);return job && job->revealRecovery(digest,password,approved);
}
QString DexEngine::lightWalletRecoveryPassphrase(const QString &coin) const {auto *job=m_lightWallets.value(coin,nullptr);return job?job->recoveryPassphrase():QString{};}
QString DexEngine::lightWalletRecoverySeed(const QString &coin) const {auto *job=m_lightWallets.value(coin,nullptr);return job?job->recoverySeed():QString{};}
void DexEngine::hideLightWalletRecovery(const QString &coin) {auto *job=m_lightWallets.value(coin,nullptr);if (job) job->hideRecovery();}
bool DexEngine::createLightWallet(const QString &coin,const QString &digest,const QString &password,const QString &confirmation) {
    auto *job=m_lightWallets.value(coin,nullptr);return job && job->createWallet(digest,password,confirmation);
}
bool DexEngine::reviewCreatedLightWallet(const QString &coin) {auto *job=m_lightWallets.value(coin,nullptr);return job && job->reviewCreatedWallet();}
bool DexEngine::openCreatedLightWallet(const QString &coin,const QString &digest) {auto *job=m_lightWallets.value(coin,nullptr);return job && job->openCreatedWallet(digest);}
bool DexEngine::receiveLightWallet(const QString &coin) {auto *job=m_lightWallets.value(coin,nullptr);return job && job->receiveCurrentWallet();}
bool DexEngine::backupLightWallet(const QString &coin,const QString &directory,const QString &password,bool save) {
    auto *job=m_lightWallets.value(coin,nullptr);return job && job->backupWallet(directory,password,save);
}
bool DexEngine::restoreLightWallet(const QString &coin,const QString &directory,const QString &digest,const QString &password) {
    auto *job=m_lightWallets.value(coin,nullptr);return job && job->restoreWallet(directory,digest,password);
}
bool DexEngine::synchronizeLightWallet(const QString &coin,quint32 maximumHeaders) {auto *job=m_lightWallets.value(coin,nullptr);return job && job->synchronize(maximumHeaders);}
bool DexEngine::refreshLightWallet(const QString &coin) {auto *job=m_lightWallets.value(coin,nullptr);return job && job->refreshWallet(20,500,1000);}
void DexEngine::cancelLightWallet(const QString &coin) {auto *job=m_lightWallets.value(coin,nullptr);if (job) job->cancel();}
QString DexEngine::swapCoin() const {return DexDeployment::litecoinLightCoin().toUpper();}
QString DexEngine::swapCoinName() const {return DexDeployment::swapCoinName();}
LitecoinLightJob *DexEngine::swapLight() const {return m_lightWallets.value(DexDeployment::litecoinLightCoin(),m_ltcLight);}
bool DexEngine::configureLitecoinLight(const QString &path) {return m_ltcLight->configure(path);}
QVariantMap DexEngine::litecoinLightStatus() const {return m_ltcLight->status();}
bool DexEngine::requestLitecoinLightStatus() {return m_ltcLight->refresh();}
bool DexEngine::synchronizeLitecoinLight(quint32 maximumDownloadHeaders) {return m_ltcLight->synchronize(maximumDownloadHeaders);}
bool DexEngine::showLitecoinReceivingAddress() {return m_ltcLight->receiveCurrentWallet();}
bool DexEngine::receiveLitecoinLightWallet(const QString &requestId) {return m_ltcLight->receiveWallet(requestId);}
bool DexEngine::prepareLitecoinLightPayment(const QString &requestId,const QVariantMap &payment,quint32 gap,quint32 addresses,quint32 entries) {return m_ltcLight->prepareWallet(requestId,QJsonObject::fromVariantMap(payment),gap,addresses,entries);}
bool DexEngine::recoverSignedLitecoinPayment(const QString &requestId,const QVariantMap &payment,const QVariantMap &savedReview) {return m_ltcLight->recoverSignedWallet(requestId,QJsonObject::fromVariantMap(payment),QJsonObject::fromVariantMap(savedReview));}
bool DexEngine::signReviewedLitecoinPayment(const QString &commitment,const QString &password) {return m_ltcLight->signReviewedWallet(commitment,password);}
bool DexEngine::checkLitecoinPaymentSendStatus(const QString &requestId,const QVariantMap &payment,const QVariantMap &savedReview,quint32 gap,quint32 addresses,quint32 entries) {return m_ltcLight->checkBroadcastWallet(requestId,QJsonObject::fromVariantMap(payment),QJsonObject::fromVariantMap(savedReview),gap,addresses,entries);}
bool DexEngine::sendReviewedLitecoinPayment(const QString &commitment,const QString &txid,const QVariantMap &endpoint) {return m_ltcLight->broadcastReviewedWallet(commitment,txid,QJsonObject::fromVariantMap(endpoint));}
bool DexEngine::createLitecoinWallet(const QString &digest,const QString &password,const QString &confirmation) {return m_ltcLight->createWallet(digest,password,confirmation);}
bool DexEngine::reviewCreatedLitecoinWallet() {return m_ltcLight->reviewCreatedWallet();}
bool DexEngine::openCreatedLitecoinWallet(const QString &digest) {return m_ltcLight->openCreatedWallet(digest);}
bool DexEngine::restoreLitecoinWallet(const QString &directory,const QString &digest,const QString &password) {return m_ltcLight->restoreWallet(directory,digest,password);}
bool DexEngine::backupLitecoinWallet(const QString &directory,const QString &password,bool save) {return m_ltcLight->backupWallet(directory,password,save);}
bool DexEngine::reconcileLitecoinPayment(const QString &requestId,const QVariantMap &payment,const QVariantMap &savedReview,quint32 maximumHeaders,quint32 maximumHistory) {return m_ltcLight->reconcileWallet(requestId,QJsonObject::fromVariantMap(payment),QJsonObject::fromVariantMap(savedReview),maximumHeaders,maximumHistory);}
bool DexEngine::reopenLitecoinPayment(const QString &requestId,const QVariantMap &payment,const QVariantMap &review,quint32 gap,quint32 addresses,quint32 entries) {return m_ltcLight->reopenWallet(requestId,QJsonObject::fromVariantMap(payment),QJsonObject::fromVariantMap(review),gap,addresses,entries);}
bool DexEngine::estimateLitecoinPaymentFee(const QVariantMap &quote,quint32 gap,quint32 addresses,quint32 entries) {return m_ltcLight->estimateWallet(QJsonObject::fromVariantMap(quote),gap,addresses,entries);}
bool DexEngine::prepareCurrentLitecoinPayment(const QVariantMap &payment,quint32 gap,quint32 addresses,quint32 entries) {return m_ltcLight->prepareCurrentWallet(QJsonObject::fromVariantMap(payment),gap,addresses,entries);}
bool DexEngine::loadLitecoinPaymentRequests() {return m_ltcLight->loadPaymentRequests();}
bool DexEngine::initializeLitecoinLightWallet() {return m_ltcLight->initializeWallet();}
bool DexEngine::refreshLitecoinLightWallet(quint32 gap,quint32 addresses,quint32 entries) {return m_ltcLight->refreshWallet(gap,addresses,entries);}
void DexEngine::cancelLitecoinLight() {m_ltcLight->cancel();}

void DexEngine::requestBchStartupChecks() {
    if (!m_bchStartupChecks.isEmpty()) return;
    if (m_bchAdmission->status().value("configured").toBool()) m_bchStartupChecks.append("admission");
    if (m_bchRefundProtection->status().value("configured").toBool()) m_bchStartupChecks.append("refund-protection");
    if (m_bchFundingReceipt->status().value("configured").toBool()) m_bchStartupChecks.append("funding-receipt");
    if (m_bchNativePreparation->status().value("configured").toBool()) m_bchStartupChecks.append("native-preparation");
    if (m_bchNativeRelay->status().value("configured").toBool()) m_bchStartupChecks.append("native-relay");
    if (m_bchNativeProof->status().value("configured").toBool()) m_bchStartupChecks.append("native-proof");
    if (m_bchPreparation->status().value("configured").toBool()) m_bchStartupChecks << "preparation" << "locks" << "signing";
    if (m_bchRecoveryRetention->status().value("configured").toBool()) m_bchStartupChecks.append("recovery-retention");
    if (m_bchFunding->status().value("configured").toBool()) m_bchStartupChecks.append("funding");
    if (m_bchRefundWallet->status().value("configured").toBool()) m_bchStartupChecks.append("refund-wallet");
    if (m_bchPunishment->status().value("configured").toBool()) m_bchStartupChecks.append("punishment");
    if (m_bchCancellation->status().value("configured").toBool()) m_bchStartupChecks.append("cancellation");
    if (m_bchRefundRelease->status().value("configured").toBool()) m_bchStartupChecks.append("refund-release");
    advanceBchStartupChecks();
}
void DexEngine::advanceBchStartupChecks() {
    while (bchOperationIdle() && !m_bchStartupChecks.isEmpty()) {
        const auto next=m_bchStartupChecks.takeFirst();
        const bool started=next=="refund-wallet"?requestBchRefundWalletCheck():next=="punishment"?requestBchPunishmentCheck():next=="native-proof"?requestBchNativeProofCheck():next=="native-relay"?requestBchNativeRelayCheck():next=="native-preparation"?requestBchNativePreparationCheck():next=="funding-receipt"?requestBchFundingReceiptCheck():next=="refund-release"?requestBchRefundReleaseCheck():next=="cancellation"?requestBchCancellationCheck():next=="recovery-retention"?requestBchRecoveryRetentionCheck():next=="admission"?requestBchAdmissionCheck():next=="refund-protection"?requestBchRefundProtectionCheck():next=="preparation"?
            requestBchPreparationCheck():next=="locks"?requestBchLockCheck():next=="signing"?requestBchSigningCheck():requestBchFundingCheck();
        if (started) return;
    }
}
bool DexEngine::bchOperationIdle() const {
    return !m_bchLightBinding && !m_bchLightFunding->busy()
        && !m_bchPreparation->status().value("busy").toBool() && !m_bchFunding->status().value("busy").toBool()
        && !m_bchAdmission->status().value("busy").toBool() && !m_bchRefundProtection->status().value("busy").toBool()
        && !m_bchRecoveryRetention->status().value("busy").toBool()
        && !m_bchRefundWallet->status().value("busy").toBool()
        && !m_bchPunishment->status().value("busy").toBool()
        && !m_bchCancellation->status().value("busy").toBool()
        && !m_bchRefundRelease->status().value("busy").toBool()
        && !m_bchFundingReceipt->status().value("busy").toBool()
        && !m_bchNativePreparation->status().value("busy").toBool()
        && !m_bchNativeRelay->status().value("busy").toBool()
        && !m_bchNativeProof->status().value("busy").toBool()
        && !m_bchNativeObservation->status().value("busy").toBool()
        && !m_bchSuccessReleaseReview->status().value("busy").toBool()
        && !m_bchSuccessSignatureReview->status().value("busy").toBool()
        && !m_bchSuccessSignedReleaseReview->status().value("busy").toBool()
        && !m_bchSuccessPreparation->status().value("busy").toBool()
        && !m_bchPublicationRepair->status().value("busy").toBool();
}
bool DexEngine::configureBchPreparation(const QString &path) {return m_bchPreparation->configure(path);}
bool DexEngine::prepareBchFunding() {return bchOperationIdle() && m_bchPreparation->prepare();}
bool DexEngine::requestBchPreparationCheck() {return bchOperationIdle() && m_bchPreparation->refresh();}
bool DexEngine::configureBchSigningRecovery(const QString &path) {return bchOperationIdle() && m_bchPreparation->configureSigningRecovery(path);}
bool DexEngine::requestBchSigningRecoveryReview() {return bchOperationIdle() && m_bchPreparation->reviewSigningRecovery();}
bool DexEngine::importBchSigningRecovery(const QString &commitment) {return bchOperationIdle() && m_bchPreparation->importSigningRecovery(commitment);}
bool DexEngine::requestBchSigningReview() {return bchOperationIdle() && m_bchPreparation->reviewSigning();}
bool DexEngine::requestBchSigningCheck() {return bchOperationIdle() && m_bchPreparation->checkSigning();}
bool DexEngine::submitBchSigning(const QString &commitment) {return bchOperationIdle() && m_bchPreparation->sign(commitment);}
bool DexEngine::requestBchLockReview() {return bchOperationIdle() && m_bchPreparation->reviewLocks();}
bool DexEngine::requestBchLockCheck() {return bchOperationIdle() && m_bchPreparation->checkLocks();}
bool DexEngine::submitBchLock(const QString &commitment) {return bchOperationIdle() && m_bchPreparation->lockInputs(commitment);}
QVariantMap DexEngine::bchPreparationStatus() const {
    auto status=m_bchPreparation->status();
    if (!bchOperationIdle()) {
        for (const auto key:{"prepareAvailable","checkAvailable","lockReviewAvailable","lockCheckAvailable","lockSubmitAvailable","signingReviewAvailable","signingCheckAvailable","signingSubmitAvailable","recoveryReviewAvailable","recoverySubmitAvailable"}) status[key]=false;
    }
    return status;
}
bool DexEngine::configureBchFunding(const QString &path) {return m_bchFunding->configure(path);}
bool DexEngine::configureBchFundingSubmission(const QString &path) {return m_bchFunding->configureSubmission(path);}
bool DexEngine::requestBchFundingObservation() {return bchOperationIdle() && m_bchFunding->observe();}
bool DexEngine::requestBchFundingCheck() {return bchOperationIdle() && m_bchFunding->refresh();}
bool DexEngine::submitBchFunding(const QString &commitment) {return bchOperationIdle() && m_bchFunding->submit(commitment);}
QVariantMap DexEngine::bchFundingStatus() const {
    auto status=m_bchFunding->status();
    if (!bchOperationIdle()) { status["reviewAvailable"]=false;status["checkAvailable"]=false;status["submitAvailable"]=false;status["observeAvailable"]=false; }
    return status;
}
bool DexEngine::requestBchFundingReview() {return bchOperationIdle() && m_bchFunding->review();}
bool DexEngine::configureBchFundingReceipt(const QString &path) {return bchOperationIdle() && m_bchFundingReceipt->configure(path);}
QVariantMap DexEngine::bchFundingReceiptStatus() const {
    auto status=m_bchFundingReceipt->status();
    if (!bchOperationIdle()) for (const auto key:{"reviewAvailable","checkAvailable","submitAvailable"}) status[key]=false;
    return status;
}
bool DexEngine::requestBchFundingReceiptReview() {return bchOperationIdle() && m_bchFundingReceipt->review();}
bool DexEngine::submitBchFundingReceipt(const QString &commitment) {return bchOperationIdle() && m_bchFundingReceipt->submit(commitment);}
bool DexEngine::requestBchFundingReceiptCheck() {return bchOperationIdle() && m_bchFundingReceipt->refresh();}
bool DexEngine::configureBchNativePreparation(const QString &path) {return bchOperationIdle() && m_bchNativePreparation->configure(path);}
QVariantMap DexEngine::bchNativePreparationStatus() const {
    auto status=m_bchNativePreparation->status();
    if (!bchOperationIdle()) for (const auto key:{"reviewAvailable","checkAvailable","submitAvailable"}) status[key]=false;
    return status;
}
bool DexEngine::requestBchNativePreparationReview() {return bchOperationIdle() && m_bchNativePreparation->review();}
bool DexEngine::submitBchNativePreparation(const QString &commitment) {return bchOperationIdle() && m_bchNativePreparation->submit(commitment);}
bool DexEngine::requestBchNativePreparationCheck() {return bchOperationIdle() && m_bchNativePreparation->refresh();}
bool DexEngine::configureBchNativeRelay(const QString &path) {return bchOperationIdle() && m_bchNativeRelay->configure(path);}
QVariantMap DexEngine::bchNativeRelayStatus() const {
    auto status=m_bchNativeRelay->status();
    if (!bchOperationIdle()) for (const auto key:{"reviewAvailable","checkAvailable","submitAvailable"}) status[key]=false;
    return status;
}
bool DexEngine::requestBchNativeRelayReview() {return bchOperationIdle() && m_bchNativeRelay->review();}
bool DexEngine::submitBchNativeRelay(const QString &commitment) {return bchOperationIdle() && m_bchNativeRelay->submit(commitment);}
bool DexEngine::requestBchNativeRelayCheck() {return bchOperationIdle() && m_bchNativeRelay->refresh();}
bool DexEngine::configureBchNativeProof(const QString &path,const QJsonObject &fundingContext) {return bchOperationIdle() && m_bchNativeProof->configure(path,fundingContext);}
QVariantMap DexEngine::bchNativeProofStatus() const {
    auto status=m_bchNativeProof->status();
    if (!bchOperationIdle()) for (const auto key:{"reviewAvailable","checkAvailable","submitAvailable"}) status[key]=false;
    return status;
}
bool DexEngine::requestBchNativeProofReview() {return bchOperationIdle() && m_bchNativeProof->review();}
bool DexEngine::submitBchNativeProof(const QString &commitment) {return bchOperationIdle() && m_bchNativeProof->submit(commitment);}
bool DexEngine::requestBchNativeProofCheck() {return bchOperationIdle() && m_bchNativeProof->refresh();}
namespace {
// EVM chainId -> operator control-document network. "-mainnet" names carry real funds and need a
// signed document (the owner signs e.g. bnb-mainnet, like btc-mainnet); test/local chains map to
// "evm-regtest", which DexRiskControl treats as needing no document (nothing to cap). An unknown
// chain returns empty, which fails the gate closed.
QString evmControlNetwork(qint64 chainId) {
    switch (chainId) {
    case 56: return QStringLiteral("bnb-mainnet");
    case 1:  return QStringLiteral("eth-mainnet");
#ifndef MONZERO_DEX_PUBLIC_EDITION
    // The public edition trades on mainnets only, always under a signed document
    case 97: case 11155111: case 1337: case 31337: return QStringLiteral("evm-regtest");
#endif
    default: return {};
    }
}
// The trade-limit ledger keys trades by UUID; an EVM trade id is a 32-byte hash ("0x" + 64 hex).
// Its first 16 bytes give the same UUID every time, so a resumed trade is found again.
QString evmLedgerId(const QString &tradeId) {
    static const QRegularExpression hash("\\A0x[0-9a-f]{64}\\z");
    if (!hash.match(tradeId).hasMatch()) return {};
    return QUuid::fromRfc4122(QByteArray::fromHex(tradeId.mid(2,32).toLatin1())).toString(QUuid::WithoutBraces);
}
// A trade id no real trade has: checks caps for an offer without counting anything
const QString OfferCheckId=QStringLiteral("6f666665-722d-4368-9563-6b0000000000");
}
void DexEngine::bindEvmNetwork(const QString &network) {
    if (m_evmBoundNetwork==network) return;
    // Bind the EVM risk control to this chain and load its signed control document once
    m_evmRiskControl->setNetwork(network);m_evmBoundNetwork=network;
    if (!m_evmControlFile.isEmpty()) m_evmRiskControl->loadControlFile(m_evmControlFile);
    if (!m_evmControlUrlBase.isEmpty() && network.endsWith("-mainnet")) m_evmRiskControl->startPolling(m_evmControlUrlBase+network+".json");
}

bool DexEngine::configureEvmTrade(const QString &config,const QString &seedFile) {
    // A seed file (prototype) or this session's vault keys
    const bool seedOk=seedFile.isEmpty() ? dexKeysReady() : (QDir::isAbsolutePath(seedFile) && QDir::cleanPath(seedFile)==seedFile);
    if (!seedOk || !m_evmTrade->configure(config)) return false;
    m_evmSeedFile=seedFile;m_evmTrade->refresh();m_evmTicker->start();return true;
}
QVariantMap DexEngine::evmTradeStatus() const {
    auto s=m_evmTrade->status();s.insert("stepping",m_evmTicker->isActive());s.insert("startRefusal",m_evmStartRefusal);return s;
}
bool DexEngine::refreshEvmTrade() {return m_evmTrade->refresh();}
bool DexEngine::approveEvmTrade(const QString &commitment) {
    // The operator kill switch and caps gate the START of a new EVM trade, exactly as for UTXO
    // swaps. refusal() returns empty for a trade already in the ledger, so later approvals of an
    // in-flight trade are never blocked (nothing is stranded); only the first is enforced.
    const auto offer=m_evmTrade->offer();const auto trade=m_evmTrade->tradeId();
    if (offer.isEmpty() || trade.isEmpty()) return false;
    const auto network=evmControlNetwork(qint64(offer.value("chainId").toDouble()));
    const auto refuse=[this](const QString &why){m_evmStartRefusal=why;emit evmTradeChanged();return false;};
    if (network.isEmpty()) return refuse(tr("This EVM chain is not supported for trading."));
    bool valueOk=false,xmzOk=false;
    // The helper shows the value in decimal; hex ("0x...") is accepted too
    const auto valueText=offer.value("value").toString();
    const quint64 valueWei=valueText.startsWith("0x")?valueText.mid(2).toULongLong(&valueOk,16)
        :(QRegularExpression("\\A[0-9]{1,20}\\z").match(valueText).hasMatch()?valueText.toULongLong(&valueOk,10):0);
    const auto xmzValue=offer.value("xmzAmount");
    const quint64 xmzAtoms=xmzValue.isString()?xmzValue.toString().toULongLong(&xmzOk)
        :(xmzOk=(xmzValue.isDouble() && xmzValue.toDouble()>=0 && xmzValue.toDouble()<9.007199254740992e15),quint64(xmzValue.toDouble()));
    if (!valueOk || !xmzOk) return refuse(tr("The trade amount could not be read."));
    const auto ledgerId=evmLedgerId(trade);
    if (ledgerId.isEmpty()) return refuse(tr("The trade identifier could not be read."));
    bindEvmNetwork(network);
    const auto refusal=m_evmRiskControl->refusal(ledgerId,valueWei,xmzAtoms);
    if (!refusal.isEmpty()) return refuse(refusal);
    if (!m_evmRiskControl->recordStart(ledgerId,valueWei)) return refuse(tr("The trade could not be recorded; not started."));
    m_evmStartRefusal.clear();
    return m_evmTrade->approve(commitment);
}
bool DexEngine::payoutEvmTrade(const QString &address) {
    // A seed file (prototype) or this session's vault keys, as for every other trade step
    if ((m_evmSeedFile.isEmpty() && !dexKeysReady()) || m_evmTrade->status().value("busy").toBool()) return false;
    QByteArray seed=evmSeed();
    return m_evmTrade->payout(seed,address);
}
void DexEngine::stepEvmTrade() {
    const auto s=m_evmTrade->status();
    const auto snapshot=s.value("snapshot").toMap();
    if (snapshot.value("done").toBool()) {
        // A swap we claimed: tell the relays where the claim is, for the public price chart
        if (snapshot.value("role")=="claimer" && snapshot.value("stage")=="Done(Swapped)" && m_reportedTrade!=m_evmTrade->tradeId()
            && evmMarketFor(m_evmTrade->tradeDir()) && evmMarketFor(m_evmTrade->tradeDir())->report(m_evmTrade->tradeDir()))
            m_reportedTrade=m_evmTrade->tradeId();
        m_evmTicker->stop();emit evmTradeChanged();return;
    }
    if (s.value("busy").toBool() || (m_evmSeedFile.isEmpty() && !dexKeysReady())) return;
    // The seed is read for each step and wiped by the job once written to the helper
    QByteArray hex=LitecoinJournalProtocol::privateConfig(m_evmSeedFile,256).trimmed();
    QByteArray seed=QByteArray::fromHex(hex);hex.fill('\0');
    m_evmTrade->tick(seed);
}
QByteArray DexEngine::evmSeed() const {
    if (m_evmSeedFile.isEmpty()) return {};
    QByteArray hex=LitecoinJournalProtocol::privateConfig(m_evmSeedFile,256).trimmed();
    QByteArray seed=QByteArray::fromHex(hex);hex.fill('\0');
    return seed;
}
EvmMarketJob *DexEngine::evmMarketFor(const QString &tradeDir) const {
    for (auto *job:m_evmMarkets) if (tradeDir.startsWith(job->config().value("tradesDir").toString()+"/")) return job;
    return nullptr;
}
EvmMarketJob *DexEngine::evmMarketForCoin(const QString &coin) const {
    for (auto *job:m_evmMarkets) if (job->status().value("coin")==coin) return job;
    return nullptr;
}
bool DexEngine::addEvmMarket(const QString &config) {
    auto *job=new EvmMarketJob(this);
    if (!job->configure(config)) {job->deleteLater();return false;}
    const auto chain=int(job->config().value("chainId").toDouble());
    if (m_evmMarkets.contains(chain)) {job->deleteLater();return false;}
    connect(job,&EvmMarketJob::changed,this,&DexEngine::evmMarketChanged);
    connect(job,&EvmMarketJob::agreed,this,[this,job](const QString &tradeDir){evmOfferAgreed(job,tradeDir);});
    m_evmMarkets.insert(chain,job);
    if (!m_evmSwapSeedHex.isEmpty()) job->setSwapKeys(m_evmSwapSeedHex,m_evmKeyHex);
    m_marketTicker->start();
    // Start fetching this chain's control document now, so the first offer is not refused for lack of it
    const auto network=evmControlNetwork(chain);
    if (!network.isEmpty()) bindEvmNetwork(network);
    emit evmMarketChanged();return !network.isEmpty();
}
QStringList DexEngine::defaultRelays() {
    // The open relays the project runs (anyone can add their own with MONZERO_DEX_RELAYS, comma-separated)
    const auto extra=qEnvironmentVariable("MONZERO_DEX_RELAYS").split(',',Qt::SkipEmptyParts);
    if (!extra.isEmpty()) {QStringList r;for (const auto &e:extra) r<<e.trimmed();return r.mid(0,8);}
    return {QStringLiteral("https://relay1.monzero.org"),QStringLiteral("https://relay2.monzero.org")};
}
void DexEngine::setUpEvmMarkets() {
    if (m_evmAuto || !m_dexWallets->ready() || !m_evmMarkets.isEmpty()) return;
    m_evmAuto=true;
    const auto main=m_dexWallets->mainEndpoint();
    const auto root=m_dexWallets->root();
    const bool mainnet=DexDeployment::mainnetStage();
    struct Chain {int id;const char *rpc;};
    const QList<Chain> chains=mainnet ? QList<Chain>{{1,"https://ethereum-rpc.publicnode.com"},{56,"https://bsc-rpc.publicnode.com"}}
                                      : QList<Chain>{{11155111,"https://ethereum-sepolia-rpc.publicnode.com"},{97,"https://bsc-testnet-rpc.publicnode.com"}};
    QJsonArray relays;for (const auto &r:defaultRelays()) relays.append(r);
    for (const auto &c:chains) {
        const auto rpcOverride=qEnvironmentVariable(QStringLiteral("MONZERO_DEX_EVM_RPC_%1").arg(c.id).toLatin1().constData());
        // Until a trade agrees, the swap wallet is the trading wallet's service; each trade gets its own
        const QJsonObject config{{"schema","monzero-private-xmz-evm-market-v1"},{"helper",m_dexWallets->helper()},{"helperDigest",m_dexWallets->helperSha256()},
            {"tradesDir",QStringLiteral("%1/evm-trades/%2").arg(root).arg(c.id)},{"evmRpc",rpcOverride.isEmpty() ? QString::fromLatin1(c.rpc) : rpcOverride},
            {"chainId",c.id},{"accountIndex",0},{"mainWallet",main},{"swapWallet",main},{"relays",relays}};
        const auto path=QStringLiteral("%1/evm-market-%2.json").arg(root).arg(c.id);
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly|QIODevice::Truncate) || !f.setPermissions(QFileDevice::ReadOwner|QFileDevice::WriteOwner)) continue;
        f.write(QJsonDocument(config).toJson(QJsonDocument::Compact));f.close();
        if (!addEvmMarket(path)) qWarning("The DEX market for chain %d could not start",c.id);
    }
}
namespace {
// Trade settings of each swap network: confirmations and timelocks, the fee of each contract spend,
// fee ceilings, the smallest output and largest lock. Sized for the opening limits ($25 a trade).
struct UtxoDefaults {int confirmations,cancelBlocks,punishBlocks;qint64 spendFee,fundingFeeMax,recoveryFeeMax,minimumOutput,maximumLock;};
UtxoDefaults utxoDefaults(const QString &network) {
    if (network=="btc-mainnet") return {2,36,10,2000,20000,20000,10000,1000000};
    if (network.startsWith("btc-")) return {1,36,10,2000,20000,20000,10000,10000000};
    return {network=="ltc-mainnet" ? 2 : 1,72,20,10000,100000,50000,100000,100000000};
}
// Electrum servers the trade journal may use besides the wallet's own (it needs two independent ones)
QJsonArray electrumServers(const QString &network,const QJsonObject &own) {
    QJsonArray servers;QSet<QString> seen;
    const auto add=[&](const QString &host,int port){
        const auto key=host.toLower()+":"+QString::number(port);
        if (host.isEmpty() || port<1 || port>65535 || seen.contains(key) || servers.size()>=8) return;
        seen.insert(key);servers.append(QJsonObject{{"host",host},{"port",port}});
    };
    if (!own.isEmpty()) add(own.value("host").toString(),own.value("port").toInt());
    for (const auto &entry:qEnvironmentVariable("MONZERO_DEX_ELECTRUM").split(',',Qt::SkipEmptyParts))
        add(entry.section(':',0,-2).trimmed(),entry.section(':',-1).toInt());
    if (network=="btc-mainnet") {add("electrum.acinq.co",50002);add("electrum.hodlister.co",50002);add("electrum2.bluewallet.io",443);}
    if (network=="ltc-mainnet") {add("electrum-ltc.bysh.me",50002);add("backup.electrum-ltc.org",443);add("electrum1.cipig.net",20063);}
    return servers;
}
}
void DexEngine::setUpUtxoMarket() {
    if (m_utxoAuto || m_btcMarket->status().value("configured").toBool()) return;
    const auto network=DexDeployment::litecoinNetwork();
    if (BtcTradeSetup::pairOf(network).isEmpty() || network.endsWith("regtest")) return;
    const auto xmz=m_dexWallets->utxoLocal();
    auto *light=swapLight();
    if (xmz.isEmpty() || !light || m_journalHelper.isEmpty()) return;
    connect(light,&LitecoinLightJob::changed,this,&DexEngine::setUpUtxoMarket,Qt::UniqueConnection);
    const auto note=[this](const QString &why){if (m_utxoMessage!=why) {m_utxoMessage=why;emit btcMarketChanged();}};
    if (!light->walletConfigured()) {note(tr("Set up your %1 light wallet to trade %1 offers.").arg(DexDeployment::swapCoinName()));return;}
    const auto servers=electrumServers(network,light->lightEndpoint());
    if (servers.size()<2) {note(tr("Trading %1 needs two Electrum servers; add one with MONZERO_DEX_ELECTRUM.").arg(DexDeployment::swapCoinName()));return;}
    // The XMZ chain's genesis: fixed on mainnet, asked of the local daemon otherwise
    if (m_xmzGenesis.isEmpty()) {
        if (DexDeployment::mainnetStage()) m_xmzGenesis=DexDeployment::xmzMainnetGenesis();
        else {
            static bool asking=false;
            if (asking) return;
            asking=true;
            auto *net=new QNetworkAccessManager(this);
            QNetworkRequest request(QUrl(QStringLiteral("http://127.0.0.1:%1/json_rpc").arg(m_dexWallets->daemonPort())));
            request.setHeader(QNetworkRequest::ContentTypeHeader,"application/json");
            auto *reply=net->post(request,QJsonDocument(QJsonObject{{"jsonrpc","2.0"},{"id","0"},{"method","get_block_header_by_height"},
                {"params",QJsonObject{{"height",0}}}}).toJson(QJsonDocument::Compact));
            connect(reply,&QNetworkReply::finished,this,[this,reply,net]{
                asking=false;
                const auto hash=QJsonDocument::fromJson(reply->readAll()).object().value("result").toObject().value("block_header").toObject().value("hash").toString();
                reply->deleteLater();net->deleteLater();
                static const QRegularExpression hex("\\A[0-9a-f]{64}\\z");
                if (hex.match(hash).hasMatch() && hash!=DexDeployment::xmzMainnetGenesis()) {m_xmzGenesis=hash;setUpUtxoMarket();}
            });
            return;
        }
    }
    // The user's own BTC/LTC address for payouts and refunds, from the light wallet
    const auto address=light->receivedAddress();
    if (address.isEmpty()) {
        if (!m_utxoAsked && !light->busy()) m_utxoAsked=light->receiveCurrentWallet();
        return;
    }
    m_utxoAuto=true;
    const auto d=utxoDefaults(network);
    const auto xmzNetwork=DexDeployment::mainnetStage() ? QStringLiteral("xmz-mainnet") : QStringLiteral("xmz-testnet");
    const QJsonObject policy{{"ltc_network",network},{"xmz_network",xmzNetwork},{"xmz_genesis",m_xmzGenesis},{"ltc_confirmations",d.confirmations},
        {"xmz_confirmations",10},{"cancel_blocks",d.cancelBlocks},{"punish_blocks",d.punishBlocks},{"maximum_ltc_atoms",double(d.maximumLock)},
        {"maximum_xmz_atoms",1e17},{"maximum_ltc_funding_fee",double(d.fundingFeeMax)},{"maximum_ltc_recovery_fee",double(d.recoveryFeeMax)},
        {"maximum_xmz_fee",double(BtcTradeSetup::MaxXmzFeeAtoms)},{"minimum_ltc_output",double(d.minimumOutput)},{"minimum_relay_fee_atoms_per_kb",1000}};
    const auto n=[](qint64 v){return QString::number(v);};
    const auto xmzAddress=xmz.value("sourceAddress").toString();
    // xmzPayoutAtoms is worked out for each offer from its amount (BtcTradeSetup::xmzPayout)
    const QJsonObject params{{"ltcGenesis",DexDeployment::litecoinGenesis()},{"xmzGenesis",m_xmzGenesis},{"ltcNetwork",network},{"xmzNetwork",xmzNetwork},
        {"ltcPayout",address},{"ltcRefund",address},{"xmzPayout",xmzAddress},{"xmzRefund",xmzAddress},
        {"ltcFundingFeeMaxAtoms",n(d.fundingFeeMax)},{"xmzNativeFeeMaxAtoms",n(BtcTradeSetup::MaxXmzFeeAtoms)},
        {"ltcCancelFeeAtoms",n(d.spendFee)},{"ltcRefundFeeAtoms",n(d.spendFee)},{"ltcRedeemFeeAtoms",n(d.spendFee)},
        {"ltcPunishFeeAtoms",n(d.spendFee)},{"ltcEarlyRefundFeeAtoms",n(d.spendFee)},
        {"ltcConfirmations",n(d.confirmations)},{"xmzConfirmations","10"},{"cancelBlocks",n(d.cancelBlocks)},{"punishBlocks",n(d.punishBlocks)},
        {"ltcMinimumOutputAtoms",n(d.minimumOutput)},{"ltcMinimumRelayFeeAtomsPerKb","1000"},{"xmzPayoutAtoms","0"},
        {"xmzClaimFeeMaxAtoms",n(BtcTradeSetup::MaxXmzFeeAtoms)},{"xmzRefundFeeMaxAtoms",n(BtcTradeSetup::MaxXmzFeeAtoms)}};
    QJsonArray relays;for (const auto &r:defaultRelays()) relays.append(r);
    const auto root=m_dexWallets->root();
    const QJsonObject config{{"schema","monzero-private-btc-market-v1"},{"network",network},{"relays",relays},
        {"tradesDir",root+"/utxo-trades/"+network},
        {"journalHelper",m_journalHelper},
        {"local",QJsonObject{{"litecoin",QJsonObject{{"light",QJsonObject{{"servers",servers},{"maximumTipAge",5400}}}}},{"xmz",xmz}}},
        {"params",params},{"policy",policy}};
    const auto path=root+"/utxo-market-"+network+".json";
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly|QIODevice::Truncate) || !f.setPermissions(QFileDevice::ReadOwner|QFileDevice::WriteOwner)) {m_utxoAuto=false;return;}
    f.write(QJsonDocument(config).toJson(QJsonDocument::Compact));f.close();
    if (!configureBtcMarket(path)) {qWarning("The DEX %s market could not start",qPrintable(network));note(tr("The %1 offers could not start.").arg(DexDeployment::swapCoinName()));return;}
    note({});
}
bool DexEngine::configureEvmMarket(const QString &config,const QString &seedFile) {
    if (!QDir::isAbsolutePath(seedFile) || QDir::cleanPath(seedFile)!=seedFile) return false;
    m_evmSeedFile=seedFile;
    return addEvmMarket(config);
}
QVariantMap DexEngine::evmMarketStatus() const {
    // By coin ("ETH", "BNB"); each with the shared message
    QVariantMap all;
    for (auto *job:m_evmMarkets) {auto s=job->status();s.insert("message",m_marketMessage);all.insert(s.value("coin").toString(),s);}
    return all;
}
QString DexEngine::atomic(const QString &text,int places) {
    static const QRegularExpression format("\\A([0-9]{1,30})(\\.([0-9]{1,30}))?\\z");
    const auto m=format.match(text.trimmed());
    if (!m.hasMatch() || m.captured(3).size()>places) return {};
    QString digits=m.captured(1)+m.captured(3).leftJustified(places,'0');
    while (digits.size()>1 && digits.startsWith('0')) digits.remove(0,1);
    return digits=="0" ? QString() : digits;
}
QString DexEngine::evmOfferRefusal(qint64 chainId,const QString &valueWei,const QString &xmzAtoms) {
    const auto network=evmControlNetwork(chainId);
    if (network.isEmpty()) return tr("This EVM chain is not supported for trading.");
    bool valueOk=false,xmzOk=false;
    const quint64 wei=valueWei.toULongLong(&valueOk),atoms=xmzAtoms.toULongLong(&xmzOk);
    if (!valueOk || !xmzOk) return tr("The amount is too large for this test phase.");
    bindEvmNetwork(network);
    // The same caps and kill switch as the approval; nothing is recorded here
    return m_evmRiskControl->refusal(OfferCheckId,wei,atoms);
}
bool DexEngine::refreshEvmOffers(const QString &pair) {
    auto *job=evmMarketForCoin(pair.section('/',0,0));
    return job && job->refreshListings(pair);
}
bool DexEngine::postEvmOffer(int chainId,const QString &side,const QString &coinAmount,const QString &xmzAmount,int lifetimeHours) {
    const auto wei=atomic(coinAmount,18),atoms=atomic(xmzAmount,11);
    if (wei.isEmpty() || atoms.isEmpty() || lifetimeHours<1 || lifetimeHours>24) {m_marketMessage=tr("Check the amounts and how long the offer stays open (1-24 hours).");emit evmMarketChanged();return false;}
    const auto refusal=evmOfferRefusal(chainId,wei,atoms);
    if (!refusal.isEmpty()) {m_marketMessage=refusal;emit evmMarketChanged();return false;}
    auto *job=m_evmMarkets.value(chainId);
    if (!job) return false;
    QByteArray seed=evmSeed();
    const bool started=job->post(seed,chainId,side,wei,atoms,qint64(lifetimeHours)*3600);
    m_marketMessage=started ? QString() : tr("The offer could not be posted now.");
    emit evmMarketChanged();return started;
}
bool DexEngine::takeEvmOffer(const QString &listingId) {
    for (auto *job:m_evmMarkets) for (const auto &v:job->status().value("listings").toList()) {
        const auto l=v.toMap();
        if (l.value("id")!=listingId) continue;
        const auto refusal=evmOfferRefusal(qint64(l.value("chainId").toDouble()),l.value("valueWei").toString(),l.value("xmzAtoms").toString());
        if (!refusal.isEmpty()) {m_marketMessage=refusal;emit evmMarketChanged();return false;}
        QByteArray seed=evmSeed();
        const bool started=job->take(seed,listingId);
        m_marketMessage=started ? QString() : tr("The offer could not be taken now.");
        emit evmMarketChanged();return started;
    }
    return false;
}
bool DexEngine::cancelEvmOffer(const QString &folder) {
    auto *job=evmMarketFor(folder);
    QByteArray seed=evmSeed();
    return job && job->cancel(seed,folder);
}
bool DexEngine::configureDexWallets(const QString &root,const QString &walletRpc,const QString &walletRpcSha256,quint16 daemonPort,
        const QString &networkFlag,const QString &helper,const QString &helperSha256) {
    return m_dexWallets->configure(root,walletRpc,walletRpcSha256,daemonPort,networkFlag,helper,helperSha256) && m_dexWallets->start();
}
QVariantMap DexEngine::dexWalletStatus() const {
    auto s=m_dexWallets->status();
    s.insert("keysReady",dexKeysReady());s.insert("keysBusy",m_keysBusy);s.insert("tradeAccount",m_evmAccount);s.insert("keysMessage",m_keysMessage);
    return s;
}
bool DexEngine::refreshDexWallet() {return m_dexWallets->refresh();}
bool DexEngine::unlockDexKeys(const QString &password) {
    if (m_keysBusy || password.isEmpty()) return false;
    // The vault that holds the user's Bitcoin (or else Litecoin) light wallet
    LitecoinLightJob *vault=nullptr;
    for (const auto &coin:{"btc","ltc"}) {
        auto *job=m_lightWallets.value(coin);
        if (job && job->vaultReady()) {vault=job;break;}
    }
    if (!vault && swapLight() && swapLight()->vaultReady()) vault=swapLight();
    if (!vault) {m_keysMessage=tr("Set up a Bitcoin or Litecoin wallet in the app first: its recovery words also hold your trading keys.");emit dexWalletChanged();return false;}
    m_keysBusy=true;m_keysMessage.clear();emit dexWalletChanged();
    const bool started=vault->deriveEvmSwapKeys(password,0,[this](QByteArray seed,QByteArray key,QString address){
        m_keysBusy=false;
        if (seed.isEmpty()) {m_keysMessage=tr("The wallet could not be unlocked; check the password.");emit dexWalletChanged();return;}
        m_evmTrade->setSwapKeys(seed,key);
        for (auto *job:m_evmMarkets) job->setSwapKeys(seed,key);
        sodium_memzero(m_evmSwapSeedHex.data(),m_evmSwapSeedHex.size());sodium_memzero(m_evmKeyHex.data(),m_evmKeyHex.size());
        m_evmSwapSeedHex=seed;m_evmKeyHex=key;
        m_evmAccount=address;m_keysMessage.clear();
        emit dexWalletChanged();emit evmMarketChanged();
    });
    if (!started) {m_keysBusy=false;m_keysMessage=tr("The wallet is busy; try again in a moment.");emit dexWalletChanged();}
    return started;
}
bool DexEngine::sendFromDexWallet(const QString &address,const QString &amount) {
    const auto atoms=amount.trimmed().isEmpty() ? QString() : atomic(amount,11);
    if (!amount.trimmed().isEmpty() && atoms.isEmpty()) return false;
    return m_dexWallets->send(address.trimmed(),atoms);
}
bool DexEngine::configureBtcMarket(const QString &config) {
    if (!m_btcMarket->configure(config)) return false;
    // The swap stage and the offers must be on the same Bitcoin network
    if (m_btcMarket->network()!=DexDeployment::litecoinNetwork()) return false;
    m_btcTicker->start();return true;
}
QVariantMap DexEngine::btcMarketStatus() const {auto s=m_btcMarket->status();s.insert("message",m_btcMessage.isEmpty() ? m_utxoMessage : m_btcMessage);return s;}
bool DexEngine::refreshBtcOffers() {return m_btcMarket->refreshListings();}
QString DexEngine::btcOfferRefusal(const QString &sats,const QString &atoms) {
    bool satsOk=false,atomsOk=false;
    const auto s=sats.toULongLong(&satsOk),a=atoms.toULongLong(&atomsOk);
    if (!satsOk || !atomsOk) return tr("The amount could not be read.");
    // The same kill switch and caps as the trade start; nothing is recorded here
    return m_riskControl->refusal(OfferCheckId,s,a);
}
bool DexEngine::postBtcOffer(const QString &side,const QString &btcAmount,const QString &xmzAmount,int lifetimeHours) {
    const auto sats=atomic(btcAmount,8),atoms=atomic(xmzAmount,11);
    if (sats.isEmpty() || atoms.isEmpty()) {m_btcMessage=tr("Check the amounts.");emit btcMarketChanged();return false;}
    const auto refusal=btcOfferRefusal(sats,atoms);
    if (!refusal.isEmpty()) {m_btcMessage=refusal;emit btcMarketChanged();return false;}
    m_btcMessage.clear();
    const bool ok=m_btcMarket->post(side,sats,atoms,lifetimeHours);
    if (!ok) m_btcMessage=tr("The offer could not be posted now.");
    emit btcMarketChanged();return ok;
}
bool DexEngine::takeBtcOffer(const QString &listingId) {
    const auto a=m_btcMarket->listingAmounts(listingId);
    const auto refusal=btcOfferRefusal(a.value("btcSats").toString(),a.value("xmzAtoms").toString());
    if (!refusal.isEmpty()) {m_btcMessage=refusal;emit btcMarketChanged();return false;}
    m_btcMessage.clear();
    const bool ok=m_btcMarket->take(listingId);
    if (!ok) m_btcMessage=tr("The offer could not be taken now.");
    emit btcMarketChanged();return ok;
}
bool DexEngine::cancelBtcOffer(const QString &folder) {return m_btcMarket->cancel(folder);}
void DexEngine::stepEvmMarket() {
    if (m_evmSeedFile.isEmpty() && !dexKeysReady()) return;
    for (auto *job:m_evmMarkets) {
        if (job->status().value("busy").toBool()) continue;
        QByteArray seed=evmSeed();
        job->step(seed);
    }
}
void DexEngine::evmOfferAgreed(EvmMarketJob *job,const QString &tradeDir) {
    // The taken offer is a saved trade now: give it its own swap-wallet service (the app's, when it
    // runs the wallets), then hand it to the trade job (one trade at a time)
    const auto open=[this,job,tradeDir](const QJsonObject &swapWallet){
        const auto path=tradeDir+"/trade-config.json";
        auto config=job->tradeConfig(tradeDir);
        if (!swapWallet.isEmpty()) config.insert("swapWallet",swapWallet);
        if (!QFile::exists(path)) {
            QFile file(path);
            if (!file.open(QIODevice::WriteOnly|QIODevice::NewOnly)) return;
            file.setPermissions(QFileDevice::ReadOwner|QFileDevice::WriteOwner);
            file.write(QJsonDocument(config).toJson(QJsonDocument::Compact));file.close();
        }
        if (m_evmTrade->status().value("configured").toBool()) {
            m_marketMessage=tr("Keys agreed for a new trade (%1). Finish the current trade, then restart the app to continue it.").arg(tradeDir);
            emit evmMarketChanged();return;
        }
        if (!configureEvmTrade(path,m_evmSeedFile)) {m_marketMessage=tr("The agreed trade could not be opened: %1").arg(tradeDir);emit evmMarketChanged();}
    };
    if (!m_dexWallets->ready()) {open({});return;}
    // The trade id is the listing id (from the folder's listing or take record)
    QString trade;
    for (const auto &name:{"/listing.json","/take.json"}) {
        QFile f(tradeDir+name);
        if (f.open(QIODevice::ReadOnly)) {trade=QJsonDocument::fromJson(f.readAll()).object().value("listing").toObject().value("listing").toObject().value("id").toString();break;}
    }
    m_dexWallets->tradeService(tradeDir,trade,[this,open,tradeDir](const QJsonObject &endpoint){
        if (endpoint.isEmpty()) {m_marketMessage=tr("The trade's swap wallet could not start: %1").arg(tradeDir);emit evmMarketChanged();return;}
        open(endpoint);
    });
}
bool DexEngine::configureBchNativeObservation(const QString &path) {return bchOperationIdle() && m_bchNativeObservation->configure(path);}
QVariantMap DexEngine::bchNativeObservationStatus() const {
    auto status=m_bchNativeObservation->status();
    if (!bchOperationIdle() || m_bchNativeProof->retainedFundingContext().isEmpty()
        || status.value("tradeId")!=m_bchNativeProof->status().value("tradeId")) status["checkAvailable"]=false;
    return status;
}
bool DexEngine::requestBchNativeObservation() {
    return bchNativeObservationStatus().value("checkAvailable").toBool()
        && m_bchNativeObservation->check(m_bchNativeProof->retainedFundingContext());
}
bool DexEngine::configureBchPublicationRepair(const QString &path) {
    return bchOperationIdle() && m_bchPublicationRepair->configure(path);
}
QVariantMap DexEngine::bchPublicationRepairStatus() const {
    auto status=m_bchPublicationRepair->status();
    if (!bchOperationIdle()) {status["reviewAvailable"]=false;status["submitAvailable"]=false;}
    return status;
}
bool DexEngine::requestBchPublicationRepairReview(const QString &stage) {
    if (!bchOperationIdle()) return false;
    m_bchSuccessPreparation->invalidate();m_bchSuccessReleaseReview->invalidate();
    m_bchSuccessSignatureReview->invalidate();m_bchSuccessSignedReleaseReview->invalidate();
    return m_bchPublicationRepair->review(stage);
}
bool DexEngine::submitBchPublicationRepair(const QString &commitment) {
    return bchOperationIdle() && m_bchPublicationRepair->submit(commitment);
}
bool DexEngine::configureBchSuccessPreparation(const QString &path) {
    return bchOperationIdle() && m_bchSuccessPreparation->configure(path);
}
QVariantMap DexEngine::bchSuccessPreparationStatus() const {
    auto status=m_bchSuccessPreparation->status();const auto proof=m_bchNativeProof->status();
    const auto release=m_bchSuccessReleaseReview->status();
    const bool proofReady=bchOperationIdle() && !m_bchNativeProof->retainedFundingContext().isEmpty()
        && status.value("tradeId")==proof.value("tradeId")
        && status.value("quoteCommitment")==proof.value("quoteCommitment");
    if (!proofReady) status["checkAvailable"]=false;
    if (!proofReady || release.value("state").toString()!="reviewed"
        || status.value("tradeId")!=release.value("tradeId")
        || status.value("quoteCommitment")!=release.value("quoteCommitment")) status["submitAvailable"]=false;
    return status;
}
bool DexEngine::requestBchSuccessPreparationCheck() {
    return bchSuccessPreparationStatus().value("checkAvailable").toBool()
        && m_bchSuccessPreparation->refresh(QJsonObject::fromVariantMap(m_bchNativeProof->status().value("review").toMap()),
            m_bchNativeProof->retainedFundingContext());
}
bool DexEngine::submitBchSuccessPreparation(const QString &commitment) {
    return bchSuccessPreparationStatus().value("submitAvailable").toBool()
        && m_bchSuccessPreparation->submit(QJsonObject::fromVariantMap(m_bchSuccessReleaseReview->status().value("review").toMap()),
            QJsonObject::fromVariantMap(m_bchNativeProof->status().value("review").toMap()),
            m_bchNativeProof->retainedFundingContext(),commitment);
}
bool DexEngine::configureBchSuccessReleaseReview(const QString &path) {
    return bchOperationIdle() && m_bchSuccessReleaseReview->configure(path);
}
QVariantMap DexEngine::bchSuccessReleaseReviewStatus() const {return successReviewStatus(m_bchSuccessReleaseReview);}
QVariantMap DexEngine::successReviewStatus(BchSuccessReleaseReviewJob *job) const {
    auto status=job->status();const auto proof=m_bchNativeProof->status();
    if (!bchOperationIdle() || m_bchNativeProof->retainedFundingContext().isEmpty()
        || status.value("tradeId")!=proof.value("tradeId")
        || status.value("quoteCommitment")!=proof.value("quoteCommitment")) status["reviewAvailable"]=false;
    return status;
}
bool DexEngine::requestBchSuccessReleaseReview() {
    return bchSuccessReleaseReviewStatus().value("reviewAvailable").toBool()
        && m_bchSuccessReleaseReview->review(QJsonObject::fromVariantMap(m_bchNativeProof->status().value("review").toMap()),
            m_bchNativeProof->retainedFundingContext());
}
bool DexEngine::configureBchSuccessSignatureReview(const QString &path) {
    return bchOperationIdle() && m_bchSuccessSignatureReview->configure(path,BchSuccessReleaseProtocol::Action::RetainSignature);
}
QVariantMap DexEngine::bchSuccessSignatureReviewStatus() const {return successReviewStatus(m_bchSuccessSignatureReview);}
bool DexEngine::requestBchSuccessSignatureReview() {
    return bchSuccessSignatureReviewStatus().value("reviewAvailable").toBool()
        && m_bchSuccessSignatureReview->review(QJsonObject::fromVariantMap(m_bchNativeProof->status().value("review").toMap()),
            m_bchNativeProof->retainedFundingContext());
}
bool DexEngine::configureBchSuccessSignedReleaseReview(const QString &path) {
    return bchOperationIdle() && m_bchSuccessSignedReleaseReview->configure(path,BchSuccessReleaseProtocol::Action::ReleaseSigned);
}
QVariantMap DexEngine::bchSuccessSignedReleaseReviewStatus() const {return successReviewStatus(m_bchSuccessSignedReleaseReview);}
bool DexEngine::requestBchSuccessSignedReleaseReview() {
    return bchSuccessSignedReleaseReviewStatus().value("reviewAvailable").toBool()
        && m_bchSuccessSignedReleaseReview->review(QJsonObject::fromVariantMap(m_bchNativeProof->status().value("review").toMap()),
            m_bchNativeProof->retainedFundingContext());
}
bool DexEngine::configureBchRefundProtection(const QString &path) {return bchOperationIdle() && m_bchRefundProtection->configure(path);}
QVariantMap DexEngine::bchRefundProtectionStatus() const {
    auto status=m_bchRefundProtection->status();
    if (!bchOperationIdle()) for (const auto key:{"reviewAvailable","checkAvailable","submitAvailable"}) status[key]=false;
    return status;
}
bool DexEngine::requestBchRefundProtectionReview() {return bchOperationIdle() && m_bchRefundProtection->review();}
bool DexEngine::submitBchRefundProtection(const QString &commitment) {return bchOperationIdle() && m_bchRefundProtection->submit(commitment);}
bool DexEngine::requestBchRefundProtectionCheck() {return bchOperationIdle() && m_bchRefundProtection->refresh();}
bool DexEngine::configureBchRecoveryRetention(const QString &path) {return bchOperationIdle() && m_bchRecoveryRetention->configure(path);}
QVariantMap DexEngine::bchRecoveryRetentionStatus() const {
    auto status=m_bchRecoveryRetention->status();
    if (!bchOperationIdle()) for (const auto key:{"reviewAvailable","checkAvailable","submitAvailable"}) status[key]=false;
    return status;
}
bool DexEngine::requestBchRecoveryRetentionReview() {return bchOperationIdle() && m_bchRecoveryRetention->review();}
bool DexEngine::submitBchRecoveryRetention(const QString &commitment) {return bchOperationIdle() && m_bchRecoveryRetention->submit(commitment);}
bool DexEngine::requestBchRecoveryRetentionCheck() {return bchOperationIdle() && m_bchRecoveryRetention->refresh();}
bool DexEngine::configureBchRefundWallet(const QString &path) {return bchOperationIdle() && m_bchRefundWallet->configure(path);}
bool DexEngine::configureBchRefundWalletReview(const QString &path) {return bchOperationIdle() && m_bchRefundWallet->configureReview(path);}
bool DexEngine::provisionBchRefundWalletService(SwapWalletRpcProcess *owner,const QString &bchRpcFile,const QString &observerRpcFile) {
    if (!bchOperationIdle() || m_bchRefundWalletOwner
        || !m_bchRefundWallet->provisionOwnedReview(owner,bchRpcFile,observerRpcFile)) return false;
    if (attachBchRefundWalletService(owner)) return true;
    m_bchRefundWallet->discardProvisionedReview();return false;
}
bool DexEngine::attachBchRefundWalletService(SwapWalletRpcProcess *owner) {
    if (!bchOperationIdle() || m_bchRefundWalletOwner || !m_bchRefundWallet->ownsConfiguredService(owner)
        || !owner->walletOperationAvailable()) return false;
    m_bchRefundWalletOwner=owner;
    connect(owner,&SwapWalletRpcProcess::availabilityChanged,this,&DexEngine::bchRefundWalletChanged,Qt::QueuedConnection);
    connect(owner,&QObject::destroyed,this,&DexEngine::bchRefundWalletChanged);
    emit bchRefundWalletChanged();return true;
}
bool DexEngine::configureBchRefundWalletService(const QString &path) {
    using namespace BchAdmissionProtocol;
    const auto trade=m_bchRefundWallet->status().value("tradeId").toString();
    if (!bchOperationIdle() || m_bchRefundWalletService || m_bchRefundWalletOwner || trade.isEmpty()
        || m_bchRefundWallet->status().value("reviewConfigured").toBool()) return false;
    QJsonObject config;quint64 daemon=0,wallet=0;
    if (!canonicalObject(LitecoinJournalProtocol::privateConfig(path),&config) || config.size()!=9
        || config.value("schema")!="monzero-private-bch-refund-wallet-service-v1"
        || config.value("tradeId")!=trade || !hex(config.value("programSha256").toString(),64)
        || !integer(config.value("daemonPort"),65535,&daemon) || !daemon
        || !integer(config.value("walletPort"),65535,&wallet) || !wallet || wallet==daemon) return false;
    const auto program=config.value("program").toString(),directory=config.value("directory").toString();
    const auto bch=config.value("bchRpcFile").toString(),observer=config.value("observerRpcFile").toString();
    for (const auto &file:{program,directory,bch,observer})
        if (!QFileInfo(file).isAbsolute() || QDir::cleanPath(file)!=file) return false;
    if (bch==observer || directory==QFileInfo(directory).path()) return false;
    // First use only: create the bound directory itself (never parents). An
    // existing directory must already be private; launch() enforces the rest.
    const QFileInfo entry(directory);
    if (!entry.exists() && !entry.isSymLink()) {
        if (!QDir().mkdir(directory)) return false;
        if (!QFile::setPermissions(directory,QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner)) return false;
    }
    m_bchRefundWalletServiceBch=bch;m_bchRefundWalletServiceObserver=observer;
    auto *service=new SwapWalletRpcProcess(this);m_bchRefundWalletService=service;
    if (!m_bchRefundWalletServiceAttach) {
        m_bchRefundWalletServiceAttach=new QTimer(this);m_bchRefundWalletServiceAttach->setInterval(250);
        connect(m_bchRefundWalletServiceAttach,&QTimer::timeout,this,[this]{
            if (!m_bchRefundWalletService || m_bchRefundWalletServiceState!="starting") {m_bchRefundWalletServiceAttach->stop();return;}
            if (provisionBchRefundWalletService(m_bchRefundWalletService,m_bchRefundWalletServiceBch,m_bchRefundWalletServiceObserver)) {
                m_bchRefundWalletServiceAttach->stop();m_bchRefundWalletServiceState="attached";emit bchRefundWalletChanged();return;
            }
            // Bounded: login file appears shortly after start. On timeout stop
            // cleanly so a later startup can resume the same bound directory.
            if (++m_bchRefundWalletServiceAttempts>=240) {
                m_bchRefundWalletServiceAttach->stop();m_bchRefundWalletServiceState="attach-failed";
                m_bchRefundWalletService->stop();emit bchRefundWalletChanged();
            }
        });
    }
    connect(service,&SwapWalletRpcProcess::processStarted,this,[this]{
        if (m_bchRefundWalletServiceState!="starting") return;
        m_bchRefundWalletServiceAttempts=0;m_bchRefundWalletServiceAttach->start();
    });
    connect(service,&SwapWalletRpcProcess::stopped,this,[this](bool clean){
        m_bchRefundWalletServiceAttach->stop();
        if (m_bchRefundWalletServiceState!="attach-failed" || !clean)
            m_bchRefundWalletServiceState=clean?"stopped":"recovery-required";
        emit bchRefundWalletChanged();
    });
    m_bchRefundWalletServiceState="starting";
    if (!service->startBoundWithDigest(program,config.value("programSha256").toString(),directory,trade,quint16(daemon),quint16(wallet))) {
        // Refused (binding mismatch, recovery-required, interrupted or foreign
        // contents). Keep files untouched and do not fall back to a new service.
        m_bchRefundWalletServiceState="start-refused";
    }
    emit bchRefundWalletChanged();return true;
}
void DexEngine::shutdownBchRefundWalletService() {
    if (!m_bchRefundWalletService) return;
    if (m_bchRefundWalletServiceAttach) m_bchRefundWalletServiceAttach->stop();
    QPointer<SwapWalletRpcProcess> service=m_bchRefundWalletService;
    QEventLoop loop;
    connect(service,&SwapWalletRpcProcess::stopped,&loop,&QEventLoop::quit);
    connect(service,&QObject::destroyed,&loop,&QEventLoop::quit);
    QTimer::singleShot(15000,&loop,&QEventLoop::quit);
    if (m_bchRefundWalletServiceState=="starting" || m_bchRefundWalletServiceState=="attached") {
        service->stop();
        if (m_bchRefundWalletServiceState=="starting" || m_bchRefundWalletServiceState=="attached") loop.exec();
    }
}
bool DexEngine::submitBchRefundWallet(const QString &commitment,QByteArray &&password) {
    if (!bchOperationIdle()) {
        if (!password.isEmpty()) sodium_memzero(password.data(),password.size());
        password.clear();return false;
    }
    return m_bchRefundWallet->submit(m_bchRefundWalletOwner,commitment,std::move(password));
}
bool DexEngine::approveBchRefundWallet(const QString &commitment,const QString &password,const QString &confirmation,bool approved) {
    const auto status=bchRefundWalletStatus();
    const auto review=status.value("review").toMap();
    const auto action=review.value("details").toMap().value("action").toString();
    if (!approved || !QStringList{"restore","open","check"}.contains(action)
        || (action=="open" && password.isEmpty())
        || !status.value("submitAvailable").toBool() || commitment!=review.value("commitment").toString()
        || (action=="restore" ? password.isEmpty() || password!=confirmation : !confirmation.isEmpty())
        || (action=="check" && !password.isEmpty())) return false;
    // QML/QString inputs cannot promise erasure; the worker consumes this UTF-8 buffer.
    auto secret=password.toUtf8();
    return submitBchRefundWallet(commitment,std::move(secret));
}
bool DexEngine::requestBchRefundWalletReview(const QString &action) {return bchOperationIdle() && m_bchRefundWallet->requestReview(action);}
bool DexEngine::requestBchRefundWalletCheck() {return bchOperationIdle() && m_bchRefundWallet->refresh();}
QVariantMap DexEngine::bchRefundWalletStatus() const {
    auto status=m_bchRefundWallet->status();
    status["serviceOwned"]=m_bchRefundWallet->ownsConfiguredService(m_bchRefundWalletOwner);
    status["serviceState"]=m_bchRefundWalletServiceState;
    status["submitAvailable"]=bchOperationIdle() && m_bchRefundWallet->canSubmit(m_bchRefundWalletOwner);
    if (!bchOperationIdle()) {status["checkAvailable"]=false;status["reviewAvailable"]=false;}return status;
}
bool DexEngine::configureBchPunishment(const QString &path) {return bchOperationIdle() && m_bchPunishment->configure(path);}
bool DexEngine::configureBchPunishmentObservation(const QString &path) {return bchOperationIdle() && m_bchPunishment->configureObservation(path);}
bool DexEngine::observeBchPunishment() {return bchOperationIdle() && m_bchPunishment->observe();}
bool DexEngine::configureBchPunishmentSubmission(const QString &path) {return bchOperationIdle() && m_bchPunishment->configureSubmission(path);}
bool DexEngine::submitBchPunishment(const QString &commitment) {return bchOperationIdle() && m_bchPunishment->submit(commitment);}
QVariantMap DexEngine::bchPunishmentStatus() const {
    auto status=m_bchPunishment->status();
    if (!bchOperationIdle()) for (const auto key:{"reviewAvailable","checkAvailable","observeAvailable","submitAvailable"}) status[key]=false;
    return status;
}
bool DexEngine::requestBchPunishmentReview() {return bchOperationIdle() && m_bchPunishment->review();}
bool DexEngine::requestBchPunishmentCheck() {return bchOperationIdle() && m_bchPunishment->refresh();}
bool DexEngine::configureBchCancellation(const QString &path) {return bchOperationIdle() && m_bchCancellation->configure(path);}
QVariantMap DexEngine::bchCancellationStatus() const {
    auto status=m_bchCancellation->status();
    if (!bchOperationIdle()) for (const auto key:{"reviewAvailable","checkAvailable","submitAvailable","observeAvailable"}) status[key]=false;
    return status;
}
bool DexEngine::requestBchCancellationReview() {return bchOperationIdle() && m_bchCancellation->review();}
bool DexEngine::submitBchCancellation(const QString &commitment) {return bchOperationIdle() && m_bchCancellation->submit(commitment);}
bool DexEngine::requestBchCancellationCheck() {return bchOperationIdle() && m_bchCancellation->refresh();}
bool DexEngine::configureBchCancellationSubmission(const QString &path) {return bchOperationIdle() && m_bchCancellation->configureSubmission(path);}
bool DexEngine::observeBchCancellation() {return bchOperationIdle() && m_bchCancellation->observe();}
bool DexEngine::configureBchRefundRelease(const QString &path) {return bchOperationIdle() && m_bchRefundRelease->configure(path);}
QVariantMap DexEngine::bchRefundReleaseStatus() const {
    auto status=m_bchRefundRelease->status();
    if (!bchOperationIdle()) for (const auto key:{"reviewAvailable","checkAvailable","submitAvailable","observeAvailable"}) status[key]=false;
    return status;
}
bool DexEngine::requestBchRefundReleaseReview() {return bchOperationIdle() && m_bchRefundRelease->review();}
bool DexEngine::submitBchRefundRelease(const QString &commitment) {return bchOperationIdle() && m_bchRefundRelease->submit(commitment);}
bool DexEngine::requestBchRefundReleaseCheck() {return bchOperationIdle() && m_bchRefundRelease->refresh();}
bool DexEngine::configureBchRefundReleaseSubmission(const QString &path) {return bchOperationIdle() && m_bchRefundRelease->configureSubmission(path);}
bool DexEngine::observeBchRefundRelease() {return bchOperationIdle() && m_bchRefundRelease->observe();}
bool DexEngine::configureBchAdmission(const QString &path) {return m_bchAdmission->configure(path);}
QVariantMap DexEngine::bchAdmissionStatus() const {
    auto status=m_bchAdmission->status();
    if (!bchOperationIdle()) { status["reviewAvailable"]=false;status["checkAvailable"]=false;status["submitAvailable"]=false; }
    return status;
}
bool DexEngine::requestBchAdmissionReview() {return bchOperationIdle() && m_bchAdmission->review();}
bool DexEngine::submitBchAdmission(const QString &commitment) {return bchOperationIdle() && m_bchAdmission->submit(commitment);}
bool DexEngine::requestBchAdmissionCheck() {return bchOperationIdle() && m_bchAdmission->refresh();}

bool DexEngine::attachLitecoinNegotiation(LitecoinNegotiationSession *session) {
    if (!session || m_ltcNegotiation || session->thread()!=thread() || !session->status().value("configured").toBool()) return false;
    m_ltcNegotiation=session;
    connect(session,&LitecoinNegotiationSession::changed,this,&DexEngine::litecoinNegotiationChanged);
    connect(session,&QObject::destroyed,this,&DexEngine::litecoinNegotiationChanged);
    emit litecoinNegotiationChanged();return true;
}
bool DexEngine::configureLitecoinReservation(const QString &path,const QString &journalPath) {return m_ltcReservation->configure(path,journalPath);}
QVariantMap DexEngine::litecoinReservationStatus() const {
    auto status=m_ltcReservation->status();const auto peer=litecoinNegotiationStatus();
    status.insert("canPrepare",status.value("reservationConfigured").toBool() && !status.value("busy").toBool()
        && status.value("tradeId")==peer.value("tradeId") && status.value("quoteCommitment")==peer.value("quoteCommitment")
        && peer.value("complete").toBool() && peer.value("transportAuthenticated").toBool()
        && !peer.value("busy").toBool() && !peer.value("recoveryStarted").toBool() && peer.value("role")=="litecoin-funder");
    return litecoinAppView(status);
}
bool DexEngine::checkLitecoinRecoverySpend(const QString &operation) {return litecoinOperationAllowed() && m_ltcReservation->checkRecovery(operation);}
bool DexEngine::reviewLitecoinRecoverySpend(const QString &operation) {return litecoinOperationAllowed() && m_ltcReservation->reviewRecovery(operation);}
bool DexEngine::submitLitecoinRecoverySpend(const QString &commitment) {return litecoinOperationAllowed() && m_ltcReservation->submitRecovery(commitment);}
bool DexEngine::reviewLitecoinLockFunding() {return !litecoinLightFundingSelected() && litecoinOperationAllowed() && m_ltcReservation->reviewFunding();}
bool DexEngine::submitLitecoinLockFunding(const QString &commitment) {return !litecoinLightFundingSelected() && litecoinOperationAllowed() && m_ltcReservation->submitFunding(commitment);}
bool DexEngine::prepareLitecoinReservation() {return !litecoinLightFundingSelected() && litecoinOperationAllowed() && litecoinReservationStatus().value("canPrepare").toBool() && m_ltcReservation->prepare();}
bool DexEngine::startLitecoinRecovery(const QByteArray &psbt) { return litecoinOperationAllowed() && m_ltcNegotiation && m_ltcNegotiation->startRecovery(psbt); }
void DexEngine::retryLitecoinRecoveryStart(int attemptsLeft) {
    if (m_ltcRecoveryPending.isEmpty()) return;
    const bool started=m_ltcNegotiation && m_ltcNegotiation->status().value("recoveryStarted").toBool();
    if (started || (litecoinLightFundingAllowed() && startLitecoinRecovery(m_ltcRecoveryPending))) {m_ltcRecoveryPending.clear();return;}
    if (attemptsLeft<=0) {
        m_ltcRecoveryPending.clear();
        qWarning("DEX: the recovery exchange did not start after funding was prepared; use \"Resume saved recovery exchange\"");
        return;
    }
    QTimer::singleShot(3000,this,[this,attemptsLeft]{retryLitecoinRecoveryStart(attemptsLeft-1);});
}
QVariantMap DexEngine::litecoinJournalStatus() const { return litecoinAppView(m_ltcJournal->status()); }
bool DexEngine::configureLitecoinJournal(const QString &path) {
    if (!m_ltcJournal->configure(path)) return false;
    // Funding configurations also own the completed Litecoin redemption phase.
    if (m_ltcJournal->status().value("fundingSubmissionConfigured").toBool()) return m_ltcRedemption->configure(path);
    return true;
}
QVariantMap DexEngine::litecoinRedemptionStatus() const {return litecoinAppView(m_ltcRedemption->status());}
bool DexEngine::configureLitecoinRedemption(const QString &path) {return m_ltcRedemption->configure(path);}
bool DexEngine::reviewLitecoinRedemptionTransaction() {return litecoinOperationAllowed() && m_ltcRedemption->review();}
bool DexEngine::submitLitecoinRedemptionTransaction(const QString &commitment) {return litecoinOperationAllowed() && m_ltcRedemption->submit(commitment);}
bool DexEngine::checkLitecoinRedemptionTransaction() {return litecoinOperationAllowed() && m_ltcRedemption->check();}
bool DexEngine::requestLitecoinStartReview() { return litecoinOperationAllowed() && litecoinStartRefusal().isEmpty() && m_ltcJournal->reviewStart(); }
QString DexEngine::litecoinStartRefusal() const {
    const auto terms=m_ltcJournal->startTerms();
    if (terms.isEmpty()) return {};
    return m_riskControl->refusal(m_ltcJournal->tradeId(),terms.value("ltcAtoms").toString().toULongLong(),
        terms.value("xmzAtoms").toString().toULongLong());
}
bool DexEngine::submitLitecoinStart(const QString &commitment) {
    // Count the trade before dispatch: a crash after recording only makes the
    // limit stricter, never looser.
    const auto terms=m_ltcJournal->startTerms();
    if (!litecoinOperationAllowed() || terms.isEmpty() || !litecoinStartRefusal().isEmpty()
        || !m_riskControl->recordStart(m_ltcJournal->tradeId(),terms.value("ltcAtoms").toString().toULongLong())) return false;
    return m_ltcJournal->submitStart(commitment);
}
QVariantMap DexEngine::dexRiskStatus() const { return m_riskControl->status(); }
bool DexEngine::requestLitecoinJournalCheck() { return litecoinOperationAllowed() && m_ltcJournal->refresh(); }
bool DexEngine::requestLitecoinRefundReview(bool retransmit) { return litecoinOperationAllowed() && litecoinPaymentJob()->reviewRefund(retransmit); }
bool DexEngine::restoreLitecoinRecoveredWallet() { return litecoinOperationAllowed() && litecoinPaymentJob()->restoreRecoveredWallet(); }
bool DexEngine::openLitecoinRecoveredWallet() { return litecoinOperationAllowed() && litecoinPaymentJob()->openRecoveredWallet(); }
bool DexEngine::checkLitecoinRecoveredWallet() { return litecoinOperationAllowed() && litecoinPaymentJob()->checkRecoveredWallet(); }
bool DexEngine::prepareLitecoinPayout() { return litecoinOperationAllowed() && litecoinPaymentJob()->preparePayout(); }
bool DexEngine::prepareLitecoinRefund() { return litecoinOperationAllowed() && litecoinPaymentJob()->prepareRefund(); }
bool DexEngine::prepareLitecoinFunding() { return litecoinOperationAllowed() && litecoinFundingJob()->prepareFunding(); }
bool DexEngine::requestLitecoinFundingReview(bool retransmit) { return litecoinOperationAllowed() && litecoinFundingJob()->reviewFunding(retransmit); }
bool DexEngine::requestLitecoinPayoutReview(bool retransmit) { return litecoinOperationAllowed() && litecoinPaymentJob()->reviewPayout(retransmit); }
bool DexEngine::checkLitecoinFunding() { return litecoinOperationAllowed() && litecoinFundingJob()->checkFunding(); }
bool DexEngine::submitLitecoinFunding(const QString &commitment) { return litecoinOperationAllowed() && litecoinFundingJob()->submitFunding(commitment); }
bool DexEngine::submitLitecoinRefund(const QString &commitment) { return litecoinOperationAllowed() && litecoinPaymentJob()->submitRefund(commitment); }
bool DexEngine::submitLitecoinPayout(const QString &commitment) { return litecoinOperationAllowed() && litecoinPaymentJob()->submitPayout(commitment); }
bool DexEngine::checkLitecoinPayout() { return litecoinOperationAllowed() && litecoinPaymentJob()->checkPayout(); }
bool DexEngine::checkLitecoinRefund() { return litecoinOperationAllowed() && litecoinPaymentJob()->checkRefund(); }

QVariantMap DexEngine::nativeFeePreview() const {
    if (m_walletSession) return m_walletSession->preview();
    if (m_sessionWasAttached) return {{"valid",false},{"executable",false},{"transportAuthenticated",false}};
    return m_feePreviewController->preview();
}

QVariantMap DexEngine::nativeWalletStatus() const {
    QString state="unattached";
    if (m_walletSession) {
        switch (m_walletSession->state()) {
        case SwapWalletSession::Unknown: state="unknown"; break;
        case SwapWalletSession::Closed: state="closed"; break;
        case SwapWalletSession::Open: state="open"; break;
        case SwapWalletSession::Busy: state="busy"; break;
        case SwapWalletSession::RecoveryRequired: state="recovery-required"; break;
        case SwapWalletSession::Stopped: state="stopped"; break;
        }
    }
    return {{"attached",!m_walletSession.isNull()},{"state",state},
        {"walletIdentityVerified",state=="open"},{"readyForTrading",false}};
}

QVariantMap DexEngine::nativeRecoveryStatus() const {
    if (m_walletSession) return m_walletSession->recovery();
    return {{"valid",false},{"status","unresolved"},{"retryAllowed",false},
        {"executable",false},{"transportAuthenticated",false}};
}

bool DexEngine::cancelNativeWalletOperation() {
    return m_walletSession && m_walletSession->cancelPendingOperation();
}

bool DexEngine::requestNativeRecoveryCheck() {
    return m_walletSession && m_walletSession->requestPinnedRecovery();
}

QVariantMap DexEngine::nativeQuoteReview() const {
    return m_quoteCoordinator ? m_quoteCoordinator->review() : QVariantMap{};
}
QVariantMap DexEngine::nativeQuoteStatus() const {
    return m_quoteCoordinator ? m_quoteCoordinator->status()
        : QVariantMap{{"state", "unattached"}, {"executable", false}};
}
bool DexEngine::attachNativeQuoteCoordinator(NativeQuoteCoordinator *coordinator) {
    if (!coordinator || coordinator->thread() != thread() || m_quoteCoordinator) return false;
    m_quoteCoordinator = coordinator;
    connect(coordinator, &NativeQuoteCoordinator::changed, this, &DexEngine::nativeQuoteReviewChanged);
    connect(coordinator, &QObject::destroyed, this, [this] {
        m_quoteCoordinator = nullptr;
        emit nativeQuoteReviewChanged();
    });
    emit nativeQuoteReviewChanged();
    return true;
}
bool DexEngine::approveNativeQuote(const QString &commitmentHex) {
    static const QRegularExpression hash("^[0-9a-f]{64}\\z");
    return m_quoteCoordinator && hash.match(commitmentHex).hasMatch()
        && m_quoteCoordinator->approve(QByteArray::fromHex(commitmentHex.toLatin1()));
}

bool DexEngine::requestNativeFeePreview(const QString &destination,const QString &amountXmz) {
    if (!m_walletSession || m_walletSession->state()!=SwapWalletSession::Open
        || destination.size()>110 || amountXmz.size()>64) return false;
    const QString address=destination.trimmed();
    // Shape check only; the native helper verifies checksum/network before construction.
    static const QRegularExpression addressShape("^[1-9A-HJ-NP-Za-km-z]{90,110}$");
    quint64 amount=0;
    if (!addressShape.match(address).hasMatch() || !parseAtomic(amountXmz,11,amount) || amount==0) return false;
    return m_walletSession->estimateFee(address,QString::number(amount));
}

bool DexEngine::attachSwapWalletSession(SwapWalletSession *session) {
    if (!session || session->thread()!=thread() || (m_walletSession && m_walletSession!=session)) return false;
    if (m_walletSession==session) return true;
    m_walletSession=session;
    m_sessionWasAttached=true;
    connect(session,&SwapWalletSession::stateChanged,this,&DexEngine::nativeWalletStatusChanged);
    connect(session,&SwapWalletSession::previewChanged,this,&DexEngine::nativeFeePreviewChanged);
    connect(session,&SwapWalletSession::recoveryChanged,this,&DexEngine::nativeRecoveryStatusChanged);
    connect(session,&QObject::destroyed,this,[this] {
        m_walletSession=nullptr;
        QPointer<DexEngine> guard(this);
        emit nativeWalletStatusChanged();
        if (guard) emit nativeFeePreviewChanged();
        if (guard) emit nativeRecoveryStatusChanged();
    });
    QPointer<DexEngine> guard(this);
    m_feePreviewController->cancel();
    if (!guard) return false;
    emit nativeWalletStatusChanged();
    if (!guard) return false;
    emit nativeFeePreviewChanged();
    if (!guard) return false;
    emit nativeRecoveryStatusChanged();
    return true;
}

bool DexEngine::refreshResearchMarket(const QString &path)
{
    if (m_researchBusy) return false;
    m_researchBusy = true;
    // The worker owns only a path copy, never this engine or a live writer.
    m_researchWatcher->setFuture(QtConcurrent::run([path] {
        return OfferBookStore::readSnapshot(path, "monzero-dex-offer-v1",
            "isolated-btc-xmz", QDateTime::currentSecsSinceEpoch());
    }));
    emit researchMarketBusyChanged();
    return true;
}

QVariantMap DexEngine::readResearchMarket(const QString &path) const
{
    return OfferBookStore::readSnapshot(path, "monzero-dex-offer-v1",
        "isolated-btc-xmz", QDateTime::currentSecsSinceEpoch());
}

bool DexEngine::parseAtomic(const QString &input, int decimals, quint64 &value)
{
    const QString text = input.trimmed();
    const QRegularExpression pattern(QStringLiteral("^(0|[1-9][0-9]*)(?:\\.([0-9]{1,%1}))?$").arg(decimals));
    const auto match = pattern.match(text);
    if (!match.hasMatch()) return false;

    QString fraction = match.captured(2);
    fraction += QString(decimals - fraction.size(), QLatin1Char('0'));
    bool wholeOk = false, fractionOk = false;
    const quint64 whole = match.captured(1).toULongLong(&wholeOk);
    const quint64 fractional = fraction.isEmpty() ? 0 : fraction.toULongLong(&fractionOk);
    if (!wholeOk || (!fraction.isEmpty() && !fractionOk)) return false;

    quint64 scale = 1;
    for (int i = 0; i < decimals; ++i) scale *= 10;
    if (whole > (std::numeric_limits<quint64>::max() - fractional) / scale) return false;
    value = whole * scale + fractional;
    return value != 0;
}

QString DexEngine::formatAtomic(quint64 value, int decimals)
{
    quint64 scale = 1;
    for (int i = 0; i < decimals; ++i) scale *= 10;
    QString fraction = QString::number(value % scale).rightJustified(decimals, QLatin1Char('0'));
    while (fraction.endsWith(QLatin1Char('0'))) fraction.chop(1);
    return QString::number(value / scale) + (fraction.isEmpty() ? QString() : QStringLiteral(".") + fraction);
}

QStringList DexEngine::supportedPairs() const
{
    QStringList pairs;
    for (const auto &market : Dex::markets()) pairs.append(QString::fromLatin1(market.symbol) + " / XMZ");
    return pairs;
}

QVariantMap DexEngine::marketInfo(const QString &pair) const
{
    const auto *market = Dex::findMarket(pair);
    if (!market) return {{"valid", false}, {"executable", false},
        {"settlementStatus", "unsupported"}, {"settlementBlocker", "Unknown market"}};
    return {{"valid", true}, {"symbol", market->symbol}, {"decimals", market->decimals},
        {"chainFamily", market->family}, {"executable", false},
        {"settlementStatus", market->status}, {"settlementBlocker", market->blocker}};
}

QVariantMap DexEngine::previewOrder(const QString &pair, const QString &side,
                                    const QString &amount, const QString &price,
                                    const QString &externalMinerFee) const
{
    QVariantMap result{{QStringLiteral("valid"), false}, {"executable", false}};
    const auto *market = Dex::findMarket(pair);
    if (!market) { result["error"] = QStringLiteral("Unsupported or disabled market"); return result; }
    if (side != QLatin1String("BUY") && side != QLatin1String("SELL")) { result["error"] = QStringLiteral("Invalid order side"); return result; }

    // Bound parsing before arithmetic; ETH wei must not be narrowed to uint64.
    const QString text = amount.trimmed();
    const QRegularExpression shape(QString("^(0|[1-9][0-9]{0,77})(?:\\.([0-9]{1,%1}))?\\z").arg(market->decimals));
    const auto match = text.size() <= 100 ? shape.match(text) : QRegularExpressionMatch{};
    if (!match.hasMatch()) { result["error"] = QString("Amount must be positive with at most %1 decimals").arg(market->decimals); return result; }
    using boost::multiprecision::cpp_int;
    QString fraction = match.captured(2);
    fraction += QString(market->decimals - fraction.size(), QLatin1Char('0'));
    cpp_int baseAtomic = 0, baseScale = 1;
    for (const QChar ch : match.captured(1) + fraction) baseAtomic = baseAtomic * 10 + ch.digitValue();
    for (int i = 0; i < market->decimals; ++i) baseScale *= 10;
    const cpp_int maximum = (cpp_int(1) << market->atomicBits) - 1;
    if (baseAtomic == 0 || baseAtomic > maximum) { result["error"] = "Amount is zero or exceeds the chain atomic range"; return result; }
    quint64 priceAtomic = 0;
    if (!parseAtomic(price, xmz_decimals, priceAtomic)) { result["error"] = QStringLiteral("Price must be positive with at most 11 decimals"); return result; }

    const cpp_int grossWide = baseAtomic * priceAtomic / baseScale;
    if (grossWide > std::numeric_limits<quint64>::max()) { result["error"] = QStringLiteral("Order total is too large"); return result; }
    const quint64 gross = static_cast<quint64>(grossWide);
    const quint64 protocolFee = static_cast<quint64>((static_cast<unsigned __int128>(gross) * protocol_fee_bps + 9999) / 10000);
    if (gross <= protocolFee + xmz_network_fee || gross - protocolFee - xmz_network_fee < minimum_net_xmz) {
        result["error"] = QStringLiteral("Trade is below the 1 XMZ minimum after fees"); return result;
    }

    if (side == "BUY" && gross > std::numeric_limits<quint64>::max() - protocolFee - xmz_network_fee) {
        result["error"] = "Order debit is too large"; return result;
    }
    result["valid"] = true;
    result["executable"] = false;
    result["feeEstimateVerified"] = false;
    result["feeEstimateSource"] = QStringLiteral("fixed-research-placeholder");
    result["settlementLabel"] = side == "BUY" ? "Estimated XMZ debit" : "Estimated XMZ proceeds";
    result["settlementXmz"] = formatAtomic(side == "BUY" ? gross + protocolFee + xmz_network_fee
        : gross - protocolFee - xmz_network_fee, xmz_decimals);
    result["pair"] = pair;
    result["side"] = side;
    while (fraction.endsWith('0')) fraction.chop(1);
    result["amount"] = match.captured(1) + (fraction.isEmpty() ? QString() : "." + fraction);
    result["amountAtomic"] = QString::fromStdString(baseAtomic.str());
    result["baseDecimals"] = market->decimals;
    result["settlementStatus"] = market->status;
    result["settlementBlocker"] = market->blocker;
    result["grossXmz"] = formatAtomic(gross, xmz_decimals);
    result["grossRounding"] = "down-to-xmz-atom";
    result["protocolFeeXmz"] = formatAtomic(protocolFee, xmz_decimals);
    result["networkFeeXmz"] = formatAtomic(xmz_network_fee, xmz_decimals);
    result["netXmz"] = formatAtomic(gross - protocolFee - xmz_network_fee, xmz_decimals);
    result["externalMinerFee"] = externalMinerFee;
    result["state"] = QStringLiteral("draft");
    return result;
}

bool DexEngine::canTransition(const QString &from, const QString &to) const
{
    static const QHash<QString, QStringList> transitions{
        {"draft", {"quoted", "cancelled"}}, {"quoted", {"matched", "expired", "cancelled"}},
        {"matched", {"xmz_locked", "expired", "refunding"}}, {"xmz_locked", {"external_locked", "refunding"}},
        {"external_locked", {"redeemable", "refunding"}}, {"redeemable", {"settled", "refunding"}},
        {"refunding", {"refunded"}}
    };
    return transitions.value(from).contains(to);
}

QVariantMap DexEngine::selfTest() const
{
    QStringList failures;
    int checks = 0;
    const auto expect = [&](bool condition, const QString &message) {
        ++checks;
        if (!condition) failures.append(message);
    };

    const QVariantMap valid = previewOrder("BTC / XMZ", "BUY", "0.001", "1172500", "0.000020 BTC");
    expect(valid.value("valid").toBool(), "valid BTC order rejected");
    expect(valid.value("grossXmz").toString() == "1172.5", "gross atomic calculation mismatch");
    expect(valid.value("protocolFeeXmz").toString() == "0", "protocol fee mismatch");
    expect(valid.value("networkFeeXmz").toString() == "0.0025", "network fee mismatch");
    expect(valid.value("netXmz").toString() == "1172.4975", "net proceeds mismatch");
    expect(!previewOrder("UNKNOWN / XMZ", "BUY", "1", "100", "unknown").value("valid").toBool(), "unsupported pair accepted");
    expect(!previewOrder("BTC / XMZ", "HOLD", "1", "1", "0 BTC").value("valid").toBool(), "invalid side accepted");
    expect(!previewOrder("BTC / XMZ", "BUY", "0.000000001", "1", "0 BTC").value("valid").toBool(), "over-precision amount accepted");
    expect(!previewOrder("BTC / XMZ", "BUY", "0.00000001", "1", "0 BTC").value("valid").toBool(), "below-minimum order accepted");
    expect(canTransition("draft", "quoted"), "valid draft transition rejected");
    expect(canTransition("redeemable", "settled"), "valid settlement transition rejected");
    expect(canTransition("refunding", "refunded"), "valid refund transition rejected");
    expect(!canTransition("draft", "settled"), "unsafe state jump accepted");
    expect(!canTransition("refunded", "settled"), "terminal state transition accepted");

    return {{"passed", failures.isEmpty()}, {"checks", checks}, {"failures", failures}};
}

DexEngine::~DexEngine() {
    // Child jobs may finish an owned operation during destruction.
    // Stop forwarding owner notifications before those jobs are torn down.
    if (m_bchRefundWalletOwner)
        disconnect(m_bchRefundWalletOwner, nullptr, this, nullptr);
}

QVariantMap DexEngine::createLocalOffer(const QString &pair, const QString &side,
    const QString &amount, const QString &price, int lifetimeSeconds)
{
    if (lifetimeSeconds < 60 || lifetimeSeconds > 86400)
        return {{"valid", false}, {"error", "Lifetime must be between 60 and 86400 seconds"}};
    auto result = previewOrder(pair, side, amount, price, "Not estimated");
    if (!result.value("valid").toBool()) return result;
    m_localOffers = localOffers();
    if (m_localOffers.size() >= 100)
        return {{"valid", false}, {"error", "Local offer limit reached"}};
    result["id"] = QUuid::createUuid().toString(QUuid::WithoutBraces);
    result["price"] = price;
    result["expiresAt"] = QDateTime::currentSecsSinceEpoch() + lifetimeSeconds;
    result["state"] = "local_unsigned";
    result["executable"] = false;
    m_localOffers.append(result);
    emit offerCreated(result.value("id").toString(), pair);
    return result;
}
QVariantList DexEngine::localOffers() const
{
    QVariantList result;
    for (const auto &offer : m_localOffers)
        if (offer.toMap().value("expiresAt").toLongLong() > QDateTime::currentSecsSinceEpoch())
            result.append(offer);
    return result;
}
bool DexEngine::cancelLocalOffer(const QString &id)
{
    m_localOffers = localOffers();
    for (int i = 0; i < m_localOffers.size(); ++i)
        if (m_localOffers[i].toMap().value("id").toString() == id) {
            m_localOffers.removeAt(i);
            emit offerCancelled(id);
            return true;
        }
    return false;
}
// Identity primitives alone do not implement validated network offers or settlement.
bool DexEngine::signLocalOffer(const QString &) { return false; }
bool DexEngine::publishOffer(const QString &) { return false; }
bool DexEngine::validateOffer(const QVariantMap &) const { return false; }
bool DexEngine::matchOffers(const QString &, const QString &) { return false; }
bool DexEngine::canTransition(OfferState from, OfferState to) const
{
    const QStringList names{"draft", "quoted", "matched", "xmz_locked", "external_locked",
        "redeemable", "settled", "refunding", "refunded", "expired", "cancelled"};
    return canTransition(names.value(static_cast<int>(from)), names.value(static_cast<int>(to)));
}
QVariantMap DexEngine::activationReadiness(const QString &pair, const QVariantMap &) const
{
    return {{"ready", false}, {"checksPassed", 0}, {"checksRequired", 2},
        {"feeEnforcementVerified", false},
        {"blockers", QStringList{marketInfo(pair).value("settlementBlocker").toString(),
            "Market-specific settlement, recovery and release validation remain incomplete (initial app trading fee: zero)"}}};
}
QVariantMap DexEngine::fundingPlan(const QString &payCoin, const QString &receiveCoin,
    const QString &unlockedXmz, const QString &protocolFeeXmz,
    const QString &networkFeeXmz, const QString &xmzPrincipal) const
{
    QVariantMap result{{"valid", false}, {"funded", false}, {"executable", false},
        {"balanceVerified", false}, {"feeCurrency", "XMZ"}};
    const auto reject = [&](const QString &reason) {
        result["error"] = reason;
        return result;
    };
    const QStringList assets{"XMZ", "BTC", "LTC", "BCH"};
    if (!assets.contains(payCoin) || !assets.contains(receiveCoin) || payCoin == receiveCoin)
        return reject("Unsupported or identical assets");
    quint64 balance = 0, protocolFee = 0, networkFee = 0, principal = 0;
    if (!DexUtils::parseAtomic(unlockedXmz, 11, balance)
        || !DexUtils::parseAtomic(protocolFeeXmz, 11, protocolFee)
        || !DexUtils::parseAtomic(networkFeeXmz, 11, networkFee)
        || !DexUtils::parseAtomic(xmzPrincipal, 11, principal))
        return reject("Invalid XMZ amount or precision");
    if (!protocolFee || !networkFee)
        return reject("A positive quoted protocol fee and network fee are required");
    const bool acquiring = receiveCoin == "XMZ";
    const bool spending = payCoin == "XMZ";
    if ((acquiring || spending) != (principal > 0))
        return reject("XMZ principal must be positive exactly when trading XMZ");
    const quint64 maximum = std::numeric_limits<quint64>::max();
    if (protocolFee > maximum - networkFee) return reject("Fee total overflow");
    const quint64 fees = protocolFee + networkFee;
    quint64 required = fees, net = 0;
    if (acquiring) {
        if (principal <= fees || principal - fees < minimum_net_xmz)
            return reject("Acquisition must leave at least 1 XMZ after fees");
        net = principal - fees;
        required = 0;
    } else if (spending) {
        if (principal > maximum - fees) return reject("Funding requirement overflow");
        required += principal;
    }
    result["valid"] = true;
    // This is arithmetic over caller-supplied values, never proof of wallet funds.
    result["funded"] = balance >= required;
    result["requiredUnlockedXmz"] = formatAtomic(required, 11);
    result["shortfallXmz"] = formatAtomic(balance >= required ? 0 : required - balance, 11);
    result["netReceiveXmz"] = formatAtomic(net, 11);
    result["feeSource"] = acquiring ? "deduct_from_received_xmz" : "unlocked_xmz";
    result["nextAction"] = balance >= required ? "verify_quote_and_wallet_funds" : "buy_xmz_first";
    result["executionBlocker"] = "Unauthenticated quote and balance; settlement and recovery unavailable";
    return result;
}
