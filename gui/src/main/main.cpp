// Copyright (c) 2014-2024, The Monero Project
//
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without modification, are
// permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice, this list of
//    conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright notice, this list
//    of conditions and the following disclaimer in the documentation and/or other
//    materials provided with the distribution.
//
// 3. Neither the name of the copyright holder nor the names of its contributors may be
//    used to endorse or promote products derived from this software without specific
//    prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
// THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
// STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
// THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include <QApplication>
#include <QFontDatabase>
#include <QQmlApplicationEngine>
#include <QtQml>
#include <QStandardPaths>
#include <QIcon>
#include <QDebug>
#include <QDesktopServices>
#include <QObject>
#include <QDesktopWidget>
#include <QScreen>
#include <QThread>

#include <version.h>

#include "clipboardAdapter.h"
#include "filter.h"
#include "oscursor.h"
#include "oshelper.h"
#include "WalletManager.h"
#include "Wallet.h"
#include "QRCodeImageProvider.h"
#include "PendingTransaction.h"
#include "UnsignedTransaction.h"
#include "TranslationManager.h"
#include "TransactionInfo.h"
#include "TransactionHistory.h"
#include "model/TransactionHistoryModel.h"
#include "model/TransactionHistorySortFilterModel.h"
#include "model/LanguageModel.h"
#include "AddressBook.h"
#include "model/AddressBookModel.h"
#include "Subaddress.h"
#include "model/SubaddressModel.h"
#include "SubaddressAccount.h"
#include "model/SubaddressAccountModel.h"
#include "Logger.h"
#include "MainApp.h"
#include "qt/downloader.h"
#include "qt/ipc.h"
#include "qt/network.h"
#include "qt/utils.h"
#include "qt/TailsOS.h"
#include "qt/KeysFiles.h"
#include "qt/MoneroSettings.h"
#include "qt/NetworkAccessBlockingFactory.h"
#ifdef WITH_DEX_PROTOTYPE
#include "dex/DexEngine.h"
#include "dex/DexDeployment.h"
#include "dex/DexRiskControl.h"
#include "dex/DexTradeFolders.h"
#include "LightWalletHelper.h"
#include "dex/LitecoinPeerStartup.h"
#include "dex/PrivateBackendConfig.h"
#include "dex/NativeQuoteConfig.h"
#include "dex/NativeQuoteCoordinator.h"
#include "dex/SwapWalletSession.h"
#include <memory>
#endif
#ifdef QML_TESTS
#include "QmlTestHarness.h"
#endif
#ifdef Q_OS_MAC
#include "qt/macoshelper.h"
#endif

// IOS exclusions
#ifndef Q_OS_IOS
#include "daemon/DaemonManager.h"
#include "p2pool/P2PoolManager.h"
#endif

#if defined(Q_OS_WIN)
#include <QOpenGLContext>
#elif defined(Q_OS_MACOS)
#include "qt/macoshelper.h"
#endif

#ifdef WITH_SCANNER
#include "QR-Code-scanner/QrCodeScanner.h"
#endif

#ifdef MONERO_GUI_STATIC

#include <QtPlugin>
#if defined(Q_OS_OSX)
  Q_IMPORT_PLUGIN(QCocoaIntegrationPlugin);
#elif defined(Q_OS_WIN)
  Q_IMPORT_PLUGIN(QWindowsIntegrationPlugin);
#elif defined(Q_OS_LINUX)
  Q_IMPORT_PLUGIN(QXcbIntegrationPlugin);
  Q_IMPORT_PLUGIN(QXcbGlxIntegrationPlugin);
#endif
Q_IMPORT_PLUGIN(QSvgIconPlugin)
Q_IMPORT_PLUGIN(QICNSPlugin)
Q_IMPORT_PLUGIN(QICOPlugin)
Q_IMPORT_PLUGIN(QJpegPlugin)
Q_IMPORT_PLUGIN(QSvgPlugin)
Q_IMPORT_PLUGIN(QTgaPlugin)
Q_IMPORT_PLUGIN(QTiffPlugin)
Q_IMPORT_PLUGIN(QWbmpPlugin)
Q_IMPORT_PLUGIN(QWebpPlugin)
Q_IMPORT_PLUGIN(QQmlDebuggerServiceFactory)
Q_IMPORT_PLUGIN(QQmlInspectorServiceFactory)
Q_IMPORT_PLUGIN(QLocalClientConnectionFactory)
Q_IMPORT_PLUGIN(QDebugMessageServiceFactory)
Q_IMPORT_PLUGIN(QQmlNativeDebugConnectorFactory)
Q_IMPORT_PLUGIN(QQmlNativeDebugServiceFactory)
Q_IMPORT_PLUGIN(QQmlProfilerServiceFactory)
Q_IMPORT_PLUGIN(QQuickProfilerAdapterFactory)
Q_IMPORT_PLUGIN(QQmlDebugServerFactory)
Q_IMPORT_PLUGIN(QTcpServerConnectionFactory)
Q_IMPORT_PLUGIN(QGenericEnginePlugin)

#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
Q_IMPORT_PLUGIN(QtQmlPlugin)
#endif
Q_IMPORT_PLUGIN(QtQmlModelsPlugin)
Q_IMPORT_PLUGIN(QtQuick2Plugin)
Q_IMPORT_PLUGIN(QtQuickLayoutsPlugin)
Q_IMPORT_PLUGIN(QtGraphicalEffectsPlugin)
Q_IMPORT_PLUGIN(QtGraphicalEffectsPrivatePlugin)
Q_IMPORT_PLUGIN(QtQuick2WindowPlugin)
Q_IMPORT_PLUGIN(QtQuickControls1Plugin)
Q_IMPORT_PLUGIN(QtQuick2DialogsPlugin)
Q_IMPORT_PLUGIN(QmlFolderListModelPlugin)
Q_IMPORT_PLUGIN(QmlSettingsPlugin)
Q_IMPORT_PLUGIN(QtLabsPlatformPlugin)
Q_IMPORT_PLUGIN(QtQuick2DialogsPrivatePlugin)
Q_IMPORT_PLUGIN(QtQuick2PrivateWidgetsPlugin)
Q_IMPORT_PLUGIN(QtQuickControls2Plugin)
Q_IMPORT_PLUGIN(QtQuickTemplates2Plugin)
#ifdef WITH_SCANNER
Q_IMPORT_PLUGIN(QMultimediaDeclarativeModule)
#endif

#endif

bool isIOS = false;
bool isAndroid = false;
bool isWindows = false;
bool isMac = false;
bool isLinux = false;
bool isTails = false;
bool isDesktop = false;
bool isOpenGL = true;
bool isARM = false;

