#include "DexDeployment.h"
#include "LitecoinLightJob.h"
#include "LightWalletProfile.h"
#include "LightWalletBackupRestore.h"
#include "LightWalletDiscoveryProtocol.h"
#include "LightWalletImportRecovery.h"
#include "LightWalletImportActivation.h"
#include "LightWalletRecoveryProtocol.h"
#include "LitecoinJournalProtocol.h"
#include "LitecoinPaymentRequest.h"
#include <QJsonDocument>
#include <QPointer>
#include <QProcessEnvironment>
#include <QUuid>
#include <cmath>
#include <limits>
#include <QSettings>
#include <QLockFile>
#include <QDir>
#include <QCryptographicHash>
#include <QDateTime>
namespace {
// Best-effort erasure of worker-owned buffers. Qt/QML/QProcess may make copies;
// this is not a secure-memory guarantee. Credentials never enter status or files.
void erase(QByteArray &bytes) {
    volatile char *data=bytes.data();for (int i=0;i<bytes.size();++i) data[i]=0;bytes.clear();
}
struct Credential {QByteArray bytes;~Credential() {erase(bytes);}};
void appendPassword(QByteArray &packet,const QByteArray &password) {
    const char hex[]="0123456789abcdef";packet.append('"');
    for (const auto byte:password) {
        const auto c=static_cast<unsigned char>(byte);
        if (c=='"' || c=='\\') {packet.append('\\');packet.append(char(c));}
        else if (c<32) {packet.append("\\u00");packet.append(hex[c>>4]);packet.append(hex[c&15]);}
        else packet.append(char(c));
    }
    packet.append('"');
}
bool object(const QByteArray &bytes,QJsonObject *out) {
    const auto document=QJsonDocument::fromJson(bytes);
    if (!document.isObject() || document.toJson(QJsonDocument::Compact)!=bytes) return false;
    *out=document.object();return true;
}
bool number(const QJsonValue &value,quint32 maximum=10000000) {
    return value.isDouble() && value.toDouble()>=0 && value.toDouble()<=maximum
        && std::floor(value.toDouble())==value.toDouble();
}
bool atomic(const QJsonValue &value) {
    bool ok=false;
    if (!value.isString() || !QRegularExpression("\\A(0|[1-9][0-9]{0,19})\\z").match(value.toString()).hasMatch()) return false;
    value.toString().toULongLong(&ok);return ok;
}
bool address(const QJsonValue &value,const QString &network,const QString &coin,bool change=false) {
    return value.isString() && LightWalletProfile::addressShape(coin,network,value.toString(),change);
}
bool hash(const QJsonValue &value) {
    return value.isString() && QRegularExpression("\\A[0-9a-f]{64}\\z").match(value.toString()).hasMatch();
}
}
LitecoinLightJob::LitecoinLightJob(QObject *parent):LitecoinLightJob(QStringLiteral("ltc"),parent) {}
LitecoinLightJob::LitecoinLightJob(const QString &coin,QObject *parent):QObject(parent),m_coin(coin) {
    m_deadline.setSingleShot(true);
    m_recoveryDeadline.setSingleShot(true);
    connect(&m_recoveryDeadline,&QTimer::timeout,this,&LitecoinLightJob::hideRecovery);
    connect(&m_deadline,&QTimer::timeout,this,[this]{finish(false);});
    connect(&m_process,&QProcess::started,this,[this]{
        if (m_process.write(m_input)!=m_input.size()) {finish(false);return;}
        m_process.closeWriteChannel();erase(m_input);
    });
    connect(&m_process,&QProcess::readyReadStandardOutput,this,&LitecoinLightJob::drain);
    connect(&m_process,&QProcess::readyReadStandardError,this,&LitecoinLightJob::drain);
    connect(&m_process,&QProcess::errorOccurred,this,[this](QProcess::ProcessError){finish(false);});
    connect(&m_process,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[this](int code,QProcess::ExitStatus exit){
        QPointer<LitecoinLightJob> guard(this);const auto request=m_request;
        drain();if (guard && request==m_request) finish(code==0 && exit==QProcess::NormalExit && m_errors==0);
    });
}
LitecoinLightJob::~LitecoinLightJob() {
    erase(m_recovery);erase(m_recoveryPassphrase);erase(m_input);erase(m_output);disconnect(&m_process,nullptr,this,nullptr);
    if (m_process.state()!=QProcess::NotRunning) {m_process.kill();m_process.waitForFinished(1000);}
}
static bool lightConfig(const QByteArray &raw,QJsonObject &config,const QString &coin) {
    if (raw.size()>4096 || !object(raw,&config) || (config.size()<4 || config.size()>6)
        || config.value("schema")!=LightWalletProfile::schema(coin,"light",true)
        || !config.value("network").isString() || !LightWalletProfile::networkAllowed(coin,config.value("network").toString())
        || !config.value("cacheDirectory").isString() || !QFileInfo(config.value("cacheDirectory").toString()).isAbsolute()
        || !number(config.value("maximumHeight"))) return false;
    for (const auto &key:config.keys()) if (!QStringList{"schema","network","cacheDirectory","maximumHeight","endpoint","wallet"}.contains(key)) return false;
    const bool wallet=config.contains("wallet") && !config.value("wallet").isNull();
    if (wallet) {
        const auto w=config.value("wallet").toObject();const auto a=w.value("account").toObject();
        for (const auto &key:w.keys()) if (!QStringList{"account","addressBookDirectory","spendDirectory","vault"}.contains(key)) return false;
        if (w.contains("spendDirectory") && !w.value("spendDirectory").isNull()
            && (!w.value("spendDirectory").isString() || !QFileInfo(w.value("spendDirectory").toString()).isAbsolute())) return false;
        if (w.contains("vault") && !w.value("vault").isNull()) {
            const auto v=w.value("vault").toObject();const auto identity=v.value("walletId").toString();const QUuid id(identity);
            if ((v.size()!=2 && v.size()!=3)
                || (v.contains("seedEncoding") && v.value("seedEncoding")!="raw-bip32-seed-hex-v1" && v.value("seedEncoding")!="bip39-english-v1")
                || (v.size()==3 && !v.contains("seedEncoding")) || !v.value("directory").isString() || !QFileInfo(v.value("directory").toString()).isAbsolute()
                || id.isNull() || id.toString(QUuid::WithoutBraces)!=identity) return false;
        }
        if ((w.size()<2 || w.size()>4) || !w.value("addressBookDirectory").isString()
            || !QFileInfo(w.value("addressBookDirectory").toString()).isAbsolute()
            || a.size()!=5 || a.value("schema")!=LightWalletProfile::schema(coin,"public-account")
            || a.value("network")!=config.value("network") || !number(a.value("account"),2147483647)
            || !a.value("masterFingerprint").isString()
            || !QRegularExpression("\\A[0-9a-f]{8}\\z").match(a.value("masterFingerprint").toString()).hasMatch()
            || !a.value("accountXpub").isString() || a.value("accountXpub").toString().isEmpty()
            || a.value("accountXpub").toString().size()>128) return false;
    }
    const bool endpoint=config.contains("endpoint") && !config.value("endpoint").isNull();
    if (endpoint) {
        const auto e=config.value("endpoint").toObject();
        if (e.size()!=2 || !e.value("host").isString() || e.value("host").toString().isEmpty()
            || e.value("host").toString().size()>253 || !number(e.value("port"),65535) || e.value("port").toInt()==0) return false;
    }
    return true;
}
static bool setupConfig(const QByteArray &raw,const QJsonObject &config,const QString &coin) {
    const QStringList fields{"schema","network","setupDirectory","vaultDirectory","addressBookDirectory","backupDirectory",
        "spendDirectory","cacheDirectory","walletId","accountIndex","maximumHeight","endpoint","seedEncoding"};
    if (raw.size()>4096 || (config.size()<11 || config.size()>13)
        || !LightWalletProfile::networkAllowed(coin,config.value("network").toString())
        || !number(config.value("accountIndex"),2147483647) || !number(config.value("maximumHeight"))) return false;
    for (const auto &key:config.keys()) if (!fields.contains(key)) return false;
    if (config.contains("seedEncoding") && config.value("seedEncoding")!="raw-bip32-seed-hex-v1" && config.value("seedEncoding")!="bip39-english-v1") return false;
    const auto identity=config.value("walletId").toString();const QUuid id(identity);
    if (id.isNull() || id.toString(QUuid::WithoutBraces)!=identity) return false;
    QStringList directories;
    for (const auto &key:{"setupDirectory","vaultDirectory","addressBookDirectory","backupDirectory","spendDirectory","cacheDirectory"}) {
        const auto path=config.value(key).toString();const QFileInfo info(path);
        if (!info.isAbsolute() || !info.isDir() || info.canonicalFilePath()!=path || directories.contains(path)) return false;
        directories.append(path);
    }
    if (config.contains("endpoint") && !config.value("endpoint").isNull()) {
        const auto endpoint=config.value("endpoint").toObject();
        if (endpoint.size()!=2 || !endpoint.value("host").isString() || endpoint.value("host").toString().isEmpty()
            || endpoint.value("host").toString().size()>253 || !number(endpoint.value("port"),65535) || endpoint.value("port").toInt()==0) return false;
    }
    return true;
}
bool LitecoinLightJob::configure(const QString &path,const QString &expectedHelper,const QString &expectedDigest) {
    if (!LightWalletProfile::supported(m_coin) || !m_path.isEmpty() || operationBusy()) return false;
    const auto bytes=LitecoinJournalProtocol::privateConfig(path);QJsonObject wrapper,config;
    if (!object(bytes,&wrapper) || wrapper.size()!=4 || wrapper.value("schema")!=LightWalletProfile::schema(m_coin,"light-launcher",true)
        || ((!expectedHelper.isEmpty() || !expectedDigest.isEmpty())
            && (wrapper.value("helper")!=expectedHelper || wrapper.value("helperDigest")!=expectedDigest))
        || !wrapper.value("configFile").isString()
        || !BackendExecutable::matches(wrapper.value("helper").toString(),wrapper.value("helperDigest").toString())) return false;
    const auto configPath=wrapper.value("configFile").toString();
    const auto raw=LitecoinJournalProtocol::privateConfig(configPath);
    if (!object(raw,&config)) return false;
    const bool setup=config.value("schema")==LightWalletProfile::schema(m_coin,"setup",true);
    const bool backupSetup=config.value("schema")==LightWalletProfile::schema(m_coin,"backup-setup",true);
    if (!(backupSetup?LightWalletBackupRestore::settings(config,m_coin):setup?setupConfig(raw,config,m_coin):lightConfig(raw,config,m_coin))) return false;
    m_backupSetup=backupSetup;
    const bool wallet=!setup && config.contains("wallet") && !config.value("wallet").isNull();
    const bool endpoint=!setup && config.contains("endpoint") && !config.value("endpoint").isNull();
    m_setupConfigured=setup;
    m_path=path;m_wrapper=bytes;m_configPath=configPath;m_config=raw;
    m_program=wrapper.value("helper").toString();m_digest=wrapper.value("helperDigest").toString();
    m_network=config.value("network").toString();m_maximumHeight=quint32(config.value("maximumHeight").toDouble());
    m_spendConfigured=wallet && config.value("wallet").toObject().value("spendDirectory").isString();
    m_vaultConfigured=wallet && config.value("wallet").toObject().value("vault").isObject();
    m_walletConfigured=wallet;m_endpoint=endpoint;m_sourcesValid=true;m_state=setup?"setup-ready":"ready";
    if (backupSetup) {
        m_state="backup-setup-ready";
        const auto root=config.value("setupDirectory").toString();
        const auto receipt=root+"/litecoin-backup-restore-complete.json";
        if (QFileInfo::exists(receipt) || QFileInfo(receipt).isSymLink()) {
            if(!adoptBackupRestore()){m_state="backup-restore-invalid";m_sourcesValid=false;}
        } else if(QFileInfo::exists(root+"/litecoin-backup-restore-intent.json") || QFileInfo::exists(root+"/litecoin-wallet-restored.json")) m_state="backup-restore-incomplete";
    }
    if (setup) {
        QJsonObject saved;
        if (object(LitecoinJournalProtocol::privateConfig(config.value("setupDirectory").toString()+"/litecoin-wallet-settings.json"),&saved)
            && saved.value("setupMode")=="mnemonic-import-v1") {
            m_importPending=true;QByteArray raw;QJsonObject receipt;
            if (createdConfiguration(raw,receipt,true)) {
                receipt.insert("backupVerified",false);m_wallet=receipt;m_state="wallet-import-pending";
                m_pendingConfig=raw;m_pendingConfigPath=receipt.value("configurationFile").toString();
                m_importRecoveryRequests=LightWalletImportRecovery::list(config.value("setupDirectory").toString(),m_config,raw,m_coin);
                const auto root=config.value("setupDirectory").toString();
                if (QFileInfo::exists(root+"/litecoin-wallet-activated.json")) {
                    if (!adoptActivatedImport()) {m_state="wallet-import-activation-invalid";m_sourcesValid=false;}
                } else if (QFileInfo::exists(root+"/litecoin-wallet-active.json") || QFileInfo::exists(root+"/litecoin-wallet-activation.json"))
                    m_state="wallet-import-activation-incomplete";

            }
            else m_state="wallet-import-incomplete";
        }
    }
    emit changed();return true;
}
bool LitecoinLightJob::sourcesMatch() const {
    return m_sourcesValid && BackendExecutable::matches(m_program,m_digest)
        && LitecoinJournalProtocol::privateConfig(m_path)==m_wrapper
        && LitecoinJournalProtocol::privateConfig(m_configPath)==m_config
        && (m_pendingConfigPath.isEmpty() || LitecoinJournalProtocol::privateConfig(m_pendingConfigPath)==m_pendingConfig)
        && (m_activationReceiptPath.isEmpty() || LitecoinJournalProtocol::privateConfig(m_activationReceiptPath)==m_activationReceiptBytes)
        && (m_backupIntentPath.isEmpty() || LitecoinJournalProtocol::privateConfig(m_backupIntentPath)==m_backupIntentBytes)
        && (m_bootstrapPath.isEmpty() || LitecoinJournalProtocol::privateConfig(m_bootstrapPath)==m_bootstrap);
}
bool LitecoinLightJob::fundingConfiguration(QString &path,QByteArray &bytes,QString &helper,QString &digest,const QString &expectedCoin) const {
    path.clear();bytes.clear();helper.clear();digest.clear();
    const bool swapChain=expectedCoin==DexDeployment::litecoinLightCoin();
    if(!QStringList{"ltc","btc","bch"}.contains(expectedCoin) || m_coin!=expectedCoin
        || m_network!=(swapChain?DexDeployment::litecoinLightNetwork():QString("regtest")) || m_busy || m_process.state()!=QProcess::NotRunning
        || m_setupConfigured || m_backupSetup || m_importPending || !m_walletConfigured
        || !m_spendConfigured || !m_vaultConfigured || !m_endpoint || !sourcesMatch())return false;
    path=m_configPath;bytes=m_config;helper=m_program;digest=m_digest;return true;
}
bool LitecoinLightJob::acquireFunding(QObject *owner,const QString &expectedCoin) {
    if(!owner || owner->thread()!=thread() || operationBusy())return false;
    QString path,helper,digest;QByteArray bytes;
    if(!fundingConfiguration(path,bytes,helper,digest,expectedCoin))return false;
    m_fundingOwner=owner;return true;
}
void LitecoinLightJob::releaseFunding(QObject *owner) {
    if(m_fundingOwner!=owner)return;
    m_fundingOwner.clear();emit changed();
}
QVariantMap LitecoinLightJob::status() const {
    const auto configuredWallet=QJsonDocument::fromJson(m_config).object().value("wallet").toObject();
    const QVariantMap restoreDirectories{{"vault",configuredWallet.value("vault").toObject().value("directory").toString()},
        {"addressBook",configuredWallet.value("addressBookDirectory").toString()}};
    const auto encoding=configuredWallet.value("vault").toObject().value("seedEncoding").toString("raw-bip32-seed-hex-v1");
    const bool available=!m_path.isEmpty() && m_sourcesValid && !operationBusy() && m_process.state()==QProcess::NotRunning;
    return {{"walletFolder",m_path.isEmpty()?QString{}:QFileInfo(m_path).absolutePath()},{"configured",!m_path.isEmpty()},{"busy",operationBusy()},{"dexFundingBusy",!m_fundingOwner.isNull()},{"state",m_state},{"network",m_network},{"coin",m_coin},
        {"chain",m_chain.toVariantMap()},{"synchronization",m_synchronization.toVariantMap()},
        {"recoveryEncoding",encoding},{"recoveryAvailable",available && m_walletConfigured && m_vaultConfigured},
        {"recoveryVisible",!m_recovery.isEmpty()},
        {"configurationDigest",m_walletConfigured?QString::fromLatin1(QCryptographicHash::hash(m_config,QCryptographicHash::Sha256).toHex()):QString{}},
        {"wallet",m_wallet.toVariantMap()},{"walletConfigured",m_walletConfigured},{"requestId",m_request},
        {"paymentRequests",m_paymentRequests.toVariantList()},{"walletRequestsAvailable",available && m_walletConfigured && m_spendConfigured},
        {"walletSignedRecoveryAvailable",available && m_walletConfigured && m_spendConfigured},
        {"walletBroadcastStatusAvailable",available && m_walletConfigured && m_spendConfigured && m_endpoint},
        {"backupSetupConfigured",m_backupSetup},{"backupSetup",m_backupSetup?QJsonDocument::fromJson(m_config).object().toVariantMap():QVariantMap{}},
        {"backupImportPreviewAvailable",available && m_backupSetup},{"backupImportAvailable",available && m_backupSetup && !m_backupImportReview.isEmpty()},
        {"backupImportReview",m_backupImportReview.toVariantMap()},{"backupRestoration",m_backupRestoration.toVariantMap()},
        {"setupConfigured",m_setupConfigured},{"creation",m_setupConfigured?QJsonDocument::fromJson(m_config).object().toVariantMap():QVariantMap{}},
        {"creationDigest",m_setupConfigured?QString::fromLatin1(QCryptographicHash::hash(m_config,QCryptographicHash::Sha256).toHex()):QString{}},
        {"importDiscovery",m_discoveryReview.toVariantMap()},
        {"importActivated",!m_importActivation.isEmpty()},{"importActivation",m_importActivation.toVariantMap()},
        {"importActivationAvailable",available && m_importPending && m_state=="wallet-import-recovered" && !m_importRecoveryReceipt.isEmpty()},
        {"importActivationDigest",m_importRecoveryReceipt.isEmpty()?QString{}:LightWalletDiscoveryProtocol::digest(QJsonDocument(m_importRecoveryReceipt).toJson(QJsonDocument::Compact))},
        {"importRecoveryRequests",m_importRecoveryRequests.toVariantList()},{"importRecovery",m_importRecoveryReceipt.toVariantMap()},
        {"importRecoveryAvailable",available && m_importPending && (!m_discoveryReview.isEmpty() || !m_importRecoveryRequests.isEmpty())},
        {"importDiscoveryAvailable",available && m_importPending && m_setupConfigured && m_state!="wallet-import-incomplete" && QJsonDocument::fromJson(m_config).object().value("endpoint").isObject()},
        {"importReview",m_importReview.toVariantMap()},{"walletImportPending",m_importPending},
        {"walletImportAvailable",available && m_setupConfigured && QJsonDocument::fromJson(m_config).object().value("seedEncoding")=="bip39-english-v1"},
        {"walletCreateAvailable",available && m_setupConfigured && !m_importPending && m_state!="wallet-created" && m_state!="wallet-created-review"},
        {"walletCreatedReviewAvailable",available && m_setupConfigured && !m_importPending},
        {"walletCreatedOpenAvailable",available && m_setupConfigured && (m_state=="wallet-created" || m_state=="wallet-created-review")},
        {"walletBackupConfigured",m_walletConfigured && m_vaultConfigured},
        {"walletRestoreAvailable",available && m_walletConfigured && m_vaultConfigured && m_state=="wallet-backup-verified"},
        {"restoreDirectories",restoreDirectories},
        {"walletBackupAvailable",available && m_walletConfigured && m_vaultConfigured},
        {"backupDirectory",(m_state=="wallet-backup-saved" || m_state=="wallet-backup-verified" || m_state=="wallet-restored")?m_backupDirectory:QString{}},
        {"walletReconcileAvailable",available && m_walletConfigured && m_spendConfigured && m_endpoint},
        {"walletBroadcastAvailable",available && m_spendConfigured && m_endpoint && m_state=="wallet-broadcast-status"
            && (m_wallet.value("broadcastStatus")=="no-local-attempt" || m_wallet.value("broadcastStatus")=="outcome-unknown")},
        {"walletSignConfigured",m_walletConfigured && m_spendConfigured && m_vaultConfigured},
        {"walletSignAvailable",available && m_vaultConfigured && m_spendConfigured && m_endpoint && m_state=="wallet-reopened"},
        {"walletReopenAvailable",available && m_walletConfigured && m_spendConfigured && m_endpoint},
        {"walletEstimateAvailable",available && m_spendConfigured && m_walletConfigured && m_endpoint},
        {"walletPrepareConfigured",m_walletConfigured && m_spendConfigured},{"walletPrepareAvailable",available && m_walletConfigured && m_spendConfigured && m_endpoint},
        {"walletReceiveAvailable",available && m_walletConfigured},{"walletInitAvailable",available && m_walletConfigured && m_importActivation.isEmpty() && m_backupRestoration.isEmpty()},{"walletRefreshAvailable",available && m_walletConfigured && m_endpoint},
        {"refreshAvailable",available && !m_setupConfigured && !m_backupSetup},{"syncAvailable",available && m_endpoint && !m_setupConfigured && !m_backupSetup},{"executable",false}};
}
bool LitecoinLightJob::adoptBackupRestore(const QJsonObject &expected) {
    if(!m_backupSetup)return false;
    const auto settings=QJsonDocument::fromJson(m_config).object();const auto root=settings.value("setupDirectory").toString();
    const auto intentPath=root+"/litecoin-backup-restore-intent.json",receiptPath=root+"/litecoin-backup-restore-complete.json";
    const auto intent=LitecoinJournalProtocol::privateConfig(intentPath),receiptBytes=LitecoinJournalProtocol::privateConfig(receiptPath);
    QJsonObject review,receipt,config;
    if(!object(intent,&review) || !LightWalletBackupRestore::review(review,settings,m_coin,false)
        || !object(receiptBytes,&receipt) || receipt!=LightWalletBackupRestore::completion(review)
        || (!expected.isEmpty() && receipt!=expected))return false;
    const auto raw=LitecoinJournalProtocol::privateConfig(receipt.value("configurationFile").toString());
    if(raw!=QJsonDocument(review.value("configuration").toObject()).toJson(QJsonDocument::Compact) || !lightConfig(raw,config,m_coin))return false;
    m_backupIntentPath=intentPath;m_backupIntentBytes=intent;m_activationReceiptPath=receiptPath;m_activationReceiptBytes=receiptBytes;
    m_bootstrapPath=m_configPath;m_bootstrap=m_config;m_configPath=receipt.value("configurationFile").toString();m_config=raw;
    m_backupSetup=false;m_walletConfigured=true;m_spendConfigured=true;m_vaultConfigured=true;m_endpoint=config.value("endpoint").isObject();
    m_backupRestoration=receipt;m_backupImportReview={};m_wallet={};m_chain={};m_state="ready";return true;
}
bool LitecoinLightJob::previewBackupImport(const QString &password) {
    Credential credential{password.toUtf8()};
    if(!m_backupSetup || operationBusy() || credential.bytes.isEmpty() || credential.bytes.size()>1024)return false;
    m_backupImportReview={};return run("wallet-backup-preview",0,{},QString{}, {},{},credential.bytes);
}
bool LitecoinLightJob::importBackup(const QString &digest,const QString &password,bool approved) {
    Credential credential{password.toUtf8()};
    if(!approved || !m_backupSetup || operationBusy() || m_backupImportReview.isEmpty() || credential.bytes.isEmpty() || credential.bytes.size()>1024
        || digest!=m_backupImportReview.value("reviewDigest").toString())return false;
    return run("wallet-backup-import",0,{},QString{}, {},m_backupImportReview,credential.bytes);
}
bool LitecoinLightJob::refresh() {return run("status",0);}
bool LitecoinLightJob::synchronize(quint32 budget) {return budget>0 && budget<=10000000 && run("sync",budget);}
bool LitecoinLightJob::receiveCurrentWallet() {
    if (operationBusy() || !m_walletConfigured) return false;
    const auto fail=[this] {m_wallet={};m_state="unknown";emit changed();return false;};
    if (!sourcesMatch()) return fail();
    // Only a public idempotency token is stored here, never keys or addresses.
    QSettings settings(QSettings::IniFormat,QSettings::UserScope,"Monzero",m_coin=="ltc"?QStringLiteral("LitecoinReceiveRequests"):m_coin+"ReceiveRequests");
    if (!QDir().mkpath(QFileInfo(settings.fileName()).absolutePath())) return fail();
    QLockFile lock(settings.fileName()+".request-lock");
    if (!lock.tryLock(0)) return fail();
    settings.sync();if (settings.status()!=QSettings::NoError) return fail();
    auto wallet=QJsonDocument::fromJson(m_config).object().value("wallet").toObject();
    wallet.remove("spendDirectory");wallet.remove("vault");
    const auto identity=QCryptographicHash::hash(QJsonDocument(wallet).toJson(QJsonDocument::Compact),QCryptographicHash::Sha256).toHex();
    const QString key="accounts/"+QString::fromLatin1(identity);
    QString requestId;
    if (settings.contains(key)) requestId=settings.value(key).toString();
    else {
        requestId=QUuid::createUuid().toString(QUuid::WithoutBraces);
        settings.setValue(key,requestId);settings.sync();
        if (settings.status()!=QSettings::NoError || settings.value(key).toString()!=requestId) return fail();
    }
    if (!receiveWallet(requestId)) return fail();
    return true;
}
bool LitecoinLightJob::receiveWallet(const QString &requestId) {
    const QUuid id(requestId);
    return !id.isNull() && id.toString(QUuid::WithoutBraces)==requestId && run("wallet-receive",0,{},requestId);
}
bool LitecoinLightJob::initializeWallet() {return run("wallet-init",0);}
bool LitecoinLightJob::refreshWallet(quint32 gap,quint32 addresses,quint32 entries) {
    return gap>0 && gap<=500 && addresses>0 && addresses<=500 && entries>0 && entries<=1000
        && run("wallet-refresh",0,{{"gap",double(gap)},{"addressesPerBranch",double(addresses)},{"historyEntriesPerBranch",double(entries)}});
}
namespace {
bool paymentRequestValid(const QJsonObject &payment,const QString &network,const QString &coin,quint32 gap,quint32 addresses,quint32 entries) {
    return gap>0 && gap<=500 && addresses>0 && addresses<=500 && entries>0 && entries<=1000
        && payment.size()==5 && address(payment.value("address"),network,coin)
        && atomic(payment.value("amountAtomic")) && payment.value("amountAtomic")!="0"
        && atomic(payment.value("feeAtomic")) && atomic(payment.value("minimumChangeAtomic"))
        && payment.value("minimumChangeAtomic")!="0" && number(payment.value("minimumConfirmations"))
        && payment.value("minimumConfirmations").toInt()>0;
}
bool paymentReviewValid(const QJsonObject &w,const QJsonObject &payment,const QString &network,const QString &coin) {
    if (w.size()!=12 || w.value("watchOnly")!=QJsonValue(true) || w.value("spendable")!=QJsonValue(false)
        || w.value("prepared")!=QJsonValue(true) || w.value("signed")!=QJsonValue(false)
        || w.value("paymentAddress")!=payment.value("address") || w.value("paymentAtomic")!=payment.value("amountAtomic")
        || w.value("feeAtomic")!=payment.value("feeAtomic") || !atomic(w.value("changeAtomic"))
        || !hash(w.value("txid")) || !hash(w.value("signingCommitment"))
        || !number(w.value("inputCount"),1000) || w.value("inputCount").toInt()==0) return false;
    const auto change=w.value("changeAtomic").toString().toULongLong();
    return change==0?w.value("changeAddress").isNull():address(w.value("changeAddress"),network,coin,true)
        && change>=payment.value("minimumChangeAtomic").toString().toULongLong();
}

}
bool LitecoinLightJob::estimateWallet(const QJsonObject &quote,quint32 gap,quint32 addresses,quint32 entries) {
    if (operationBusy()) return false;
    auto payment=quote;payment.remove("targetBlocks");payment.remove("maximumRateAtomicPerKvb");payment.remove("maximumFeeAtomic");payment.insert("feeAtomic","0");
    if (quote.size()!=7 || !paymentRequestValid(payment,m_network,m_coin,gap,addresses,entries)
        || !number(quote.value("targetBlocks"),1008) || quote.value("targetBlocks").toInt()==0
        || !atomic(quote.value("maximumRateAtomicPerKvb")) || quote.value("maximumRateAtomicPerKvb")=="0"
        || !atomic(quote.value("maximumFeeAtomic"))) {
        m_wallet={};m_chain={};m_state="unknown";emit changed();return false;
    }
    return run("wallet-estimate",0,{{"gap",double(gap)},{"addressesPerBranch",double(addresses)},{"historyEntriesPerBranch",double(entries)}},{},quote);
}
bool LitecoinLightJob::prepareWallet(const QString &requestId,const QJsonObject &payment,quint32 gap,quint32 addresses,quint32 entries) {
    const QUuid id(requestId);
    if (id.isNull() || id.toString(QUuid::WithoutBraces)!=requestId || !m_spendConfigured
        || !paymentRequestValid(payment,m_network,m_coin,gap,addresses,entries)) return false;
    return run("wallet-prepare",0,{{"gap",double(gap)},{"addressesPerBranch",double(addresses)},{"historyEntriesPerBranch",double(entries)}},requestId,payment);
}
bool LitecoinLightJob::reopenWallet(const QString &requestId,const QJsonObject &payment,const QJsonObject &review,quint32 gap,quint32 addresses,quint32 entries) {
    if (operationBusy()) return false;
    const QUuid id(requestId);
    if (id.isNull() || id.toString(QUuid::WithoutBraces)!=requestId || !m_spendConfigured
        || !paymentRequestValid(payment,m_network,m_coin,gap,addresses,entries) || review.size()!=2
        || !hash(review.value("signingCommitment"))
        || !(review.value("changeAddress").isNull() || address(review.value("changeAddress"),m_network,m_coin,true))) {
        m_wallet={};m_chain={};m_state="unknown";emit changed();return false;
    }
    return run("wallet-reopen",0,{{"gap",double(gap)},{"addressesPerBranch",double(addresses)},{"historyEntriesPerBranch",double(entries)}},requestId,payment,review);
}
bool LitecoinLightJob::prepareCurrentWallet(const QJsonObject &payment,quint32 gap,quint32 addresses,quint32 entries) {
    if (operationBusy()) return false;
    const auto fail=[this] {m_wallet={};m_chain={};m_state="unknown";emit changed();return false;};
    if (!m_walletConfigured || !m_spendConfigured || !m_endpoint || !sourcesMatch()
        || !paymentRequestValid(payment,m_network,m_coin,gap,addresses,entries)) return fail();
    auto wallet=QJsonDocument::fromJson(m_config).object().value("wallet").toObject();wallet.remove("vault");
    const QJsonObject context{{"wallet",wallet},{"payment",payment},{"scan",QJsonObject{
        {"gap",double(gap)},{"addressesPerBranch",double(addresses)},{"historyEntriesPerBranch",double(entries)}}}};
    QString requestId;
    if (!LitecoinPaymentRequest::retain(wallet.value("spendDirectory").toString(),context,requestId)) return fail();
    if (!prepareWallet(requestId,payment,gap,addresses,entries)) return fail();
    m_uiContext=context;return true;
}
bool LitecoinLightJob::recoverSignedWallet(const QString &requestId,const QJsonObject &payment,const QJsonObject &savedReview) {
    if (operationBusy()) return false;
    const QUuid id(requestId);
    if (id.isNull() || id.toString(QUuid::WithoutBraces)!=requestId || !m_spendConfigured
        || !paymentRequestValid(payment,m_network,m_coin,1,1,1) || !paymentReviewValid(savedReview,payment,m_network,m_coin)) {
        m_wallet={};m_chain={};m_state="unknown";emit changed();return false;
    }
    const QJsonObject review{{"signingCommitment",savedReview.value("signingCommitment")},{"changeAddress",savedReview.value("changeAddress")}};
    if (!run("wallet-signed",0,{},requestId,payment,review)) return false;
    m_signReview=savedReview;return true;
}
bool LitecoinLightJob::signReviewedWallet(const QString &commitment,const QString &password) {
    if (operationBusy()) return false;
    Credential credential{password.toUtf8()};
    if (!m_vaultConfigured || !m_spendConfigured || m_state!="wallet-reopened" || credential.bytes.isEmpty() || credential.bytes.size()>1024
        || !hash(commitment) || m_wallet.value("signingCommitment")!=commitment || m_retainedReview.value("signingCommitment")!=commitment) {
        m_wallet={};m_chain={};m_state="unknown";emit changed();return false;
    }
    const auto expected=m_wallet;const auto payment=m_payment;const auto review=m_retainedReview;
    const auto scan=m_scan;const auto request=m_request;
    if (!run("wallet-sign",0,scan,request,payment,review,credential.bytes)) return false;
    m_signReview=expected;return true;
}
bool LitecoinLightJob::checkBroadcastWallet(const QString &requestId,const QJsonObject &payment,const QJsonObject &savedReview,quint32 gap,quint32 addresses,quint32 entries) {
    if (operationBusy()) return false;
    const QUuid id(requestId);
    if (id.isNull() || id.toString(QUuid::WithoutBraces)!=requestId || !m_spendConfigured || !m_endpoint
        || !paymentRequestValid(payment,m_network,m_coin,gap,addresses,entries) || !paymentReviewValid(savedReview,payment,m_network,m_coin)) {
        m_wallet={};m_chain={};m_state="unknown";emit changed();return false;
    }
    const QJsonObject review{{"signingCommitment",savedReview.value("signingCommitment")},{"changeAddress",savedReview.value("changeAddress")}};
    const QJsonObject scan{{"gap",double(gap)},{"addressesPerBranch",double(addresses)},{"historyEntriesPerBranch",double(entries)}};
    if (!run("wallet-broadcast-status",0,scan,requestId,payment,review)) return false;
    m_signReview=savedReview;return true;
}
bool LitecoinLightJob::broadcastReviewedWallet(const QString &commitment,const QString &txid,const QJsonObject &endpoint) {
    if (operationBusy()) return false;
    if (!status().value("walletBroadcastAvailable").toBool() || !hash(commitment) || !hash(txid)
        || m_wallet.value("signingCommitment")!=commitment || m_wallet.value("txid")!=txid
        || m_wallet.value("broadcastEndpoint").toObject()!=endpoint
        || QJsonDocument::fromJson(m_config).object().value("endpoint").toObject()!=endpoint) {
        m_wallet={};m_chain={};m_state="unknown";emit changed();return false;
    }
    auto expected=m_wallet;expected.remove("recovered");expected.remove("broadcastStatus");
    expected.remove("broadcastEndpoint");expected.remove("confirmationStatus");expected.insert("signed",false);
    const auto payment=m_payment;const auto review=m_retainedReview;const auto scan=m_scan;const auto request=m_request;
    auto approval=endpoint;approval.insert("approvedCommitment",commitment);approval.insert("approvedTxid",txid);
    if (!run("wallet-broadcast",0,scan,request,payment,review,{},approval)) return false;
    m_signReview=expected;return true;
}
bool LitecoinLightJob::reconcileWallet(const QString &requestId,const QJsonObject &payment,const QJsonObject &savedReview,quint32 maximumHeaders,quint32 maximumHistory) {
    if (operationBusy()) return false;
    const QUuid id(requestId);
    if (id.isNull() || id.toString(QUuid::WithoutBraces)!=requestId || !m_spendConfigured || !m_endpoint
        || maximumHeaders==0 || maximumHeaders>10000000 || maximumHistory==0 || maximumHistory>1000
        || !paymentRequestValid(payment,m_network,m_coin,1,1,1) || !paymentReviewValid(savedReview,payment,m_network,m_coin)) {
        m_wallet={};m_chain={};m_state="unknown";emit changed();return false;
    }
    const QJsonObject review{{"signingCommitment",savedReview.value("signingCommitment")},{"changeAddress",savedReview.value("changeAddress")}};
    if (!run("wallet-reconcile",maximumHeaders,{{"maximumHistoryEntries",double(maximumHistory)}},requestId,payment,review)) return false;
    m_signReview=savedReview;return true;
}
bool LitecoinLightJob::createdConfiguration(QByteArray &raw,QJsonObject &receipt,bool imported) const {
    if (!m_setupConfigured) return false;
    const auto setup=QJsonDocument::fromJson(m_config).object();const auto directory=setup.value("setupDirectory").toString();
    QJsonObject ready;
    if (!object(LitecoinJournalProtocol::privateConfig(directory+"/litecoin-wallet-ready.json"),&ready)
        || ready.size()!=4 || ready.value("schema")!=LightWalletProfile::schema(m_coin,"wallet-ready")
        || ready.value("wallet")!=setup.value("walletId") || !hash(ready.value("backupDigest"))) return false;
    const QJsonObject paths{{"vault",setup.value("vaultDirectory")},{"addresses",setup.value("addressBookDirectory")},{"backup",setup.value("backupDirectory")}};
    QJsonObject settings{{"schema",LightWalletProfile::schema(m_coin,"wallet-settings")},{"network",setup.value("network")},{"wallet",setup.value("walletId")},
        {"account",setup.value("accountIndex")},{"directories",paths},{"cache",setup.value("cacheDirectory")},{"spends",setup.value("spendDirectory")},
        {"maximumHeight",setup.value("maximumHeight")},{"endpoint",setup.contains("endpoint")?setup.value("endpoint"):QJsonValue(QJsonValue::Null)}};
    if (setup.value("seedEncoding")=="bip39-english-v1") settings.insert("seedEncoding",setup.value("seedEncoding"));
    if (imported) settings.insert("setupMode","mnemonic-import-v1");
    if (LitecoinJournalProtocol::privateConfig(directory+"/litecoin-wallet-settings.json")!=QJsonDocument(settings).toJson(QJsonDocument::Compact)) return false;
    const auto path=directory+"/litecoin-wallet.json";raw=LitecoinJournalProtocol::privateConfig(path);QJsonObject config;
    if (!object(raw,&config) || ready.value("account").toObject().value("account")!=setup.value("accountIndex")) return false;
    const QJsonObject recovery{{"status","discovery-required"},{"addressRecordsRestored",false},{"activeSwapRecovery",false}};
    auto normalized=config;QJsonObject checked;
    if (imported) {
        if (config.value("schema")!=LightWalletProfile::schema(m_coin,"light-recovery-pending",true) || config.value("recovery").toObject()!=recovery) return false;
        normalized.remove("recovery");normalized.insert("schema",LightWalletProfile::schema(m_coin,"light",true));
    }
    if (!lightConfig(QJsonDocument(normalized).toJson(QJsonDocument::Compact),checked,m_coin)) return false;
    QJsonObject expected{{"schema",LightWalletProfile::schema(m_coin,"light",true)},{"network",setup.value("network")},
        {"cacheDirectory",setup.value("cacheDirectory")},{"maximumHeight",setup.value("maximumHeight")},
        {"wallet",QJsonObject{{"account",ready.value("account")},{"addressBookDirectory",setup.value("addressBookDirectory")},
            {"spendDirectory",setup.value("spendDirectory")},{"vault",QJsonObject{{"directory",setup.value("vaultDirectory")},{"walletId",setup.value("walletId")}}}}}};
    if (setup.value("seedEncoding")=="bip39-english-v1") {
        auto wallet=expected.value("wallet").toObject();auto vault=wallet.value("vault").toObject();
        vault.insert("seedEncoding",setup.value("seedEncoding"));wallet.insert("vault",vault);expected.insert("wallet",wallet);
    }
    if (setup.value("endpoint").isObject()) expected.insert("endpoint",setup.value("endpoint"));
    if (imported) {expected.insert("schema",LightWalletProfile::schema(m_coin,"light-recovery-pending",true));expected.insert("recovery",recovery);}
    if (config!=expected) return false;
    receipt={{"watchOnly",true},{"spendable",false},{"created",true},{"backupVerified",true},{"configurationFile",path},
        {"configurationDigest",QString::fromLatin1(QCryptographicHash::hash(raw,QCryptographicHash::Sha256).toHex())},
        {"backupDigest",ready.value("backupDigest")},{"walletId",setup.value("walletId")},{"receivingAllocated",0},{"changeAllocated",0},
        {"scope","seed-and-address-allocations"},{"allocationFreshness","unassessed"},{"activeSwapRecovery",false}};
    if (imported) receipt={{"imported",true},{"spendable",false},{"discoveryRequired",true},{"configurationFile",path},
        {"configurationDigest",receipt.value("configurationDigest")},{"backupVerified",true},{"backupDigest",ready.value("backupDigest")},
        {"walletId",setup.value("walletId")},{"addressRecordsRestored",false},{"activeSwapRecovery",false}};
    return true;
}
bool LitecoinLightJob::previewMnemonicImport(const QString &digest,const QString &phrase,const QString &passphrase) {
    if (operationBusy()) return false;
    Credential material{phrase.toUtf8()},additional{passphrase.toUtf8()};m_importReview={};
    if (!m_setupConfigured || QJsonDocument::fromJson(m_config).object().value("seedEncoding")!="bip39-english-v1"
        || material.bytes.isEmpty() || material.bytes.size()>1024 || additional.bytes.size()>1024
        || !hash(digest) || digest!=QString::fromLatin1(QCryptographicHash::hash(m_config,QCryptographicHash::Sha256).toHex())) return false;
    return run("wallet-import-preview",0,{},QString{}, {},{{"approvedConfigDigest",digest}}, {}, {},QString{},QString{},material.bytes,additional.bytes);
}
bool LitecoinLightJob::importMnemonic(const QString &digest,const QString &phrase,const QString &passphrase,const QString &password,const QString &confirmation,bool approved) {
    if (operationBusy()) return false;
    Credential material{phrase.toUtf8()},additional{passphrase.toUtf8()},credential{password.toUtf8()};
    if (!approved || !m_setupConfigured || m_importReview.isEmpty() || digest!=m_importReview.value("reviewDigest").toString()
        || password!=confirmation || credential.bytes.isEmpty() || credential.bytes.size()>1024
        || material.bytes.isEmpty() || material.bytes.size()>1024 || additional.bytes.size()>1024) return false;
    const QJsonObject approval{{"approvedConfigDigest",m_importReview.value("review").toObject().value("configurationDigest")},{"approvedReview",digest}};
    return run("wallet-import",0,{},QString{}, {},approval,credential.bytes,{},QString{},QString{},material.bytes,additional.bytes);
}
bool LitecoinLightJob::discoverImportedWallet(quint32 receiveMinimum,quint32 changeMinimum,quint32 gap,quint32 addresses,quint32 entries,quint32 headers,bool approved) {
    if (!approved || !m_importPending || !m_setupConfigured || operationBusy() || gap==0 || gap>500 || addresses==0 || addresses>500
        || entries==0 || entries>1000 || headers>10000 || receiveMinimum>10000 || changeMinimum>10000
        || receiveMinimum+gap>addresses || changeMinimum+gap>addresses) return false;
    QByteArray raw;QJsonObject receipt;
    if (!sourcesMatch() || !createdConfiguration(raw,receipt,true) || !QJsonDocument::fromJson(raw).object().value("endpoint").isObject()) return false;
    m_pendingConfig=raw;m_pendingConfigPath=receipt.value("configurationFile").toString();m_discoveryReview={};
    return run("wallet-import-discover",headers,{{"gap",double(gap)},{"addressesPerBranch",double(addresses)},{"historyEntriesPerBranch",double(entries)}},
        {},{},{{"knownMinimum",QJsonArray{double(receiveMinimum),double(changeMinimum)}}});
}
bool LitecoinLightJob::adoptActivatedImport(const QJsonObject &expected) {
    if (!m_setupConfigured || !m_importPending || m_pendingConfig.isEmpty()) return false;
    const auto root=QJsonDocument::fromJson(m_config).object().value("setupDirectory").toString();
    QByteArray raw;QJsonObject receipt,config;
    if (!LightWalletImportActivation::load(root,m_pendingConfig,m_coin,raw,receipt)
        || (!expected.isEmpty() && receipt!=expected) || !lightConfig(raw,config,m_coin)) return false;
    m_activationReceiptPath=root+"/litecoin-wallet-activated.json";m_activationReceiptBytes=QJsonDocument(receipt).toJson(QJsonDocument::Compact);
    m_bootstrapPath=m_configPath;m_bootstrap=m_config;m_configPath=receipt.value("configurationFile").toString();m_config=raw;
    m_setupConfigured=false;m_importPending=false;m_walletConfigured=true;m_spendConfigured=true;m_vaultConfigured=true;
    m_endpoint=config.value("endpoint").isObject();m_importActivation=receipt;
    m_wallet={};m_chain={};m_synchronization={};m_paymentRequests={};m_importRecoveryReceipt={};
    m_importRecoveryRequests={};m_discoveryReview={};m_discoveryResponse={};m_state="ready";return true;
}
bool LitecoinLightJob::activateImportedWallet(const QString &digest,const QString &password,bool approved) {
    Credential credential{password.toUtf8()};
    if (!approved || operationBusy() || !m_setupConfigured || !m_importPending || m_state!="wallet-import-recovered"
        || m_importRecoveryReceipt.isEmpty() || credential.bytes.isEmpty() || credential.bytes.size()>1024
        || !hash(digest) || digest!=LightWalletDiscoveryProtocol::digest(QJsonDocument(m_importRecoveryReceipt).toJson(QJsonDocument::Compact))
        || !sourcesMatch()) return false;
    const auto root=QJsonDocument::fromJson(m_config).object().value("setupDirectory").toString();
    const auto saved=m_retainedReview.value("response").toObject();
    const auto session=LightWalletImportRecovery::directory(root,saved.value("reviewDigest").toString());
    if (m_importRecoveryReceipt.value("backupDirectory")!=session+"/backup") return false;
    return run("wallet-import-activate",0,{}, {},{},{{"recovery",m_importRecoveryReceipt},{"journalDirectory",session+"/journal"}},credential.bytes);
}
bool LitecoinLightJob::recoverImportedWallet(const QString &digest,const QString &password,bool approved) {
    Credential credential{password.toUtf8()};
    if (!approved || !m_importPending || !m_setupConfigured || operationBusy() || !hash(digest)
        || credential.bytes.isEmpty() || credential.bytes.size()>1024 || !sourcesMatch()) return false;
    QByteArray raw;QJsonObject receipt;
    if (!createdConfiguration(raw,receipt,true)) return false;
    m_pendingConfig=raw;m_pendingConfigPath=receipt.value("configurationFile").toString();
    const auto root=QJsonDocument::fromJson(m_config).object().value("setupDirectory").toString();
    QJsonObject record=LightWalletImportRecovery::load(root,digest,m_config,raw,m_coin);
    if (record.isEmpty() && m_discoveryReview.value("reviewDigest")==digest && !m_discoveryResponse.isEmpty()) {
        record={{"schema",LightWalletProfile::schema(m_coin,"light-import-recovery-ui")},
            {"setupDigest",LightWalletDiscoveryProtocol::digest(m_config)},{"configurationDigest",LightWalletDiscoveryProtocol::digest(raw)},
            {"response",m_discoveryResponse},{"scan",m_scan},{"knownMinimum",m_retainedReview.value("knownMinimum")},
            {"maximumDownloadHeaders",double(m_budget)}};
    }
    if (!LightWalletImportRecovery::valid(record,m_config,raw,m_coin) || !LightWalletImportRecovery::retain(root,record)) return false;
    m_importRecoveryRequests=LightWalletImportRecovery::list(root,m_config,raw,m_coin);
    return run("wallet-import-recover",quint32(record.value("maximumDownloadHeaders").toInt()),record.value("scan").toObject(),{}, {},record,credential.bytes);
}
bool LitecoinLightJob::createWallet(const QString &digest,const QString &password,const QString &confirmation) {
    if (operationBusy()) return false;
    Credential credential{password.toUtf8()};
    if (!m_setupConfigured || m_importPending || password!=confirmation || credential.bytes.isEmpty() || credential.bytes.size()>1024
        || !hash(digest) || digest!=QString::fromLatin1(QCryptographicHash::hash(m_config,QCryptographicHash::Sha256).toHex())) {
        m_wallet={};m_chain={};m_state="unknown";emit changed();return false;
    }
    return run("wallet-create",0,{},QString{}, {},{{"approvedConfigDigest",digest}},credential.bytes);
}
bool LitecoinLightJob::reviewCreatedWallet() {
    if (operationBusy()) return false;
    QByteArray raw;QJsonObject receipt;m_wallet={};m_chain={};
    if (!sourcesMatch() || !createdConfiguration(raw,receipt)) {m_state="unknown";emit changed();return false;}
    receipt.insert("created",false);receipt.insert("backupVerified",false);
    m_wallet=receipt;m_state="wallet-created-review";emit changed();return true;
}
bool LitecoinLightJob::openCreatedWallet(const QString &digest) {
    if (operationBusy()) return false;
    QByteArray raw;QJsonObject receipt;
    if (!sourcesMatch() || !m_setupConfigured || (m_state!="wallet-created" && m_state!="wallet-created-review")
        || !hash(digest) || m_wallet.value("configurationDigest")!=digest || !createdConfiguration(raw,receipt)
        || receipt.value("configurationDigest")!=digest || receipt.value("backupDigest")!=m_wallet.value("backupDigest")) {
        m_wallet={};m_chain={};m_state="unknown";emit changed();return false;
    }
    const auto config=QJsonDocument::fromJson(raw).object();
    m_bootstrapPath=m_configPath;m_bootstrap=m_config;
    m_configPath=receipt.value("configurationFile").toString();m_config=raw;m_setupConfigured=false;
    m_walletConfigured=true;m_spendConfigured=true;m_vaultConfigured=true;m_endpoint=config.value("endpoint").isObject();
    m_wallet={};m_chain={};m_synchronization={};m_paymentRequests={};m_operation="";m_state="ready";emit changed();return true;
}
bool LitecoinLightJob::restoreWallet(const QString &directory,const QString &digest,const QString &password) {
    if (operationBusy()) return false;
    Credential credential{password.toUtf8()};
    if (!m_walletConfigured || !m_vaultConfigured || m_state!="wallet-backup-verified"
        || directory!=m_backupDirectory || !hash(digest) || m_wallet.value("backupDigest")!=digest
        || credential.bytes.isEmpty() || credential.bytes.size()>1024) {
        m_wallet={};m_chain={};m_state="unknown";emit changed();return false;
    }
    const auto expected=m_wallet;
    if (!run("wallet-restore",0,{},QString{}, {}, {},credential.bytes,{},directory,digest)) return false;
    m_backupReview=expected;return true;
}
bool LitecoinLightJob::backupWallet(const QString &directory,const QString &password,bool save) {
    if (operationBusy()) return false;
    Credential credential{password.toUtf8()};
    const QFileInfo destination(directory);
    if (!m_walletConfigured || !m_vaultConfigured || credential.bytes.isEmpty() || credential.bytes.size()>1024
        || !destination.isAbsolute() || !destination.isDir() || destination.canonicalFilePath()!=directory) {
        m_wallet={};m_chain={};m_state="unknown";emit changed();return false;
    }
    return run(save?"wallet-backup-save":"wallet-backup-verify",0,{},QString{}, {}, {},credential.bytes,{},directory);
}
bool LitecoinLightJob::loadPaymentRequests() {
    if (operationBusy()) return false;
    m_paymentRequests={};m_wallet={};m_chain={};
    const auto fail=[this] {m_state="unknown";emit changed();return false;};
    if (!m_spendConfigured || !sourcesMatch()) return fail();
    auto wallet=QJsonDocument::fromJson(m_config).object().value("wallet").toObject();wallet.remove("vault");
    QJsonArray requests;
    if (!LitecoinPaymentRequest::list(wallet.value("spendDirectory").toString(),wallet,requests)) return fail();
    for (const auto &value:requests) {
        const auto item=value.toObject();const auto scan=item.value("scan").toObject();
        if (scan.size()!=3 || !number(scan.value("gap"),500) || !number(scan.value("addressesPerBranch"),500)
            || !number(scan.value("historyEntriesPerBranch"),1000)
            || !paymentRequestValid(item.value("payment").toObject(),m_network,m_coin,scan.value("gap").toInt(),
                scan.value("addressesPerBranch").toInt(),scan.value("historyEntriesPerBranch").toInt())) return fail();
        if (item.contains("review") && (!item.value("review").isObject()
            || !paymentReviewValid(item.value("review").toObject(),item.value("payment").toObject(),m_network,m_coin))) return fail();
    }
    m_paymentRequests=requests;m_state="wallet-requests";emit changed();return true;
}
bool LitecoinLightJob::restoreMnemonicRecovery(const QString &digest,const QString &phrase,const QString &passphrase,const QString &password,const QString &confirmation,bool approved) {
    if (operationBusy()) return false;
    hideRecovery();Credential credential{password.toUtf8()},material{phrase.toUtf8()},additional{passphrase.toUtf8()};
    if (!approved || status().value("recoveryEncoding")!="bip39-english-v1" || !m_walletConfigured || !m_vaultConfigured
        || password!=confirmation || credential.bytes.isEmpty() || credential.bytes.size()>1024
        || material.bytes.isEmpty() || material.bytes.size()>1024 || additional.bytes.size()>1024
        || !hash(digest) || digest!=QString::fromLatin1(QCryptographicHash::hash(m_config,QCryptographicHash::Sha256).toHex())) return false;
    return run("wallet-mnemonic-restore",0,{},QString{}, {},{{"approvedConfigDigest",digest}},credential.bytes,{},QString{},QString{},material.bytes,additional.bytes);
}
bool LitecoinLightJob::restoreRecovery(const QString &digest,const QString &seed,const QString &password,const QString &confirmation,bool approved) {
    if (operationBusy()) return false;
    hideRecovery();Credential credential{password.toUtf8()},material{seed.toLatin1()};
    if (!approved || status().value("recoveryEncoding")!="raw-bip32-seed-hex-v1" || !m_walletConfigured || !m_vaultConfigured || password!=confirmation
        || credential.bytes.isEmpty() || credential.bytes.size()>1024 || seed.size()!=64 || !hash(seed)
        || !hash(digest) || digest!=QString::fromLatin1(QCryptographicHash::hash(m_config,QCryptographicHash::Sha256).toHex())) return false;
    return run("wallet-key-restore",0,{},QString{}, {},{{"approvedConfigDigest",digest}},credential.bytes,{},QString{},QString{},material.bytes);
}
bool LitecoinLightJob::revealRecovery(const QString &digest,const QString &password,bool approved) {
    if (operationBusy()) return false;
    hideRecovery();Credential credential{password.toUtf8()};
    if (!approved || !m_walletConfigured || !m_vaultConfigured || credential.bytes.isEmpty() || credential.bytes.size()>1024
        || !hash(digest) || digest!=QString::fromLatin1(QCryptographicHash::hash(m_config,QCryptographicHash::Sha256).toHex())) return false;
    return run(status().value("recoveryEncoding")=="bip39-english-v1"?"wallet-mnemonic-recovery":"wallet-recovery",0,{},QString{}, {},{{"approvedConfigDigest",digest}},credential.bytes);
}
QString LitecoinLightJob::recoverySeed() const {return QString::fromUtf8(m_recovery);}
QString LitecoinLightJob::recoveryPassphrase() const {return QString::fromUtf8(m_recoveryPassphrase);}
void LitecoinLightJob::hideRecovery() {
    m_recoveryDeadline.stop();
    if (m_busy && (m_operation=="wallet-recovery" || m_operation=="wallet-mnemonic-recovery")) {finish(false);return;}
    if (m_recovery.isEmpty() && m_recoveryPassphrase.isEmpty()) return;
    erase(m_recovery);erase(m_recoveryPassphrase);emit changed();
}
void LitecoinLightJob::cancel() {hideRecovery();if (m_busy) finish(false);}
bool LitecoinLightJob::run(const QString &operation,quint32 budget,const QJsonObject &scan,const QString &requestId,const QJsonObject &payment,const QJsonObject &review,const QByteArray &password,const QJsonObject &broadcastApproval,const QString &backupDirectory,const QString &restoreDigest,const QByteArray &recoverySeed,const QByteArray &recoveryPassphrase) {
    const bool backupImport=operation=="wallet-backup-preview" || operation=="wallet-backup-import";
    if(m_backupSetup && !backupImport)return false;
    const bool sync=operation=="sync";const bool wallet=operation.startsWith("wallet-");
    const bool discovery=operation=="wallet-import-discover",importRecovery=operation=="wallet-import-recover",activation=operation=="wallet-import-activate";
    if (operationBusy() || m_path.isEmpty() || m_process.state()!=QProcess::NotRunning || ((sync || operation=="wallet-reconcile" || operation=="wallet-broadcast" || operation=="wallet-broadcast-status" || operation=="wallet-refresh" || operation=="wallet-prepare" || operation=="wallet-reopen" || operation=="wallet-sign" || operation=="wallet-estimate") && !m_endpoint) || (wallet && !m_walletConfigured && !backupImport && !discovery && !importRecovery && !activation && operation!="wallet-create" && operation!="wallet-import-preview" && operation!="wallet-import") || (m_setupConfigured && !discovery && !importRecovery && !activation && operation!="wallet-create" && operation!="wallet-import-preview" && operation!="wallet-import")) return false;
    if (operation!="wallet-import") m_importReview={};
    if (!discovery) {m_discoveryReview={};m_discoveryResponse={};}
    m_importRecoveryReceipt={};
    erase(m_recovery);erase(m_recoveryPassphrase);m_recoveryDeadline.stop();
    m_chain={};m_synchronization={};m_wallet={};m_paymentRequests={};
    if (!sourcesMatch()) {m_sourcesValid=false;m_state="unknown";emit changed();return false;}
    m_request=requestId.isEmpty()?QUuid::createUuid().toString(QUuid::WithoutBraces):requestId;m_sync=sync;m_budget=budget;
    m_operation=operation;m_backupDirectory=backupDirectory;m_scan=scan;m_payment=payment;m_retainedReview=review;m_uiContext={};m_signReview={};m_backupReview={};
    QJsonObject request{{"schema",LightWalletProfile::schema(m_coin,"light-"+operation+"-request")},{"requestId",m_request}};
    if (backupImport) {
        request.insert("approvedConfigDigest",LightWalletBackupRestore::digest(m_config));
        if(operation=="wallet-backup-import"){request.insert("approvedReview",review.value("reviewDigest"));request.insert("restoreApproved",true);}
    }
    if (discovery) {
        request.insert("approvedConfigDigest",LightWalletDiscoveryProtocol::digest(m_pendingConfig));
        request.insert("discoveryApproved",true);request.insert("maximumDownloadHeaders",double(budget));
        request.insert("knownMinimum",review.value("knownMinimum"));request.insert("walletScan",scan);
    }
    if (activation) {
        const auto recovery=review.value("recovery").toObject();
        request.insert("approvedConfigDigest",LightWalletDiscoveryProtocol::digest(m_pendingConfig));request.insert("activationApproved",true);
        request.insert("approvedCoverage",recovery.value("coverageCommitment"));
        request.insert("approvedRecoveryDigest",LightWalletDiscoveryProtocol::digest(QJsonDocument(recovery).toJson(QJsonDocument::Compact)));
        request.insert("journalDirectory",review.value("journalDirectory"));request.insert("backupDirectory",recovery.value("backupDirectory"));
    }
    if (importRecovery) {
        const auto saved=review.value("response").toObject();
        const auto root=QJsonDocument::fromJson(m_config).object().value("setupDirectory").toString();
        const auto session=LightWalletImportRecovery::directory(root,saved.value("reviewDigest").toString());
        request.insert("approvedConfigDigest",LightWalletDiscoveryProtocol::digest(m_pendingConfig));
        request.insert("recoveryApproved",true);request.insert("approvedReview",saved.value("review"));request.insert("approvedReviewDigest",saved.value("reviewDigest"));
        request.insert("journalDirectory",session+"/journal");request.insert("backupDirectory",session+"/backup");
        request.insert("resume",QFileInfo::exists(session+"/journal/light-recovery-allocation-intent.json"));
        request.insert("maximumDownloadHeaders",double(budget));request.insert("knownMinimum",review.value("knownMinimum"));request.insert("walletScan",scan);
    }
    if (sync || operation=="wallet-reconcile") request.insert("maximumDownloadHeaders",double(budget));
    if (operation=="wallet-reconcile") request.insert("maximumHistoryEntries",scan.value("maximumHistoryEntries"));
    if (operation=="wallet-broadcast" || operation=="wallet-refresh" || operation=="wallet-prepare" || operation=="wallet-reopen" || operation=="wallet-sign" || operation=="wallet-estimate") request.insert("walletScan",scan);
    if (operation=="wallet-reconcile" || operation=="wallet-broadcast" || operation=="wallet-broadcast-status" || operation=="wallet-prepare" || operation=="wallet-reopen" || operation=="wallet-sign" || operation=="wallet-signed") request.insert("payment",payment);
    if (operation=="wallet-reconcile" || operation=="wallet-broadcast" || operation=="wallet-broadcast-status" || operation=="wallet-reopen" || operation=="wallet-sign" || operation=="wallet-signed") request.insert("retainedReview",review);
    if (operation=="wallet-broadcast") request.insert("broadcastApproval",broadcastApproval);
    if (operation=="wallet-estimate") request.insert("feeQuote",payment);
    erase(m_input);m_input=QJsonDocument(request).toJson(QJsonDocument::Compact);
    if (importRecovery || activation || backupImport) {
        if (password.isEmpty() || password.size()>1024) {erase(m_input);return false;}
        m_input.chop(1);m_input.append(",\"password\":");appendPassword(m_input,password);m_input.append("}");
        if (m_input.size()>((activation || backupImport)?8192:16384)) {erase(m_input);return false;}
    }
    if (operation=="wallet-import-preview" || operation=="wallet-import") {
        m_input.chop(1);m_input.append(",\"approvedConfigDigest\":\"");m_input.append(review.value("approvedConfigDigest").toString().toLatin1());
        m_input.append("\",\"seedEncoding\":\"bip39-english-v1\",\"phrase\":");appendPassword(m_input,recoverySeed);
        m_input.append(",\"recoveryPassphrase\":");appendPassword(m_input,recoveryPassphrase);
        if (operation=="wallet-import") {
            m_input.append(",\"importApproved\":true,\"approvedReview\":\"");m_input.append(review.value("approvedReview").toString().toLatin1());
            m_input.append("\",\"password\":");appendPassword(m_input,password);
        }
        m_input.append("}");if (m_input.size()>8192) {erase(m_input);return false;}
    }
    if (operation=="wallet-mnemonic-restore") {
        if (password.isEmpty() || password.size()>1024 || recoverySeed.isEmpty() || recoverySeed.size()>1024 || recoveryPassphrase.size()>1024) {erase(m_input);return false;}
        m_input.chop(1);m_input.append(",\"approvedConfigDigest\":\"");
        m_input.append(review.value("approvedConfigDigest").toString().toLatin1());
        m_input.append("\",\"restoreApproved\":true,\"seedEncoding\":\"bip39-english-v1\",\"phrase\":");
        appendPassword(m_input,recoverySeed);m_input.append(",\"recoveryPassphrase\":");appendPassword(m_input,recoveryPassphrase);
        m_input.append(",\"password\":");appendPassword(m_input,password);m_input.append("}");
        if (m_input.size()>8192) {erase(m_input);return false;}
    }
    if (operation=="wallet-key-restore") {
        if (password.isEmpty() || password.size()>1024 || recoverySeed.size()!=64) {erase(m_input);return false;}
        m_input.chop(1);m_input.append(",\"approvedConfigDigest\":\"");
        m_input.append(review.value("approvedConfigDigest").toString().toLatin1());
        m_input.append("\",\"restoreApproved\":true,\"seedEncoding\":\"raw-bip32-seed-hex-v1\",\"seedHex\":");
        appendPassword(m_input,recoverySeed);m_input.append(",\"password\":");appendPassword(m_input,password);m_input.append("}");
        if (m_input.size()>8192) {erase(m_input);return false;}
    }
    if (operation=="wallet-recovery" || operation=="wallet-mnemonic-recovery") {
        if (password.isEmpty() || password.size()>1024) {erase(m_input);return false;}
        m_input.chop(1);m_input.append(",\"approvedConfigDigest\":\"");
        m_input.append(review.value("approvedConfigDigest").toString().toLatin1());
        m_input.append("\",\"revealApproved\":true,\"password\":");appendPassword(m_input,password);m_input.append("}");
        if (m_input.size()>8192) {erase(m_input);return false;}
    }
    if (operation=="wallet-create") {
        if (password.isEmpty() || password.size()>1024) {erase(m_input);return false;}
        m_input.chop(1);m_input.append(",\"createApproval\":{\"approvedConfigDigest\":\"");
        m_input.append(review.value("approvedConfigDigest").toString().toLatin1());m_input.append("\",\"password\":");
        appendPassword(m_input,password);m_input.append("}}");
        if (m_input.size()>8192) {erase(m_input);return false;}
    }
    if (operation=="wallet-sign") {
        if (password.isEmpty() || password.size()>1024) {erase(m_input);return false;}
        m_input.chop(1);m_input.append(",\"signingApproval\":{\"approvedCommitment\":\"");
        m_input.append(review.value("signingCommitment").toString().toLatin1());m_input.append("\",\"password\":");
        appendPassword(m_input,password);m_input.append("}}");
        if (m_input.size()>8192) {erase(m_input);return false;}
    }
    if (operation=="wallet-backup-save" || operation=="wallet-backup-verify" || operation=="wallet-restore") {
        if (password.isEmpty() || password.size()>1024) {erase(m_input);return false;}
        QJsonObject publicApproval{{"directory",backupDirectory}};
        if (operation=="wallet-restore") publicApproval.insert("approvedDigest",restoreDigest);
        QByteArray approval=QJsonDocument(publicApproval).toJson(QJsonDocument::Compact);
        approval.chop(1);m_input.chop(1);
        m_input.append(operation=="wallet-restore"?",\"restoreApproval\":":",\"backupApproval\":");m_input.append(approval);
        m_input.append(",\"password\":");appendPassword(m_input,password);m_input.append("}}");
        if (m_input.size()>8192) {erase(m_input);return false;}
    }
    erase(m_output);m_errors=0;m_busy=true;m_startedAt=QDateTime::currentSecsSinceEpoch();
    m_state=sync?"synchronizing":wallet?"wallet-loading":"loading";
    QProcessEnvironment environment;environment.insert("LANG","C");
    // Preserve only explicit platform certificate-store overrides, not a general
    // inherited process environment. The connector still verifies hostname/chain.
    const auto system=QProcessEnvironment::systemEnvironment();
    for (const auto name:{"SSL_CERT_FILE","SSL_CERT_DIR"}) if (system.contains(name)) environment.insert(name,system.value(name));
    m_process.setProcessEnvironment(environment);m_process.setProcessChannelMode(QProcess::SeparateChannels);
    m_deadline.start(3600000);m_process.start(m_program,{"--"+operation+"-stdio-v1",(discovery || importRecovery || activation)?m_pendingConfigPath:m_configPath});emit changed();return true;
}
void LitecoinLightJob::drain() {
    if (!m_busy) return;
    m_process.setReadChannel(QProcess::StandardOutput);m_output+=m_process.read(4097-m_output.size());
    m_process.setReadChannel(QProcess::StandardError);m_errors+=m_process.read(1025-m_errors).size();
    if (m_output.size()>4096 || m_errors>1024) finish(false);
}
bool LitecoinLightJob::validResponse(const QJsonObject &r) const {
    const QString operation=m_operation;
    if (operation=="wallet-create") {
        QByteArray raw;QJsonObject expected;
        return r.size()==4 && r.value("schema")==LightWalletProfile::schema(m_coin,"light-wallet-create-response")
            && r.value("requestId")==m_request && r.value("executable")==QJsonValue(false)
            && createdConfiguration(raw,expected) && r.value("wallet")==expected;
    }
    if (operation=="wallet-restore") {
        const auto w=r.value("wallet").toObject();auto normalized=w;
        normalized.remove("restored");normalized.insert("backupVerified",true);normalized.insert("backupSaved",false);
        return r.size()==4 && r.value("schema")==LightWalletProfile::schema(m_coin,"light-wallet-restore-response")
            && r.value("requestId")==m_request && r.value("executable")==QJsonValue(false)
            && w.size()==10 && w.value("restored")==QJsonValue(true) && normalized==m_backupReview;
    }
    if (operation=="wallet-backup-save" || operation=="wallet-backup-verify") {
        const auto w=r.value("wallet").toObject();
        const auto vault=QJsonDocument::fromJson(m_config).object().value("wallet").toObject().value("vault").toObject();
        return r.size()==4 && r.value("schema")==LightWalletProfile::schema(m_coin,"light-"+operation+"-response")
            && r.value("requestId")==m_request && r.value("executable")==QJsonValue(false)
            && w.size()==11 && w.value("watchOnly")==QJsonValue(true) && w.value("spendable")==QJsonValue(false)
            && w.value("backupVerified")==QJsonValue(true) && w.value("backupSaved")==QJsonValue(operation=="wallet-backup-save")
            && hash(w.value("backupDigest")) && w.value("walletId")==vault.value("walletId")
            && number(w.value("receivingAllocated"),10000) && number(w.value("changeAllocated"),10000)
            && w.value("receivingAllocated").toDouble()+w.value("changeAllocated").toDouble()<=10000
            && w.value("scope")=="seed-and-address-allocations" && w.value("allocationFreshness")=="unassessed"
            && w.value("activeSwapRecovery")==QJsonValue(false);
    }
    if (r.size()!=5 || r.value("schema")!=LightWalletProfile::schema(m_coin,"light-"+operation+"-response")
        || r.value("requestId")!=m_request || !r.value("executable").isBool() || r.value("executable").toBool()
        || !r.value("chain").isObject()) return false;
    const auto c=r.value("chain").toObject();
    if (c.size()!=5 || c.value("network")!=m_network || c.value("freshness")!="unassessed"
        || !number(c.value("height"),m_maximumHeight) || !hash(c.value("tip"))
        || !c.value("chainWork").isString() || !QRegularExpression("\\A[1-9][0-9]{0,89}\\z").match(c.value("chainWork").toString()).hasMatch()) return false;
    if (operation.startsWith("wallet-")) {
        if (!r.value("wallet").isObject()) return false;
        const auto w=r.value("wallet").toObject();
        if (operation=="wallet-reconcile" || operation=="wallet-broadcast" || operation=="wallet-broadcast-status") {
            const bool sending=operation=="wallet-broadcast";
            const bool reconciling=operation=="wallet-reconcile";
            const auto status=w.value("broadcastStatus");
            if (w.size()!=(sending?15:reconciling?17:16) || w.value("watchOnly")!=QJsonValue(!sending) || w.value("signed")!=QJsonValue(true)
                || (!reconciling && w.value("confirmationStatus")!="unassessed") || !w.value("broadcastEndpoint").isObject()
                || w.value("broadcastEndpoint")!=QJsonDocument::fromJson(m_config).object().value("endpoint")
                || (sending?status!="server-acknowledged":(w.value("recovered")!=QJsonValue(true)
                    || (status!="no-local-attempt" && status!="outcome-unknown" && status!="server-acknowledged")))) return false;
            if (reconciling) {
                const auto o=w.value("paymentObservation").toObject();const auto checked=o.value("observedAtUnix");
                if (o.size()!=6 || o.value("selectedHeight")!=c.value("height") || o.value("selectedTip")!=c.value("tip")
                    || !number(o.value("confirmations"),m_maximumHeight) || !checked.isDouble()
                    || std::floor(checked.toDouble())!=checked.toDouble() || checked.toDouble()<=0
                    || checked.toDouble()<double(m_startedAt) || checked.toDouble()>double(QDateTime::currentSecsSinceEpoch()+1)) return false;
                const auto position=w.value("confirmationStatus");
                if (position=="included") {
                    if (!number(o.value("height"),quint32(c.value("height").toDouble())) || o.value("height").toDouble()==0
                        || !hash(o.value("blockHash")) || o.value("confirmations").toDouble()!=c.value("height").toDouble()-o.value("height").toDouble()+1
                        || (o.value("height")==c.value("height") && o.value("blockHash")!=c.value("tip"))) return false;
                } else if ((position!="not-reported" && position!="reported-pending") || !o.value("height").isNull()
                    || !o.value("blockHash").isNull() || o.value("confirmations").toDouble()!=0) return false;
            }
            auto unsignedReview=w;unsignedReview.remove("recovered");unsignedReview.remove("broadcastStatus");
            unsignedReview.remove("broadcastEndpoint");unsignedReview.remove("confirmationStatus");
            unsignedReview.remove("paymentObservation");
            unsignedReview.insert("signed",false);unsignedReview.insert("watchOnly",true);
            // Legacy P2PKH inputs acquire scriptSig while signing, so their
            // final txid differs from the unsigned review. The pinned helper
            // verifies the retained bytes against the original commitment.
            // Broadcast still requires the exact final txid the user approved.
            auto expected=m_signReview;
            if ((m_coin=="bch" || m_coin=="doge") && operation!="wallet-broadcast")
                expected.insert("txid",unsignedReview.value("txid"));
            return unsignedReview==expected && paymentReviewValid(unsignedReview,m_payment,m_network,m_coin);
        }
        if (operation=="wallet-signed") {
            if (w.size()!=14 || w.value("watchOnly")!=QJsonValue(true) || w.value("signed")!=QJsonValue(true)
                || w.value("recovered")!=QJsonValue(true) || w.value("broadcastStatus")!="unassessed") return false;
            auto unsignedReview=w;unsignedReview.remove("recovered");unsignedReview.remove("broadcastStatus");unsignedReview.insert("signed",false);
            // Legacy P2PKH inputs acquire scriptSig while signing, so their
            // final txid differs from the unsigned review. The pinned helper
            // verifies the retained bytes against the original commitment.
            // Broadcast still requires the exact final txid the user approved.
            auto expected=m_signReview;
            if ((m_coin=="bch" || m_coin=="doge") && operation!="wallet-broadcast")
                expected.insert("txid",unsignedReview.value("txid"));
            return unsignedReview==expected && paymentReviewValid(unsignedReview,m_payment,m_network,m_coin);
        }
        if (operation=="wallet-sign") {
            if (w.size()!=13 || w.value("watchOnly")!=QJsonValue(false) || w.value("signed")!=QJsonValue(true)
                || w.value("broadcast")!=QJsonValue(false)) return false;
            auto unsignedReview=w;unsignedReview.remove("broadcast");unsignedReview.insert("watchOnly",true);unsignedReview.insert("signed",false);
            // Legacy P2PKH inputs acquire scriptSig while signing, so their
            // final txid differs from the unsigned review. The pinned helper
            // verifies the retained bytes against the original commitment.
            // Broadcast still requires the exact final txid the user approved.
            auto expected=m_signReview;
            if ((m_coin=="bch" || m_coin=="doge") && operation!="wallet-broadcast")
                expected.insert("txid",unsignedReview.value("txid"));
            return unsignedReview==expected && paymentReviewValid(unsignedReview,m_payment,m_network,m_coin);
        }
        if (!w.value("watchOnly").isBool() || !w.value("watchOnly").toBool()
            || !w.value("spendable").isBool() || w.value("spendable").toBool()) return false;
        if (operation=="wallet-estimate") {
            if (w.size()!=6 || !w.value("prepared").isBool() || w.value("prepared").toBool()
                || w.value("paymentAddress")!=m_payment.value("address") || w.value("paymentAtomic")!=m_payment.value("amountAtomic")) return false;
            if (w.value("feeEstimate").isNull()) return true;
            if (!w.value("feeEstimate").isObject()) return false;
            const auto e=w.value("feeEstimate").toObject();
            if (e.size()!=6 || e.value("targetBlocks")!=m_payment.value("targetBlocks")
                || !atomic(e.value("rateAtomicPerKvb")) || e.value("rateAtomicPerKvb")=="0"
                || !atomic(e.value("feeAtomic")) || !atomic(e.value("changeAtomic"))
                || !number(e.value("virtualBytes"),100000) || e.value("virtualBytes").toInt()==0
                || !number(e.value("inputCount"),1000) || e.value("inputCount").toInt()==0) return false;
            const quint64 rate=e.value("rateAtomicPerKvb").toString().toULongLong();
            const quint64 size=e.value("virtualBytes").toInt(),fee=e.value("feeAtomic").toString().toULongLong();
            const quint64 change=e.value("changeAtomic").toString().toULongLong();
            // Compute ceil(rate * size / 1000) without overflowing or rounding via double.
            const quint64 tail=((rate%1000)*size+999)/1000;
            if (rate/1000>(std::numeric_limits<quint64>::max()-tail)/size) return false;
            return fee==(rate/1000)*size+tail && rate<=m_payment.value("maximumRateAtomicPerKvb").toString().toULongLong()
                && fee<=m_payment.value("maximumFeeAtomic").toString().toULongLong()
                && (change==0 || change>=m_payment.value("minimumChangeAtomic").toString().toULongLong());
        }
        if (operation=="wallet-prepare" || operation=="wallet-reopen" || operation=="wallet-sign") {
            return paymentReviewValid(w,m_payment,m_network,m_coin) && (operation!="wallet-reopen"
                || (w.value("signingCommitment")==m_retainedReview.value("signingCommitment")
                    && w.value("changeAddress")==m_retainedReview.value("changeAddress")));
        }
        if (operation=="wallet-init") return w.size()==5 && w.value("initialized").isBool() && w.value("initialized").toBool()
            && number(w.value("receivingAllocated"),0) && number(w.value("changeAllocated"),0);
        if (operation=="wallet-receive") {
            const auto account=QJsonDocument::fromJson(m_config).object().value("wallet").toObject().value("account").toObject().value("account").toInt();
            const QString pathPrefix=LightWalletProfile::receivePath(m_coin,m_network,account);
            const auto path=w.value("derivationPath").toString();
            const auto indexText=path.mid(pathPrefix.size());bool ok=false;const auto index=indexText.toUInt(&ok);
            return w.size()==6 && w.value("address").isString() && w.value("derivationPath").isString()
                && address(w.value("address"),m_network,m_coin,true)
                && path.startsWith(pathPrefix) && ok && QString::number(index)==indexText
                && number(w.value("receivingAllocated"),10000) && index<quint32(w.value("receivingAllocated").toInt())
                && number(w.value("changeAllocated"),10000)
                && w.value("receivingAllocated").toInt()+w.value("changeAllocated").toInt()<=10000;
        }
        if (w.size()!=12 || w.value("balanceStatus")!="provisional" || w.value("scanStop")!="reported-gap") return false;
        for (const auto key:{"observedConfirmedAtomic","immatureAtomic","pendingSpentAtomic","pendingReceivedAtomic"}) {
            const auto v=w.value(key);bool ok=false;
            if (!v.isString() || !QRegularExpression("\\A(0|[1-9][0-9]{0,19})\\z").match(v.toString()).hasMatch()) return false;
            v.toString().toULongLong(&ok);if (!ok) return false;
        }
        const auto maximum=quint32(m_scan.value("addressesPerBranch").toInt());
        for (const auto key:{"receiveScanned","changeScanned"}) if (!number(w.value(key),maximum)
            || w.value(key).toInt()<m_scan.value("gap").toInt()) return false;
        return number(w.value("pendingTransactions"),quint32(m_scan.value("historyEntriesPerBranch").toInt()*2))
            && number(w.value("confirmedCoinCount"),100000)
            && (w.value("confirmedCoinCount").toInt()!=0 || (w.value("observedConfirmedAtomic")=="0" && w.value("immatureAtomic")=="0"));
    }
    if (!m_sync) return r.value("synchronization").isNull();
    if (!r.value("synchronization").isObject()) return false;
    const auto s=r.value("synchronization").toObject();
    if (s.size()!=5 || !number(s.value("reportedHeight"),m_maximumHeight) || !hash(s.value("reportedTip"))
        || !number(s.value("downloadedHeaders"),m_budget) || !s.value("reportedTipOnSelectedChain").isBool()) return false;
    if (s.value("reportedHeight").toDouble()>c.value("height").toDouble() && s.value("reportedTipOnSelectedChain").toBool()) return false;
    if (s.value("reportedHeight")==c.value("height")
        && s.value("reportedTipOnSelectedChain").toBool()!=(s.value("reportedTip")==c.value("tip"))) return false;
    if (s.value("change").isNull()) return true;
    if (!s.value("change").isObject()) return false;
    const auto change=s.value("change").toObject();
    return change.size()==5 && number(change.value("commonAncestor"),m_maximumHeight)
        && number(change.value("oldHeight"),m_maximumHeight) && hash(change.value("oldTip"))
        && change.value("newHeight")==c.value("height") && change.value("newTip")==c.value("tip")
        && change.value("commonAncestor").toDouble()<=change.value("oldHeight").toDouble()
        && change.value("commonAncestor").toDouble()<change.value("newHeight").toDouble();
}
void LitecoinLightJob::finish(bool success) {
    if (!m_busy) return;
    m_busy=false;m_deadline.stop();if (m_process.state()!=QProcess::NotRunning) m_process.kill();
    QJsonObject response;const bool same=sourcesMatch();m_sourcesValid=same;
    if(m_operation=="wallet-backup-preview" || m_operation=="wallet-backup-import") {
        bool valid=success && same && m_errors==0 && object(m_output,&response)
            && response.value("schema")==LightWalletProfile::schema(m_coin,"light-"+m_operation+"-response")
            && response.value("requestId")==m_request && response.value("executable")==false;
        if(m_operation=="wallet-backup-preview") {
            const auto review=response.value("review").toObject();QJsonObject config;
            valid=valid && response.size()==6 && response.value("configurationDigest")==LightWalletBackupRestore::digest(m_config)
                && LightWalletBackupRestore::review(review,QJsonDocument::fromJson(m_config).object(),m_coin,true)
                && lightConfig(QJsonDocument(review.value("configuration").toObject()).toJson(QJsonDocument::Compact),config,m_coin)
                && response.value("reviewDigest")==LightWalletBackupRestore::digest(QJsonDocument(review).toJson(QJsonDocument::Compact));
            m_backupImportReview=valid?QJsonObject{{"review",review},{"reviewDigest",response.value("reviewDigest")}}:QJsonObject{};
            m_state=valid?"backup-import-reviewed":"unknown";
        } else {
            valid=valid && response.size()==4 && response.value("restoration").isObject()
                && response.value("restoration").toObject()==LightWalletBackupRestore::completion(m_retainedReview.value("review").toObject())
                && adoptBackupRestore(response.value("restoration").toObject());
            if(!valid){m_backupImportReview={};m_state="unknown";}
        }
        erase(m_input);erase(m_output);emit changed();return;
    }
    if (m_operation=="wallet-import-activate") {
        const bool valid=success && same && m_errors==0 && object(m_output,&response) && response.size()==4
            && response.value("schema")==LightWalletProfile::schema(m_coin,"light-wallet-import-activate-response")
            && response.value("requestId")==m_request && response.value("tradingEnabled")==false && response.value("activation").isObject()
            && response.value("activation").toObject().value("recoveryDigest")==LightWalletDiscoveryProtocol::digest(QJsonDocument(m_retainedReview.value("recovery").toObject()).toJson(QJsonDocument::Compact))
            && adoptActivatedImport(response.value("activation").toObject());
        if (!valid) m_state="unknown";
        erase(m_input);erase(m_output);emit changed();return;
    }
    if (m_operation=="wallet-import-recover") {
        const auto config=QJsonDocument::fromJson(m_pendingConfig).object();const auto wallet=config.value("wallet").toObject();
        const auto saved=m_retainedReview.value("response").toObject(),review=saved.value("review").toObject();
        const auto root=QJsonDocument::fromJson(m_config).object().value("setupDirectory").toString();
        const auto session=LightWalletImportRecovery::directory(root,saved.value("reviewDigest").toString());
        const auto recovery=QJsonDocument::fromJson(m_output).object().value("recovery").toObject();
        const auto backup=LitecoinJournalProtocol::privateRecord(session+"/backup/litecoin-wallet.backup",2000000);
        const auto allocations=LitecoinJournalProtocol::privateRecord(wallet.value("addressBookDirectory").toString()+"/litecoin-address-book.json",2000000);
        const auto vault=LitecoinJournalProtocol::privateRecord(wallet.value("vault").toObject().value("directory").toString()+"/litecoin-seed.vault",65536);
        const auto context=LitecoinJournalProtocol::privateConfig(session+"/journal/light-recovery-backup-context.json");
        const auto completed=LitecoinJournalProtocol::privateConfig(session+"/journal/light-recovery-backup-complete.json");
        const bool valid=success && same && m_errors==0 && object(m_output,&response) && response.size()==5
            && response.value("schema")==LightWalletProfile::schema(m_coin,"light-wallet-import-recover-response")
            && response.value("requestId")==m_request && response.value("activationRequired")==true && response.value("executable")==false
            && recovery.size()==15 && recovery.value("schema")=="monzero-light-recovery-backup-complete-v1"
            && recovery.value("configurationDigest")==LightWalletDiscoveryProtocol::digest(m_pendingConfig)
            && recovery.value("coverageCommitment")==review.value("coverageCommitment")
            && recovery.value("walletId")==wallet.value("vault").toObject().value("walletId")
            && recovery.value("backupDirectory")==session+"/backup" && recovery.value("allocated")==review.value("coverage").toObject().value("allocatedAfter")
            && recovery.value("allocationsRetained")==true && recovery.value("backupVerified")==true && recovery.value("activated")==false
            && recovery.value("historyCompleteness")=="unproven" && recovery.value("activeSwapRecovery")==false
            && !backup.isEmpty() && recovery.value("backupDigest")==LightWalletDiscoveryProtocol::digest(backup)
            && !vault.isEmpty() && recovery.value("vaultDigest")==LightWalletDiscoveryProtocol::digest(vault)
            && !allocations.isEmpty() && recovery.value("allocationDigest")==LightWalletDiscoveryProtocol::digest(allocations)
            && !context.isEmpty() && recovery.value("contextDigest")==LightWalletDiscoveryProtocol::digest(context)
            && completed==QJsonDocument(recovery).toJson(QJsonDocument::Compact)
            && LightWalletImportRecovery::load(root,saved.value("reviewDigest").toString(),m_config,m_pendingConfig,m_coin)==m_retainedReview;
        m_importRecoveryReceipt=valid?recovery:QJsonObject{};m_state=valid?"wallet-import-recovered":"unknown";
        erase(m_input);erase(m_output);emit changed();return;
    }
    if (m_operation=="wallet-import-discover") {
        const bool valid=success && same && m_errors==0 && object(m_output,&response)
            && LightWalletDiscoveryProtocol::validate(response,m_pendingConfig,m_coin,m_request,m_scan,m_retainedReview.value("knownMinimum").toArray(),m_budget);
        m_discoveryResponse=valid?response:QJsonObject{};
        m_discoveryReview=valid?QJsonObject{{"review",response.value("review")},{"reviewDigest",response.value("reviewDigest")}}:QJsonObject{};
        m_synchronization=valid?response.value("synchronization").toObject():QJsonObject{};
        m_state=valid?"wallet-import-discovered":"unknown";
        erase(m_input);erase(m_output);emit changed();return;
    }
    if (m_operation=="wallet-import-preview" || m_operation=="wallet-import") {
        const auto setup=QJsonDocument::fromJson(m_config).object();
        bool valid=success && same && m_errors==0 && object(m_output,&response) && response.size()==4
            && response.value("schema")==LightWalletProfile::schema(m_coin,"light-"+m_operation+"-response")
            && response.value("requestId")==m_request;
        if (m_operation=="wallet-import-preview") {
            const auto review=response.value("review").toObject();const auto account=review.value("account").toObject();
            QJsonObject checked;
            const QJsonObject publicConfig{{"schema",LightWalletProfile::schema(m_coin,"light",true)},{"network",m_network},
                {"cacheDirectory",setup.value("cacheDirectory")},{"maximumHeight",setup.value("maximumHeight")},
                {"wallet",QJsonObject{{"account",account},{"addressBookDirectory",setup.value("addressBookDirectory")}}}};
            valid=valid && review.size()==14 && review.value("schema")==LightWalletProfile::schema(m_coin,"light-wallet-import-review")
                && review.value("coin")==m_coin && review.value("network")==m_network && review.value("walletId")==setup.value("walletId")
                && account.value("account")==setup.value("accountIndex")
                && lightConfig(QJsonDocument(publicConfig).toJson(QJsonDocument::Compact),checked,m_coin)
                && review.value("configurationDigest")==QString::fromLatin1(QCryptographicHash::hash(m_config,QCryptographicHash::Sha256).toHex())
                && review.value("seedEncoding")=="bip39-english-v1" && review.value("additionalPassphraseUsed").isBool()
                && number(review.value("wordCount"),24) && QList<int>{12,15,18,21,24}.contains(review.value("wordCount").toInt())
                && review.value("scope")=="keys-only" && review.value("discoveryRequired")==true && review.value("activeSwapRecovery")==false
                && address(review.value("receiveAddress"),m_network,m_coin,true)
                && review.value("receivePath")==LightWalletProfile::receivePath(m_coin,m_network,setup.value("accountIndex").toInt())+"0"
                && response.value("reviewDigest")==QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(review).toJson(QJsonDocument::Compact),QCryptographicHash::Sha256).toHex());
            m_importReview=valid?QJsonObject{{"review",review},{"reviewDigest",response.value("reviewDigest")}}:QJsonObject{};
            m_wallet={};m_state=valid?"wallet-import-reviewed":"unknown";
        } else {
            QByteArray pending;QJsonObject receipt,settings;
            if (object(LitecoinJournalProtocol::privateConfig(setup.value("setupDirectory").toString()+"/litecoin-wallet-settings.json"),&settings)
                && settings.value("setupMode")=="mnemonic-import-v1") m_importPending=true;
            valid=valid && !m_importReview.isEmpty() && response.value("reviewDigest")==m_importReview.value("reviewDigest")
                && createdConfiguration(pending,receipt,true) && response.value("wallet").toObject()==receipt
                && QJsonDocument::fromJson(pending).object().value("wallet").toObject().value("account")==m_importReview.value("review").toObject().value("account");
            m_wallet=valid?receipt:QJsonObject{};m_state=valid?"wallet-import-pending":"unknown";m_importReview={};
        }
        erase(m_input);erase(m_output);emit changed();return;
    }
    if (m_operation=="wallet-recovery" || m_operation=="wallet-mnemonic-recovery") {
        const auto wallet=QJsonDocument::fromJson(m_config).object().value("wallet").toObject();
        const auto identity=wallet.value("vault").toObject().value("walletId").toString();
        const int account=wallet.value("account").toObject().value("account").toInt();
        const bool valid=success && same && m_errors==0 && (m_operation=="wallet-mnemonic-recovery"
            ?LightWalletRecoveryProtocol::parseMnemonic(m_output,m_coin,m_network,m_request,identity,account,m_recovery,m_recoveryPassphrase)
            :LightWalletRecoveryProtocol::parse(m_output,m_coin,m_network,m_request,identity,account,m_recovery));
        if (!valid) {erase(m_recovery);erase(m_recoveryPassphrase);}
        m_wallet={};m_chain={};m_state=valid?"recovery-revealed":"unknown";
        erase(m_input);erase(m_output);if (valid) m_recoveryDeadline.start(30000);emit changed();return;
    }
    if (m_operation=="wallet-key-restore" || m_operation=="wallet-mnemonic-restore") {
        const auto wallet=QJsonDocument::fromJson(m_config).object().value("wallet").toObject();
        const QJsonObject expected{{"schema",LightWalletProfile::schema(m_coin,"light-"+m_operation+"-response")},
            {"requestId",m_request},{"coin",m_coin},{"network",m_network},
            {"walletId",wallet.value("vault").toObject().value("walletId")},
            {"account",wallet.value("account").toObject().value("account")},
            {"restored",true},{"activeSwapRecovery",false},{"addressRecordsRestored",false}};
        const bool valid=success && same && m_errors==0 && object(m_output,&response) && response==expected;
        m_wallet={};m_chain={};m_state=valid?"wallet-key-restored":"unknown";
        erase(m_input);erase(m_output);emit changed();return;
    }
    bool valid=success && same && m_errors==0 && object(m_output,&response) && validResponse(response);
    if (valid && m_operation=="wallet-prepare" && !m_uiContext.isEmpty()) {
        valid=LitecoinPaymentRequest::retainReview(m_uiContext.value("wallet").toObject().value("spendDirectory").toString(),
            m_uiContext,m_request,response.value("wallet").toObject());
    }
    m_uiContext={};
    m_chain=valid?response.value("chain").toObject():QJsonObject{};
    m_synchronization=valid?response.value("synchronization").toObject():QJsonObject{};
    m_wallet=valid?response.value("wallet").toObject():QJsonObject{};
    m_state=valid?(m_operation=="wallet-create"?"wallet-created":m_operation=="wallet-restore"?"wallet-restored":m_operation=="wallet-backup-save"?"wallet-backup-saved":m_operation=="wallet-backup-verify"?"wallet-backup-verified":m_operation=="wallet-reconcile"?"wallet-reconciled":m_operation=="wallet-broadcast"?"wallet-broadcast-acknowledged":m_operation=="wallet-broadcast-status"?"wallet-broadcast-status":m_operation=="wallet-signed"?"wallet-signed-recovered":m_operation=="wallet-sign"?"wallet-signed":m_operation=="wallet-reopen"?"wallet-reopened":m_operation=="wallet-estimate"?"wallet-estimated":m_operation=="wallet-prepare"?"wallet-prepared":m_operation=="wallet-receive"?"wallet-address":m_operation=="wallet-init"?"wallet-ready":m_operation=="wallet-refresh"?"wallet-checked":m_sync?"checked":"loaded"):"unknown";
    m_signReview={};m_backupReview={};erase(m_input);erase(m_output);emit changed();
}