int main(int argc, char *argv[])
{
    Q_INIT_RESOURCE(translations);

#ifdef QML_TESTS
    int qmlTestResult;
    if (runQmlTestsIfRequested(argc, argv, qmlTestResult))
        return qmlTestResult;
#endif

    // platform dependant settings
#if !defined(Q_OS_ANDROID) && !defined(Q_OS_IOS)
    bool isDesktop = true;
#elif defined(Q_OS_ANDROID)
    bool isAndroid = true;
#elif defined(Q_OS_IOS)
    bool isIOS = true;
#endif
#ifdef Q_OS_WIN
    bool isWindows = true;
#elif defined(Q_OS_LINUX)
    bool isLinux = true;
    bool isTails = TailsOS::detect();
#elif defined(Q_OS_MAC)
    bool isMac = true;
#endif
#if defined(__aarch64__)
    bool isARM = true;
#endif

    // detect low graphics mode (start-low-graphics-mode.bat)
    if (qgetenv("QMLSCENE_DEVICE") == "softwarecontext"
        || qgetenv("QT_QUICK_BACKEND") == "software")
        isOpenGL = false;

#ifdef Q_OS_MAC
    // macOS window tabbing is not supported
    MacOSHelper::disableWindowTabbing();
#endif
    // disable "QApplication: invalid style override passed" warning
    if (isDesktop) qputenv("QT_STYLE_OVERRIDE", "fusion");
#ifdef Q_OS_LINUX
    // platform xcb by default
    if (isDesktop && qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) qputenv("QT_QPA_PLATFORM", "xcb");
#endif

    // enable High DPI scaling
    qputenv("QT_ENABLE_HIGHDPI_SCALING", "1");

    // Turn off colors in monerod log output.
    qputenv("TERM", "goaway");

#if defined(Q_OS_MACOS)
    QDir::setCurrent(QDir(MacOSHelper::bundlePath() + QDir::separator() + "..").canonicalPath());
#endif

    qputenv("QML_DISABLE_DISK_CACHE", "1");

    for (int i = 0; i < argc; i++) {
        if (QString(argv[i]).contains("platformpluginpath")) {
            qCritical() << "Setting -platformpluginpath as an argument is disabled"; // CVE-2021-3401
            return 1;
        }
    }

    MainApp app(argc, argv);

    // Load the bundled outline fonts before any QML/control is constructed.
    // Windows raster-font defaults such as Fixedsys cannot be drawn by DirectWrite.
    for (const char *font : {":/fonts/Roboto-Regular.ttf", ":/fonts/Roboto-Bold.ttf",
                             ":/fonts/RobotoMono-Regular.ttf", ":/fonts/RobotoMono-Bold.ttf"})
        QFontDatabase::addApplicationFont(QString::fromLatin1(font));
    app.setFont(QFont(QStringLiteral("Roboto"), 10));

#if defined(Q_OS_WIN)
    if (isOpenGL)
    {
        QOpenGLContext ctx;
        isOpenGL = ctx.create() && ctx.format().version() >= qMakePair(2, 1);
        if (!isOpenGL) {
            qputenv("QMLSCENE_DEVICE", "softwarecontext");
        }
    }
#endif

    app.setApplicationName("monzero-core");
    app.setDesktopFileName("org.monzero.Monzero");
    app.setOrganizationDomain("monzero.org");
    app.setOrganizationName("monzero-project");

    // Ask to enable Tails OS persistence mode, it affects:
    // - Log file location
    // - QML Settings file location (monero-core.conf)
    // - Default wallets path
    // Target directory is: ~/Persistent/Monzero
    if (isTails) {
        if (!TailsOS::detectDataPersistence())
            TailsOS::showDataPersistenceDisabledWarning();
        else
            TailsOS::askPersistence();
    }

    QString moneroAccountsDir;
    QString legacyMoneroAccountsDir;
    #if defined(Q_OS_WIN) || defined(Q_OS_IOS)
        QStringList moneroAccountsRootDir = QStandardPaths::standardLocations(QStandardPaths::DocumentsLocation);
    #else
        QStringList moneroAccountsRootDir = QStandardPaths::standardLocations(QStandardPaths::HomeLocation);
    #endif

    if(isTails && TailsOS::usePersistence){
        moneroAccountsDir = QDir::homePath() + "/Persistent/Monzero/wallets";
        legacyMoneroAccountsDir = QDir::homePath() + "/Persistent/Monero/wallets";
    } else if (!moneroAccountsRootDir.empty()) {
        moneroAccountsDir = moneroAccountsRootDir.at(0) + "/Monzero/wallets";
        legacyMoneroAccountsDir = moneroAccountsRootDir.at(0) + "/Monero/wallets";
    } else {
        qCritical() << "Error: accounts root directory could not be set";
        return 1;
    }
    moneroAccountsDir = QDir::toNativeSeparators(moneroAccountsDir);
    legacyMoneroAccountsDir = QDir::toNativeSeparators(legacyMoneroAccountsDir);

#if defined(Q_OS_LINUX)
    if (isDesktop) app.setWindowIcon(QIcon(":/images/appicon.ico"));
#endif

    filter *eventFilter = new filter;
    app.installEventFilter(eventFilter);

    QCommandLineParser parser;
    QCommandLineOption logPathOption(QStringList() << "l" << "log-file",
        QCoreApplication::translate("main", "Log to specified file"),
        QCoreApplication::translate("main", "file"));

    QCommandLineOption disableCheckUpdatesOption("disable-check-updates", "Disable automatic check for updates.");
    parser.addOption(disableCheckUpdatesOption);
    QCommandLineOption socksProxyOption("socks5-proxy", "Enable socks5 proxy. Used for remote node connection (advanced mode), updates downloading and fetching price sources.", "address:port");
    parser.addOption(socksProxyOption);
    // Keep a startup-only alias distinct from the QML_TESTS harness interception.
    QCommandLineOption testQmlOption(QStringList() << "test-qml" << "startup-smoke-test");
    testQmlOption.setFlags(QCommandLineOption::HiddenFromHelp);
    parser.addOption(logPathOption);
    parser.addOption(testQmlOption);
    QCommandLineOption smokeDelayOption("startup-smoke-delay-ms", "Bounded event-loop delay for isolated startup tests.", "milliseconds", "0");
    smokeDelayOption.setFlags(QCommandLineOption::HiddenFromHelp);
    parser.addOption(smokeDelayOption);
#ifdef WITH_DEX_PROTOTYPE
    QCommandLineOption dexSelfTestOption("dex-self-test");
    dexSelfTestOption.setFlags(QCommandLineOption::HiddenFromHelp);
    parser.addOption(dexSelfTestOption);
    QCommandLineOption dexBackendOption("dex-private-backend", "Start a separate experimental DEX wallet service from a private local config.", "config");
    dexBackendOption.setFlags(QCommandLineOption::HiddenFromHelp);
    parser.addOption(dexBackendOption);
    QCommandLineOption dexQuoteOption("dex-private-quote", "Review one signed native quote from trusted private local configuration; no trading.", "config");
    dexQuoteOption.setFlags(QCommandLineOption::HiddenFromHelp);
    parser.addOption(dexQuoteOption);
    QCommandLineOption dexBchFundingReceiptOption("dex-bch-funding-receipt", "Attach private native-funder BCH funding receipt; startup reads saved status only.", "config");
    dexBchFundingReceiptOption.setFlags(QCommandLineOption::HiddenFromHelp);
    parser.addOption(dexBchFundingReceiptOption);
    QCommandLineOption dexBchNativePreparationOption("dex-bch-native-preparation", "Attach private XMZ deposit preparation; startup reads saved status only.", "config");
    dexBchNativePreparationOption.setFlags(QCommandLineOption::HiddenFromHelp);
    parser.addOption(dexBchNativePreparationOption);
    QCommandLineOption dexBchNativeRelayOption("dex-bch-native-relay", "Attach private XMZ funding broadcast; startup reads saved status only.", "config");
    dexBchNativeRelayOption.setFlags(QCommandLineOption::HiddenFromHelp);
    parser.addOption(dexBchNativeRelayOption);
    QCommandLineOption dexBchNativeProofOption("dex-bch-native-proof", "Attach private XMZ payment proof receipt; startup restores context and reads saved status only.", "config");
    dexBchNativeProofOption.setFlags(QCommandLineOption::HiddenFromHelp);
    parser.addOption(dexBchNativeProofOption);
    QCommandLineOption dexBchNativeObservationOption("dex-bch-native-observation", "Attach private XMZ payment observation; chain checks require an explicit request.", "config");
    dexBchNativeObservationOption.setFlags(QCommandLineOption::HiddenFromHelp);
    parser.addOption(dexBchNativeObservationOption);
    QCommandLineOption dexEvmTradeOption("dex-evm-trade", "Attach a private XMZ<->BNB/ETH trade (prototype); needs --dex-evm-seed-file.", "config");
    dexEvmTradeOption.setFlags(QCommandLineOption::HiddenFromHelp);
    parser.addOption(dexEvmTradeOption);
    QCommandLineOption dexEvmSeedOption("dex-evm-seed-file", "Private file with the wallet seed (hex) for the XMZ<->BNB/ETH prototype, until the vault supplies it.", "file");
    dexEvmSeedOption.setFlags(QCommandLineOption::HiddenFromHelp);
    parser.addOption(dexEvmSeedOption);
    QCommandLineOption dexBchPublicationRepairOption("dex-bch-publication-repair", "Attach private BCH record repair; requires explicit review and approval.", "config");
    dexBchPublicationRepairOption.setFlags(QCommandLineOption::HiddenFromHelp);
    parser.addOption(dexBchPublicationRepairOption);
    QCommandLineOption dexBchSuccessPreparationOption("dex-bch-success-preparation", "Attach private BCH settlement preparation; requires explicit review and approval.", "config");
    dexBchSuccessPreparationOption.setFlags(QCommandLineOption::HiddenFromHelp);
    parser.addOption(dexBchSuccessPreparationOption);
    QCommandLineOption dexBchSuccessReleaseReviewOption("dex-bch-success-release-review", "Attach private BCH settlement review; requires an explicit request.", "config");
    dexBchSuccessReleaseReviewOption.setFlags(QCommandLineOption::HiddenFromHelp);
    parser.addOption(dexBchSuccessReleaseReviewOption);
    QCommandLineOption dexBchSuccessSignatureReviewOption("dex-bch-success-signature-review", "Attach private BCH signature retention review; requires an explicit request.", "config");
    dexBchSuccessSignatureReviewOption.setFlags(QCommandLineOption::HiddenFromHelp);
    parser.addOption(dexBchSuccessSignatureReviewOption);
    QCommandLineOption dexBchSuccessSignedReleaseReviewOption("dex-bch-success-signed-release-review", "Attach private BCH signed settlement release review; requires an explicit request.", "config");
    dexBchSuccessSignedReleaseReviewOption.setFlags(QCommandLineOption::HiddenFromHelp);
    parser.addOption(dexBchSuccessSignedReleaseReviewOption);
    QCommandLineOption dexBchRefundProtectionOption("dex-bch-refund-protection", "Attach private BCH peer refund protection; startup reads saved status only.", "config");
    dexBchRefundProtectionOption.setFlags(QCommandLineOption::HiddenFromHelp);
    parser.addOption(dexBchRefundProtectionOption);
    QCommandLineOption dexBchRecoveryRetentionOption("dex-bch-recovery-retention", "Attach private BCH private recovery retention; startup reads saved status only.", "config");
    dexBchRecoveryRetentionOption.setFlags(QCommandLineOption::HiddenFromHelp);
    parser.addOption(dexBchRecoveryRetentionOption);
    QCommandLineOption dexBchPunishmentSubmissionOption("dex-bch-punishment-submission", "Attach private approved BCH timeout-recovery send; requires review and observation configurations.", "config");
    dexBchPunishmentSubmissionOption.setFlags(QCommandLineOption::HiddenFromHelp);
    parser.addOption(dexBchPunishmentSubmissionOption);
    QCommandLineOption dexBchPunishmentObservationOption("dex-bch-punishment-observation", "Attach private BCH timeout-recovery chain checks; requires punishment configuration.", "config");
    dexBchPunishmentObservationOption.setFlags(QCommandLineOption::HiddenFromHelp);
    parser.addOption(dexBchPunishmentObservationOption);
    QCommandLineOption dexBchRefundWalletOption("dex-bch-refund-wallet", "Attach private historical XMZ refund-wallet status; startup reads local records only.", "config");
    dexBchRefundWalletOption.setFlags(QCommandLineOption::HiddenFromHelp);
    parser.addOption(dexBchRefundWalletOption);
    QCommandLineOption dexBchRefundWalletReviewOption("dex-bch-refund-wallet-review", "Attach explicit fresh refund-wallet review configuration; no startup review or wallet mutation.", "config");
    dexBchRefundWalletReviewOption.setFlags(QCommandLineOption::HiddenFromHelp);
    parser.addOption(dexBchRefundWalletReviewOption);
    QCommandLineOption dexBchRefundWalletServiceOption("dex-bch-refund-wallet-service", "Bind and start the app-owned recovery-wallet service for the configured trade; resumes only a cleanly stopped directory.", "config");
    dexBchRefundWalletServiceOption.setFlags(QCommandLineOption::HiddenFromHelp);
    parser.addOption(dexBchRefundWalletServiceOption);
    QCommandLineOption dexBchPunishmentOption("dex-bch-punishment", "Attach private offline BCH punishment review; startup reads saved status only.", "config");
    dexBchPunishmentOption.setFlags(QCommandLineOption::HiddenFromHelp);
    parser.addOption(dexBchPunishmentOption);
    QCommandLineOption dexBchCancellationOption("dex-bch-cancellation", "Attach private BCH cancellation review; startup reads saved status only.", "config");
    dexBchCancellationOption.setFlags(QCommandLineOption::HiddenFromHelp);
    parser.addOption(dexBchCancellationOption);
    QCommandLineOption dexBchCancellationSubmitOption("dex-bch-cancellation-submit", "Attach private BCH explicit cancellation submission; startup reads saved status only.", "config");
    dexBchCancellationSubmitOption.setFlags(QCommandLineOption::HiddenFromHelp);
    parser.addOption(dexBchCancellationSubmitOption);
    QCommandLineOption dexBchRefundReleaseOption("dex-bch-refund-release", "Attach private BCH refund-release review; startup reads saved status only.", "config");
    dexBchRefundReleaseOption.setFlags(QCommandLineOption::HiddenFromHelp);
    parser.addOption(dexBchRefundReleaseOption);
    QCommandLineOption dexBchRefundReleaseSubmitOption("dex-bch-refund-release-submit", "Attach private BCH explicit refund-release submission; startup reads saved status only.", "config");
    dexBchRefundReleaseSubmitOption.setFlags(QCommandLineOption::HiddenFromHelp);
    parser.addOption(dexBchRefundReleaseSubmitOption);
    QCommandLineOption dexBchAdmissionOption("dex-bch-admission", "Attach a private BCH regtest negotiation admission configuration; no funding or broadcast.", "config");
    dexBchAdmissionOption.setFlags(QCommandLineOption::HiddenFromHelp);
    parser.addOption(dexBchAdmissionOption);
    for (const auto &coin:QStringList{"btc","bch","doge"}) {
        QCommandLineOption option("dex-"+coin+"-light","Attach a private local light-wallet configuration; no automatic network requests or payments.","config");
        option.setFlags(QCommandLineOption::HiddenFromHelp);parser.addOption(option);
    }
    for (const auto &name:QStringList{"dex-light-setup-helper","dex-light-setup-digest"}) {
        QCommandLineOption option(name,"Trusted local wallet setup helper installation.","value");
        option.setFlags(QCommandLineOption::HiddenFromHelp);parser.addOption(option);
    }
    QCommandLineOption dexLtcLightOption("dex-ltc-light", "Attach private Litecoin light-chain configuration; startup reads saved headers only.", "config");
    dexLtcLightOption.setFlags(QCommandLineOption::HiddenFromHelp);
    parser.addOption(dexLtcLightOption);
    QCommandLineOption dexBchPreparationOption("dex-bch-preparation", "Attach private BCH unsigned preparation configuration; startup reads saved status only.", "config");
    dexBchPreparationOption.setFlags(QCommandLineOption::HiddenFromHelp);
    parser.addOption(dexBchPreparationOption);
    QCommandLineOption dexBchSigningRecoveryOption("dex-bch-signing-recovery", "Attach a private recovered BCH transaction; importing requires separate review and approval.", "config");
    dexBchSigningRecoveryOption.setFlags(QCommandLineOption::HiddenFromHelp);
    parser.addOption(dexBchSigningRecoveryOption);
    QCommandLineOption dexBchFundingOption("dex-bch-funding", "Attach a private BCH retained funding review configuration; no broadcast.", "config");
    dexBchFundingOption.setFlags(QCommandLineOption::HiddenFromHelp);
    parser.addOption(dexBchFundingOption);
    QCommandLineOption dexBchFundingSubmitOption("dex-bch-funding-submit", "Attach caller-owned private BCH regtest funding relay configuration; sending requires separate approval.", "config");
    dexBchFundingSubmitOption.setFlags(QCommandLineOption::HiddenFromHelp);
    parser.addOption(dexBchFundingSubmitOption);
    QCommandLineOption dexLtcAppOption("dex-ltc-app", "Attach one private Litecoin trade setup covering startup, peer and settlement phases.", "config");
    dexLtcAppOption.setFlags(QCommandLineOption::HiddenFromHelp);
    parser.addOption(dexLtcAppOption);
    QCommandLineOption dexLtcJournalOption("dex-ltc-journal", "Attach a private Litecoin trade configuration for isolated startup, journal review or separately approved payments.", "config");
    dexLtcJournalOption.setFlags(QCommandLineOption::HiddenFromHelp);
    parser.addOption(dexLtcJournalOption);
    QCommandLineOption dexLtcPeerOption("dex-ltc-peer", "Attach private Litecoin identity and pinned TLS configuration; connects after the reviewed trade is saved.", "config");
    dexLtcPeerOption.setFlags(QCommandLineOption::HiddenFromHelp);
    parser.addOption(dexLtcPeerOption);
    QCommandLineOption dexLtcReservationOption("dex-ltc-reservation", "Configure isolated Litecoin input reservation and recovery exchange; no funding broadcast.", "config");
    dexLtcReservationOption.setFlags(QCommandLineOption::HiddenFromHelp);
    parser.addOption(dexLtcReservationOption);
    QCommandLineOption dexLtcNetworkOption("dex-ltc-network", "Swap chain network: regtest (default) or testnet for Litecoin, btc-regtest, btc-testnet4 or btc-signet for Bitcoin. Mainnet is not available.", "network");
    dexLtcNetworkOption.setFlags(QCommandLineOption::HiddenFromHelp);
    parser.addOption(dexLtcNetworkOption);
    QCommandLineOption dexControlFileOption("dex-control-file", "Signed operator control document (kill switch and trade limits) from a private local file.", "file");
    dexControlFileOption.setFlags(QCommandLineOption::HiddenFromHelp);
    parser.addOption(dexControlFileOption);
    QCommandLineOption dexControlUrlOption("dex-control-url", "HTTPS URL polled for the signed operator control document.", "url");
    dexControlUrlOption.setFlags(QCommandLineOption::HiddenFromHelp);
    parser.addOption(dexControlUrlOption);
    QCommandLineOption dexTradeOption("dex-trade", "Reopen this trade folder (trade id) instead of the single active one.", "trade-id");
    dexTradeOption.setFlags(QCommandLineOption::HiddenFromHelp);
    parser.addOption(dexTradeOption);
    QCommandLineOption dexTradeRootOption("dex-trade-root", "Directory holding trade folders (default: application data dex/trades).", "directory");
    dexTradeRootOption.setFlags(QCommandLineOption::HiddenFromHelp);
    parser.addOption(dexTradeRootOption);
#endif
    parser.addHelpOption();
    parser.process(app);
    bool smokeDelayOk=false;
    const int smokeDelay=parser.value(smokeDelayOption).toInt(&smokeDelayOk);
    if (!smokeDelayOk || smokeDelay<0 || smokeDelay>15000
        || (parser.isSet(smokeDelayOption) && !parser.isSet(testQmlOption))) return 1;

#ifdef WITH_DEX_PROTOTYPE
    if (parser.isSet(dexSelfTestOption)) {
        DexEngine engine;
        const QVariantMap result = engine.selfTest();
        qInfo() << "DEX engine self-test:" << result;
        return result.value("passed").toBool() ? 0 : 1;
    }
#endif

    Monero::Utils::onStartup();

    Logger logger(app, parser.value(logPathOption));

    // Log level is configured in main.qml. Anything lower than qWarning is
    // not shown here unless MONZERO_LOG_LEVEL is set.
    bool logLevelOk;
    int logLevel = qEnvironmentVariableIntValue("MONZERO_LOG_LEVEL", &logLevelOk);
    if (logLevelOk && logLevel >= 0 && logLevel <= Monero::WalletManagerFactory::LogLevel_Max){
        Monero::WalletManagerFactory::setLogLevel(logLevel);
    }

    IPC *ipc = new IPC(&app);
    QStringList posArgs = parser.positionalArguments();

    for(int i = 0; i != posArgs.count(); i++){
        QString arg = QString(posArgs.at(i));
        if(arg.isEmpty() || arg.length() >= 512) continue;
        if(arg.contains(reURI)){
            if(!ipc->saveCommand(arg)){
                return 0;
            }
        }
    }

    // start listening
    QTimer::singleShot(0, ipc, SLOT(bind()));

    // screen settings
    // Mobile is designed on 128dpi
    qreal ref_dpi = 128;
    QSize screenAvailableSize = QGuiApplication::primaryScreen()->availableSize();
    QRect rect = QGuiApplication::primaryScreen()->geometry();
    qreal dpi = QGuiApplication::primaryScreen()->logicalDotsPerInch();
    qreal physicalDpi = QGuiApplication::primaryScreen()->physicalDotsPerInch();
    qreal calculated_ratio = physicalDpi/ref_dpi;

    QString GUI_VERSION = "-";
    QFile f(":/version.js");
    if(!f.open(QFile::ReadOnly)) {
        qWarning() << "Could not read qrc:///version.js";
    } else {
        QByteArray contents = f.readAll();
        f.close();

        QRegularExpression numberRe("var GUI_VERSION_NUMBER = \"([^\"]+)\"");
        QRegularExpression tagRe("var GUI_VERSION_TAG = \"([^\"]+)\"");
        QRegularExpressionMatch numberMatch = numberRe.match(contents);
        QRegularExpressionMatch tagMatch = tagRe.match(contents);
        if (numberMatch.hasMatch() && tagMatch.hasMatch()) {
            GUI_VERSION = numberMatch.captured(1) + "-" + tagMatch.captured(1);
        }
    }

    qWarning().nospace().noquote() << "Qt:" << QT_VERSION_STR << " GUI:" << GUI_VERSION
                                   << " | screen: " << rect.width() << "x" << rect.height()
                                   << " - available: " << screenAvailableSize
                                   << " - dpi: " << dpi << " - ratio:" << calculated_ratio;

    // registering types for QML
    qmlRegisterType<clipboardAdapter>("moneroComponents.Clipboard", 1, 0, "Clipboard");
    qmlRegisterType<Downloader>("moneroComponents.Downloader", 1, 0, "Downloader");
    qmlRegisterType<Network>("moneroComponents.Network", 1, 0, "Network");
    qmlRegisterType<WalletKeysFilesModel>("moneroComponents.WalletKeysFilesModel", 1, 0, "WalletKeysFilesModel");
    qmlRegisterType<LanguageModel>("moneroComponents.LanguageModel", 1, 0, "LanguageModel");
    qmlRegisterType<WalletManager>("moneroComponents.WalletManager", 1, 0, "WalletManager");

    // Temporary Qt.labs.settings replacement
    qmlRegisterType<MoneroSettings>("moneroComponents.Settings", 1, 0, "MonzeroSettings");

    qmlRegisterUncreatableType<Wallet>("moneroComponents.Wallet", 1, 0, "Wallet", "Wallet can't be instantiated directly");


    qmlRegisterUncreatableType<PendingTransaction>("moneroComponents.PendingTransaction", 1, 0, "PendingTransaction",
                                                   "PendingTransaction can't be instantiated directly");

    qmlRegisterUncreatableType<UnsignedTransaction>("moneroComponents.UnsignedTransaction", 1, 0, "UnsignedTransaction",
                                                   "UnsignedTransaction can't be instantiated directly");

    qmlRegisterUncreatableType<TranslationManager>("moneroComponents.TranslationManager", 1, 0, "TranslationManager",
                                                   "TranslationManager can't be instantiated directly");

    qmlRegisterUncreatableType<TransactionHistoryModel>("moneroComponents.TransactionHistoryModel", 1, 0, "TransactionHistoryModel",
                                                        "TransactionHistoryModel can't be instantiated directly");

    qmlRegisterUncreatableType<TransactionHistorySortFilterModel>("moneroComponents.TransactionHistorySortFilterModel", 1, 0, "TransactionHistorySortFilterModel",
                                                        "TransactionHistorySortFilterModel can't be instantiated directly");

    qmlRegisterUncreatableType<TransactionHistory>("moneroComponents.TransactionHistory", 1, 0, "TransactionHistory",
                                                        "TransactionHistory can't be instantiated directly");

    qmlRegisterUncreatableType<TransactionInfo>("moneroComponents.TransactionInfo", 1, 0, "TransactionInfo",
                                                        "TransactionHistory can't be instantiated directly");
#ifndef Q_OS_IOS
    qmlRegisterUncreatableType<DaemonManager>("moneroComponents.DaemonManager", 1, 0, "DaemonManager",
                                                   "DaemonManager can't be instantiated directly");
    qmlRegisterUncreatableType<P2PoolManager>("moneroComponents.P2PoolManager", 1, 0, "P2PoolManager",
                                                   "P2PoolManager can't be instantiated directly");
#endif
    qmlRegisterUncreatableType<AddressBookModel>("moneroComponents.AddressBookModel", 1, 0, "AddressBookModel",
                                                        "AddressBookModel can't be instantiated directly");

    qmlRegisterUncreatableType<AddressBook>("moneroComponents.AddressBook", 1, 0, "AddressBook",
                                                        "AddressBook can't be instantiated directly");

    qmlRegisterUncreatableType<SubaddressModel>("moneroComponents.SubaddressModel", 1, 0, "SubaddressModel",
                                                        "SubaddressModel can't be instantiated directly");

    qmlRegisterUncreatableType<Subaddress>("moneroComponents.Subaddress", 1, 0, "Subaddress",
                                                        "Subaddress can't be instantiated directly");

    qmlRegisterUncreatableType<SubaddressAccountModel>("moneroComponents.SubaddressAccountModel", 1, 0, "SubaddressAccountModel",
                                                        "SubaddressAccountModel can't be instantiated directly");

    qmlRegisterUncreatableType<SubaddressAccount>("moneroComponents.SubaddressAccount", 1, 0, "SubaddressAccount",
                                                        "SubaddressAccount can't be instantiated directly");

    qRegisterMetaType<PendingTransaction::Priority>();
    qRegisterMetaType<TransactionInfo::Direction>();
    qRegisterMetaType<TransactionHistoryModel::TransactionInfoRole>();

    qRegisterMetaType<NetworkType::Type>();
    qmlRegisterType<NetworkType>("moneroComponents.NetworkType", 1, 0, "NetworkType");

#ifdef WITH_SCANNER
    qmlRegisterType<QrCodeScanner>("moneroComponents.QRCodeScanner", 1, 0, "QRCodeScanner");
#endif

    QQmlApplicationEngine engine;

#if QT_VERSION >= QT_VERSION_CHECK(5, 12, 0)
    engine.setNetworkAccessManagerFactory(new NetworkAccessBlockingFactory);
#endif
    OSCursor cursor;
    engine.rootContext()->setContextProperty("globalCursor", &cursor);
    OSHelper osHelper;
    engine.rootContext()->setContextProperty("oshelper", &osHelper);

    engine.addImportPath(":/fonts");

    engine.rootContext()->setContextProperty("moneroAccountsDir", moneroAccountsDir);
    engine.rootContext()->setContextProperty("legacyMoneroAccountsDir", legacyMoneroAccountsDir);

    engine.rootContext()->setContextProperty("translationManager", TranslationManager::instance());

    engine.addImageProvider(QLatin1String("qrcode"), new QRCodeImageProvider());

    engine.rootContext()->setContextProperty("logger", &logger);

    engine.rootContext()->setContextProperty("mainApp", &app);

    engine.rootContext()->setContextProperty("IPC", ipc);

    engine.rootContext()->setContextProperty("qtRuntimeVersion", qVersion());

    engine.rootContext()->setContextProperty("tailsUsePersistence", TailsOS::usePersistence);

// Exclude daemon manager from IOS
#ifndef Q_OS_IOS
    DaemonManager daemonManager;
    P2PoolManager p2poolManager;
    engine.rootContext()->setContextProperty("daemonManager", &daemonManager);
    engine.rootContext()->setContextProperty("p2poolManager", &p2poolManager);
#endif

    engine.rootContext()->setContextProperty("isWindows", isWindows);
    engine.rootContext()->setContextProperty("isMac", isMac);
    engine.rootContext()->setContextProperty("isLinux", isLinux);
    engine.rootContext()->setContextProperty("isIOS", isIOS);
    engine.rootContext()->setContextProperty("isAndroid", isAndroid);
    engine.rootContext()->setContextProperty("isOpenGL", isOpenGL);
    engine.rootContext()->setContextProperty("isTails", isTails);
    engine.rootContext()->setContextProperty("isARM", isARM);

    bool miningEnabled = true;
#ifdef MONZERO_NO_MINING
    miningEnabled = false;
#endif
    engine.rootContext()->setContextProperty("miningEnabled", miningEnabled);

    bool dexPrototypeEnabled = false;
#ifdef WITH_DEX_PROTOTYPE
    dexPrototypeEnabled = qgetenv("MONZERO_ENABLE_PRIVATE_DEX") == "I_UNDERSTAND_THIS_IS_A_LOCAL_PROTOTYPE";
#endif
    engine.rootContext()->setContextProperty("dexPrototypeEnabled", dexPrototypeEnabled);
#ifdef WITH_DEX_PROTOTYPE
    if (parser.isSet(dexLtcNetworkOption)) {
        if (!dexPrototypeEnabled || !DexDeployment::selectLitecoinStage(parser.value(dexLtcNetworkOption))) {
            qCritical("Swap network rejected; only regtest, testnet, btc-regtest, btc-testnet4 or btc-signet is available");return 1;
        }
    }
    DexDeployment::lockLitecoinStage();
    DexEngine dexEngine;
    {
        // New-trade limits. The ledger is local; the control key is pinned at build time.
        const QString dexData=QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath("dex");
        if (!QDir().mkpath(dexData) || !QFile::setPermissions(dexData,QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner)
            || !dexEngine.riskControl()->setLedgerFile(QDir(dexData).filePath("started-trades.json"))
            || !dexEngine.riskControl()->setPublicKey(QStringLiteral(MONZERO_DEX_CONTROL_PUBKEY))) {
            qCritical("DEX trade-limit storage or control key is invalid");return 1;
        }
        if (parser.isSet(dexControlFileOption) && (!dexPrototypeEnabled || !dexEngine.riskControl()->loadControlFile(parser.value(dexControlFileOption)))) {
            qCritical("DEX operator control document rejected");return 1;
        }
        if (parser.isSet(dexControlUrlOption) && (!dexPrototypeEnabled || !dexEngine.riskControl()->startPolling(parser.value(dexControlUrlOption)))) {
            qCritical("DEX operator control URL rejected; HTTPS is required");return 1;
        }
    }
    for (const auto &coin:QStringList{"btc","bch","doge"}) {
        const auto option="dex-"+coin+"-light";
        if (parser.isSet(option) && (!dexPrototypeEnabled || !dexEngine.configureLightWallet(coin,parser.value(option)))) {
            qCritical("Invalid private light-wallet configuration or missing prototype opt-in");return 1;
        }
    }
    if (parser.isSet(dexLtcLightOption)) {
        if (!dexPrototypeEnabled || !dexEngine.configureLitecoinLight(parser.value(dexLtcLightOption))) {
            qCritical("Invalid private Litecoin light-chain configuration or missing prototype opt-in");
            return 1;
        }
        QTimer::singleShot(0,&dexEngine,[&dexEngine]{dexEngine.requestLitecoinLightStatus();});
    }
    if (parser.isSet("dex-light-setup-helper") || parser.isSet("dex-light-setup-digest")) {
        if (!dexPrototypeEnabled || !parser.isSet("dex-light-setup-helper") || !parser.isSet("dex-light-setup-digest")
            || !dexEngine.configureLightWalletSetup(parser.value("dex-light-setup-helper"),parser.value("dex-light-setup-digest"))) {
            qCritical("Invalid trusted light-wallet setup helper or missing prototype opt-in");return 1;
        }
    }
    if (dexPrototypeEnabled && !parser.isSet("dex-light-setup-helper") && !parser.isSet("dex-light-setup-digest")
        && BundledLightWalletHelper::sha256[0] != '\0') {
        const auto helper=QCoreApplication::applicationDirPath()+QString::fromLatin1(BundledLightWalletHelper::relativePath);
        if (!dexEngine.configureLightWalletSetup(helper,QString::fromLatin1(BundledLightWalletHelper::sha256))) {
            qCritical("Bundled light-wallet helper verification failed");return 1;
        }
    }
    if (parser.isSet(dexBchPreparationOption)) {
        if (!dexPrototypeEnabled || !dexEngine.configureBchPreparation(parser.value(dexBchPreparationOption))) {
            qCritical("Invalid private BCH preparation configuration or missing prototype opt-in");
            return 1;
        }
    }

    if (parser.isSet(dexBchSigningRecoveryOption)) {
        if (!dexPrototypeEnabled || !dexEngine.configureBchSigningRecovery(parser.value(dexBchSigningRecoveryOption))) {
            qCritical("Invalid private BCH signing recovery configuration or missing preparation/prototype opt-in");
            return 1;
        }
    }

    if (parser.isSet(dexBchFundingOption)) {
        if (!dexPrototypeEnabled || !dexEngine.configureBchFunding(parser.value(dexBchFundingOption))) {
            qCritical("Invalid private BCH funding review configuration or missing prototype opt-in");
            return 1;
        }
    }
    if (parser.isSet(dexBchFundingSubmitOption)) {
        if (!dexPrototypeEnabled || !dexEngine.configureBchFundingSubmission(parser.value(dexBchFundingSubmitOption))) {
            qCritical("Invalid private BCH funding submission configuration or missing prototype opt-in");
            return 1;
        }
    }
    if (parser.isSet(dexBchAdmissionOption)) {
        if (!dexPrototypeEnabled || !dexEngine.configureBchAdmission(parser.value(dexBchAdmissionOption))) {
            qCritical() << "Private BCH admission configuration rejected";
            return 1;
        }
    }
    if (parser.isSet(dexBchFundingReceiptOption)) {
        if (!dexPrototypeEnabled || !dexEngine.configureBchFundingReceipt(parser.value(dexBchFundingReceiptOption))) {
            qCritical("Private BCH funding receipt configuration rejected");
            return 1;
        }
    }
    if (parser.isSet(dexBchNativePreparationOption)) {
        if (!dexPrototypeEnabled || !dexEngine.configureBchNativePreparation(parser.value(dexBchNativePreparationOption))) {
            qCritical("Private BCH native preparation configuration rejected");
            return 1;
        }
    }
    if (parser.isSet(dexBchNativeRelayOption)) {
        if (!dexPrototypeEnabled || !dexEngine.configureBchNativeRelay(parser.value(dexBchNativeRelayOption))) {
            qCritical("Private BCH native relay configuration rejected");
            return 1;
        }
    }
    if (parser.isSet(dexBchNativeProofOption)) {
        if (!dexPrototypeEnabled || !dexEngine.configureBchNativeProof(parser.value(dexBchNativeProofOption))) {
            qCritical("Private BCH native proof configuration rejected");
            return 1;
        }
    }
    if (parser.isSet(dexEvmTradeOption) || parser.isSet(dexEvmSeedOption)) {
        if (!dexPrototypeEnabled || !parser.isSet(dexEvmTradeOption) || !parser.isSet(dexEvmSeedOption)
            || !dexEngine.configureEvmTrade(parser.value(dexEvmTradeOption),parser.value(dexEvmSeedOption))) {
            qCritical("Private XMZ<->EVM trade configuration rejected");
            return 1;
        }
    }
    if (parser.isSet(dexBchNativeObservationOption)) {
        if (!dexPrototypeEnabled || !dexEngine.configureBchNativeObservation(parser.value(dexBchNativeObservationOption))) {
            qCritical("Private BCH native observation configuration rejected");
            return 1;
        }
    }
    if (parser.isSet(dexBchPublicationRepairOption)) {
        if (!dexPrototypeEnabled || !dexEngine.configureBchPublicationRepair(parser.value(dexBchPublicationRepairOption))) {
            qCritical("Private BCH publication repair configuration rejected");
            return 1;
        }
    }
    if (parser.isSet(dexBchSuccessPreparationOption)) {
        if (!dexPrototypeEnabled || !dexEngine.configureBchSuccessPreparation(parser.value(dexBchSuccessPreparationOption))) {
            qCritical("Private BCH success preparation configuration rejected");
            return 1;
        }
    }
    if (parser.isSet(dexBchSuccessReleaseReviewOption)) {
        if (!dexPrototypeEnabled || !dexEngine.configureBchSuccessReleaseReview(parser.value(dexBchSuccessReleaseReviewOption))) {
            qCritical("Private BCH success release review configuration rejected");
            return 1;
        }
    }
    if (parser.isSet(dexBchSuccessSignatureReviewOption)) {
        if (!dexPrototypeEnabled || !dexEngine.configureBchSuccessSignatureReview(parser.value(dexBchSuccessSignatureReviewOption))) {
            qCritical("Private BCH success signature review configuration rejected");
            return 1;
        }
    }
    if (parser.isSet(dexBchSuccessSignedReleaseReviewOption)) {
        if (!dexPrototypeEnabled || !dexEngine.configureBchSuccessSignedReleaseReview(parser.value(dexBchSuccessSignedReleaseReviewOption))) {
            qCritical("Private BCH success signed release review configuration rejected");
            return 1;
        }
    }
    if (parser.isSet(dexBchRefundProtectionOption)) {
        if (!dexPrototypeEnabled || !dexEngine.configureBchRefundProtection(parser.value(dexBchRefundProtectionOption))) {
            qCritical("Private BCH refund protection configuration rejected");
            return 1;
        }
    }
    if (parser.isSet(dexBchRecoveryRetentionOption)) {
        if (!dexPrototypeEnabled || !dexEngine.configureBchRecoveryRetention(parser.value(dexBchRecoveryRetentionOption))) {
            qCritical("Private BCH recovery retention configuration rejected");
            return 1;
        }
    }
    if (parser.isSet(dexBchRefundWalletOption)) {
        if (!dexPrototypeEnabled || !dexEngine.configureBchRefundWallet(parser.value(dexBchRefundWalletOption))) {
            qCritical("Private BCH refund-wallet status configuration rejected");return 1;
        }
    }
    if (parser.isSet(dexBchRefundWalletReviewOption)) {
        if (!dexPrototypeEnabled || !parser.isSet(dexBchRefundWalletOption)
            || !dexEngine.configureBchRefundWalletReview(parser.value(dexBchRefundWalletReviewOption))) {
            qCritical("Invalid private refund-wallet review configuration or missing status configuration");return 1;
        }
    }
    if (parser.isSet(dexBchRefundWalletServiceOption)) {
        if (!dexPrototypeEnabled || !parser.isSet(dexBchRefundWalletOption) || parser.isSet(dexBchRefundWalletReviewOption)
            || !dexEngine.configureBchRefundWalletService(parser.value(dexBchRefundWalletServiceOption))) {
            qCritical("Invalid private refund-wallet service configuration, missing status configuration or conflicting review configuration");return 1;
        }
        QObject::connect(&app,&QCoreApplication::aboutToQuit,&dexEngine,&DexEngine::shutdownBchRefundWalletService);
    }

    if (parser.isSet(dexBchPunishmentOption)) {
        if (!dexPrototypeEnabled || !dexEngine.configureBchPunishment(parser.value(dexBchPunishmentOption))) {
            qCritical("Private BCH punishment configuration rejected");return 1;
        }
    }
    if (parser.isSet(dexBchPunishmentObservationOption)) {
        if (!dexPrototypeEnabled || !dexEngine.configureBchPunishmentObservation(parser.value(dexBchPunishmentObservationOption))) {
            qCritical("Private BCH punishment observation configuration rejected");return 1;
        }
    }
    if (parser.isSet(dexBchPunishmentSubmissionOption)) {
        if (!dexPrototypeEnabled || !dexEngine.configureBchPunishmentSubmission(parser.value(dexBchPunishmentSubmissionOption))) {
            qCritical("Private BCH punishment submission configuration rejected");return 1;
        }
    }
    if (parser.isSet(dexBchCancellationOption)) {
        if (!dexPrototypeEnabled || parser.isSet(dexBchCancellationSubmitOption)
            || !dexEngine.configureBchCancellation(parser.value(dexBchCancellationOption))) {
            qCritical("Private BCH cancellation configuration rejected");return 1;
        }
    }
    if (parser.isSet(dexBchCancellationSubmitOption)) {
        if (!dexPrototypeEnabled || !dexEngine.configureBchCancellationSubmission(parser.value(dexBchCancellationSubmitOption))) {
            qCritical("Private BCH cancellation submission configuration rejected");return 1;
        }
    }
    if (parser.isSet(dexBchRefundReleaseOption)) {
        if (!dexPrototypeEnabled || parser.isSet(dexBchRefundReleaseSubmitOption)
            || !dexEngine.configureBchRefundRelease(parser.value(dexBchRefundReleaseOption))) {
            qCritical("Private BCH refund configuration rejected");return 1;
        }
    }
    if (parser.isSet(dexBchRefundReleaseSubmitOption)) {
        if (!dexPrototypeEnabled || !dexEngine.configureBchRefundReleaseSubmission(parser.value(dexBchRefundReleaseSubmitOption))) {
            qCritical("Private BCH refund submission configuration rejected");return 1;
        }
    }
    dexEngine.requestBchStartupChecks();
    if (dexPrototypeEnabled && !parser.isSet(dexLtcAppOption) && !parser.isSet(dexLtcJournalOption)
        && !parser.isSet(dexLtcPeerOption) && !parser.isSet(dexLtcReservationOption)) {
        const auto root=parser.isSet(dexTradeRootOption)?parser.value(dexTradeRootOption):DexTradeFolders::defaultRoot();
        if (!dexEngine.openTradeFolders(root,parser.value(dexTradeOption))) {
            qCritical("Requested trade folder could not be opened");return 1;
        }
    } else if (parser.isSet(dexTradeOption)) {
        qCritical("--dex-trade cannot be combined with explicit Litecoin trade configuration");return 1;
    }
    if (parser.isSet(dexLtcAppOption)) {
        if (!dexPrototypeEnabled || parser.isSet(dexLtcJournalOption) || parser.isSet(dexLtcPeerOption)
            || parser.isSet(dexLtcReservationOption) || !dexEngine.configureLitecoinApp(QFileInfo(parser.value(dexLtcAppOption)).absoluteFilePath())) {
            qCritical() << "Private Litecoin app configuration rejected";
            return 1;
        }
        const auto root=parser.isSet(dexTradeRootOption)?parser.value(dexTradeRootOption):DexTradeFolders::defaultRoot();
        if (!dexEngine.registerConfiguredLitecoinApp(root,QFileInfo(parser.value(dexLtcAppOption)).absoluteFilePath())) {
            qCritical("Private Litecoin app could not be registered for reopening");return 1;
        }
        dexEngine.requestLitecoinJournalCheck();
    }
    if (parser.isSet(dexLtcJournalOption)) {
        if (!dexPrototypeEnabled || !dexEngine.configureLitecoinJournal(parser.value(dexLtcJournalOption))) {
            qCritical() << "Private Litecoin journal configuration rejected";
            return 1;
        }
        dexEngine.requestLitecoinJournalCheck();
    }
    std::unique_ptr<LitecoinPeerStartup> dexLtcPeer;
    if (parser.isSet(dexLtcPeerOption)) {
        dexLtcPeer=std::make_unique<LitecoinPeerStartup>(dexEngine);
        if (!dexPrototypeEnabled || !parser.isSet(dexLtcJournalOption)
            || !dexLtcPeer->configure(parser.value(dexLtcPeerOption),parser.value(dexLtcJournalOption))) {
            qCritical() << "Private Litecoin peer configuration rejected";
            return 1;
        }
    }
    if (parser.isSet(dexLtcReservationOption)) {
        if (!dexPrototypeEnabled || !parser.isSet(dexLtcPeerOption) || !parser.isSet(dexLtcJournalOption)
            || !dexEngine.configureLitecoinReservation(parser.value(dexLtcReservationOption),parser.value(dexLtcJournalOption))) {
            qCritical() << "Private Litecoin reservation configuration rejected";
            return 1;
        }
    }
    std::unique_ptr<SwapWalletRpcProcess> dexOwner;
    std::unique_ptr<SwapWalletSession> dexSession;
    std::unique_ptr<NativeQuoteCoordinator> dexQuote;
    if (parser.isSet(dexQuoteOption)) {
        NativeQuoteConfig config;
        if (!dexPrototypeEnabled || !NativeQuoteConfig::load(parser.value(dexQuoteOption), QDateTime::currentSecsSinceEpoch(), config)) {
            qCritical() << "Private DEX quote configuration rejected";
            return 1;
        }
        dexQuote = std::make_unique<NativeQuoteCoordinator>(config.helper, config.helperDigest,
            config.directory, config.policy, config.btcPeer, config.xmzPeer);
        if (!dexEngine.attachNativeQuoteCoordinator(dexQuote.get())
            || !dexQuote->begin(config.payload, config.btcSignature, config.xmzSignature)) {
            qCritical() << "Private DEX quote review rejected";
            return 1;
        }
    }
    if (parser.isSet(dexBackendOption)) {
        PrivateBackendConfig config;
        if (!dexPrototypeEnabled || !PrivateBackendConfig::load(parser.value(dexBackendOption),config)) {
            qCritical() << "Private DEX backend configuration rejected";
            return 1;
        }
        dexOwner=std::make_unique<SwapWalletRpcProcess>();
        dexSession=std::make_unique<SwapWalletSession>(dexOwner.get(),config.helper,config.genesis);
        if (!dexSession->pinHelperDigest(config.helperDigest)
            || !dexSession->pinRecoveryDirectory(config.directory)
            || !dexEngine.attachSwapWalletSession(dexSession.get())
            || !dexOwner->startFreshWithDigest(config.rpc,config.rpcDigest,config.directory,
                config.daemonPort,config.walletPort)) {
            qCritical() << "Private DEX backend startup rejected";
            return 1;
        }
        QObject::connect(&app,&QCoreApplication::aboutToQuit,dexOwner.get(),&SwapWalletRpcProcess::stop);
    }
    engine.rootContext()->setContextProperty("dexEngine", &dexEngine);
#endif

    engine.rootContext()->setContextProperty("screenAvailableWidth", screenAvailableSize.width());
    engine.rootContext()->setContextProperty("screenAvailableHeight", screenAvailableSize.height());

#ifndef Q_OS_IOS
    const QString desktopFolder = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
    if (!desktopFolder.isEmpty())
        engine.rootContext()->setContextProperty("desktopFolder", desktopFolder);
#endif

    // Get default account name
    QString accountName = qgetenv("USER"); // mac/linux
    if (accountName.isEmpty())
        accountName = qgetenv("USERNAME"); // Windows
    if (accountName.isEmpty())
        accountName = "My Monzero Account";

    engine.rootContext()->setContextProperty("defaultAccountName", accountName);
    engine.rootContext()->setContextProperty("homePath", QDir::homePath());
    engine.rootContext()->setContextProperty("applicationDirectory", QApplication::applicationDirPath());
    engine.rootContext()->setContextProperty("idealThreadCount", QThread::idealThreadCount());
#ifdef WITH_UPDATER
    engine.rootContext()->setContextProperty("disableCheckUpdatesFlag", parser.isSet(disableCheckUpdatesOption));
#else
    engine.rootContext()->setContextProperty("disableCheckUpdatesFlag", true);
#endif
    engine.rootContext()->setContextProperty("socksProxyFlag", parser.value(socksProxyOption));
    engine.rootContext()->setContextProperty("socksProxyFlagSet", parser.isSet(socksProxyOption));

    bool builtWithScanner = false;
#ifdef WITH_SCANNER
    builtWithScanner = true;
#endif
    engine.rootContext()->setContextProperty("builtWithScanner", builtWithScanner);

    bool builtWithDesktopEntry = false;
#ifdef WITH_DESKTOP_ENTRY
    builtWithDesktopEntry = true;
#endif
    engine.rootContext()->setContextProperty("builtWithDesktopEntry", builtWithDesktopEntry);

    engine.rootContext()->setContextProperty("moneroVersion", MONERO_VERSION_FULL);

    // Load main window (context properties needs to be defined obove this line)
    engine.load(QUrl(QStringLiteral("qrc:///main.qml")));
    if (engine.rootObjects().isEmpty())
    {
        qCritical() << "Error: no root objects";
        return 1;
    }
    QObject *rootObject = engine.rootObjects().first();
    if (!rootObject)
    {
        qCritical() << "Error: no root objects";
        return 1;
    }

    // QML loaded successfully.
    if (parser.isSet(testQmlOption)) {
        if (smokeDelay==0) return 0;
        QTimer::singleShot(smokeDelay,&app,&QCoreApplication::quit);
        const int status = app.exec();
#ifdef WITH_DEX_PROTOTYPE
        if (dexQuote) {
            if (status != 0 || dexQuote->state() != NativeQuoteCoordinator::Ready
                || dexEngine.nativeQuoteReview().isEmpty()) {
                qCritical() << "Private native quote startup verification failed";
                return 1;
            }
            qWarning() << "Private native quote review verified; no reservation or funding";
        }
#endif
        return status;
    }

#ifdef WITH_SCANNER
    QObject *qmlCamera = rootObject->findChild<QObject*>("qrCameraQML");
    if (qmlCamera)
    {
        qWarning() << "QrCodeScanner : object found";
        QCamera *camera_ = qvariant_cast<QCamera*>(qmlCamera->property("mediaObject"));
        QObject *qmlFinder = rootObject->findChild<QObject*>("QrFinder");
        qobject_cast<QrCodeScanner*>(qmlFinder)->setSource(camera_);
    }
    else
        qCritical() << "QrCodeScanner : something went wrong !";
#endif

    QObject::connect(eventFilter, &filter::quitRequested, rootObject, [rootObject]{ QMetaObject::invokeMethod(rootObject, "gracefulQuit", Qt::QueuedConnection); });
    QObject::connect(rootObject, SIGNAL(gracefulShutdownComplete()), eventFilter, SLOT(acceptQuit()));
    QObject::connect(eventFilter, SIGNAL(sequencePressed(QVariant,QVariant)), rootObject, SLOT(sequencePressed(QVariant,QVariant)));
    QObject::connect(eventFilter, SIGNAL(sequenceReleased(QVariant,QVariant)), rootObject, SLOT(sequenceReleased(QVariant,QVariant)));
    QObject::connect(eventFilter, SIGNAL(mousePressed(QVariant,QVariant,QVariant)), rootObject, SLOT(mousePressed(QVariant,QVariant,QVariant)));
    QObject::connect(eventFilter, SIGNAL(mouseReleased(QVariant,QVariant,QVariant)), rootObject, SLOT(mouseReleased(QVariant,QVariant,QVariant)));
    QObject::connect(eventFilter, SIGNAL(userActivity()), rootObject, SLOT(userActivity()));
    QObject::connect(eventFilter, SIGNAL(uriHandler(QUrl)), ipc, SLOT(parseCommand(QUrl)));
    return app.exec();
}
