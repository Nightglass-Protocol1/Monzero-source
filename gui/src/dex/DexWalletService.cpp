#include "DexWalletService.h"
#include "BackendExecutable.h"
#include "SwapWalletRpcProcess.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QLockFile>
#include <QSaveFile>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QTcpServer>
#include <QUuid>
#include <sodium.h>

namespace {
const QString RequestSchema="monzero-xmz-evm-helper-request-v1";
const QString WalletName="dex";
// The trading wallet's service binding (a fixed id; the folder is the app's own)
const QString MainServiceId="6d6f6e7a-6572-4f64-8578-77616c6c6574";
// A port kept next to a service folder: the service binding records it, so a restart reuses it
quint16 keptPort(const QString &path,quint16 fresh) {
    QFile f(path);
    if (f.open(QIODevice::ReadOnly)) {bool ok=false;const auto p=f.readAll().trimmed().toUInt(&ok);if (ok && p>1024 && p<65536) return quint16(p);}
    QFile w(path);
    if (fresh && w.open(QIODevice::WriteOnly|QIODevice::Truncate)) {w.write(QByteArray::number(fresh));w.setPermissions(QFileDevice::ReadOwner|QFileDevice::WriteOwner);}
    return fresh;
}
// A trade's service id: BTC/LTC trade ids are UUIDs; an EVM trade id (0x + 64 hex) gives one from its first 16 bytes
QString serviceId(const QString &tradeId) {
    const QUuid direct(tradeId);
    if (!direct.isNull() && direct.toString(QUuid::WithoutBraces)==tradeId) return tradeId;
    static const QRegularExpression hash("\\A0x[0-9a-f]{64}\\z");
    if (!hash.match(tradeId).hasMatch()) return {};
    return QUuid::fromRfc4122(QByteArray::fromHex(tradeId.mid(2,32).toLatin1())).toString(QUuid::WithoutBraces);
}
bool privateDir(const QString &path) {
    return QDir().mkpath(path) && QFile::setPermissions(path,QFileDevice::ReadOwner|QFileDevice::WriteOwner|QFileDevice::ExeOwner);
}
}

DexWalletService::DexWalletService(QObject *parent):QObject(parent) {
    m_poll.setInterval(30000);
    connect(&m_poll,&QTimer::timeout,this,[this]{refresh();});
}
DexWalletService::~DexWalletService() {stop();}

quint16 DexWalletService::freePort() {
    QTcpServer probe;
    if (!probe.listen(QHostAddress::LocalHost,0)) return 0;
    const auto port=probe.serverPort();probe.close();return port;
}

bool DexWalletService::configure(const QString &root,const QString &walletRpc,const QString &walletRpcSha256,quint16 daemonPort,
        const QString &networkFlag,const QString &helper,const QString &helperSha256) {
    if (m_state!="unconfigured" || !QDir::isAbsolutePath(root) || daemonPort==0
        || !BackendExecutable::matches(walletRpc,walletRpcSha256) || !BackendExecutable::matches(helper,helperSha256)
        || (!networkFlag.isEmpty() && networkFlag!="--testnet" && networkFlag!="--stagenet")) return false;
    m_root=QDir::cleanPath(root);m_walletRpc=walletRpc;m_walletRpcSha256=walletRpcSha256;m_daemonPort=daemonPort;
    m_networkFlag=networkFlag;m_helper=helper;m_helperSha256=helperSha256;
    m_state="configured";emit changed();return true;
}

bool DexWalletService::start() {
    if (m_state!="configured" && m_state!="stopped" && m_state!="failed") return false;
    if (!privateDir(m_root)) {m_state="failed";m_error=tr("The DEX folder could not be created.");emit changed();return false;}
    // The trading wallet's password, made once; the service folder holds only the service's files
    const auto passwordPath=m_root+"/xmz-wallet.password";
    if (!QFile::exists(passwordPath)) {
        QByteArray secret(24,'\0');randombytes_buf(secret.data(),secret.size());
        auto hex=secret.toHex();sodium_memzero(secret.data(),secret.size());
        QFile f(passwordPath);
        const bool ok=f.open(QIODevice::WriteOnly|QIODevice::NewOnly) && f.setPermissions(QFileDevice::ReadOwner|QFileDevice::WriteOwner) && f.write(hex)==hex.size();
        sodium_memzero(hex.data(),hex.size());
        if (!ok) {m_state="failed";m_error=tr("The DEX wallet password could not be saved.");emit changed();return false;}
    }
    const auto serviceDir=m_root+"/xmz-wallet";
    if (!QFileInfo::exists(serviceDir) && !privateDir(serviceDir)) return false;
    // After the app was closed abruptly, the trading wallet's service is left "active" or
    // "recovery-required". It holds no swap state (only the wallet files), so once nothing holds
    // its lock or port it may resume. Trade services never take this path.
    const auto statePath=serviceDir+"/service.state";
    if (QFileInfo::exists(serviceDir+"/service.binding")) {
        QFile state(statePath);
        const auto recorded=state.open(QIODevice::ReadOnly) ? state.read(65) : QByteArray();state.close();
        if (recorded!="monzero-swap-rpc-v1\nstopped\n") {
            QLockFile lock(serviceDir+"/service.lock");lock.setStaleLockTime(0);
            const quint16 port=keptPort(m_root+"/xmz-wallet.port",0);
            QTcpServer probe;
            const bool portFree=port && probe.listen(QHostAddress::LocalHost,port);probe.close();
            if (!lock.tryLock(0) || !portFree) {m_state="failed";m_error=tr("The DEX wallet service is still running from an earlier session.");emit changed();return false;}
            for (const auto &name:QDir(serviceDir).entryList({"monzero-wallet-rpc.*.login"},QDir::Files)) QFile::remove(serviceDir+"/"+name);
            QSaveFile reset(statePath);
            const QByteArray stopped="monzero-swap-rpc-v1\nstopped\n";
            const bool ok=reset.open(QIODevice::WriteOnly) && reset.setPermissions(QFileDevice::ReadOwner|QFileDevice::WriteOwner)
                && reset.write(stopped)==stopped.size() && reset.commit();
            lock.unlock();
            if (!ok) {m_state="failed";m_error=tr("The DEX wallet service could not be resumed.");emit changed();return false;}
            qWarning("DEX trading wallet: resumed after an unclean exit");
        }
    }
    auto *service=new SwapWalletRpcProcess(this);
    m_main=service;m_walletPort=keptPort(m_root+"/xmz-wallet.port",freePort());
    service->setNetworkFlag(m_networkFlag);
    connect(service,&SwapWalletRpcProcess::processStarted,this,[this]{
        // The login file appears once the service is listening
        auto *wait=new QTimer(this);wait->setInterval(250);
        auto tries=std::make_shared<int>(0);
        connect(wait,&QTimer::timeout,this,[this,wait,tries]{
            if (m_main && QFile::exists(m_main->loginFile())) {wait->stop();wait->deleteLater();openWallet();return;}
            if (++*tries>240) {wait->stop();wait->deleteLater();m_state="failed";m_error=tr("The DEX wallet service did not start.");qWarning("DEX trading wallet: no login file at %s",m_main?qPrintable(m_main->loginFile()):"?");emit changed();}
        });
        wait->start();
    });
    connect(service,&SwapWalletRpcProcess::stopped,this,[this](bool clean){
        m_poll.stop();m_state=clean?"stopped":"failed";if (!clean) m_error=tr("The DEX wallet service stopped unexpectedly.");emit changed();
    });
    m_state="starting";emit changed();
    if (!service->startBoundWithDigest(m_walletRpc,m_walletRpcSha256,serviceDir,MainServiceId,m_daemonPort,m_walletPort)) {
        m_state="failed";m_error=tr("The DEX wallet service was refused (its folder is in use or was not stopped cleanly).");emit changed();return false;
    }
    return true;
}

void DexWalletService::stop() {
    m_poll.stop();
    if (m_main) m_main->stop();
    for (const auto &s:m_trades) if (s) s->stop();
}

QJsonObject DexWalletService::mainEndpoint() const {
    if (!m_main || !ready()) return {};
    return {{"url",QStringLiteral("http://127.0.0.1:%1").arg(m_walletPort)},{"loginFile",m_main->loginFile()}};
}

bool DexWalletService::runHelper(const QJsonObject &walletRequest,std::function<void(const QJsonObject&)> done) {
    if (!BackendExecutable::matches(m_helper,m_helperSha256)) return false;
    auto *p=new QProcess(this);
    QProcessEnvironment env;env.insert("LANG","C");p->setProcessEnvironment(env);
    const QJsonObject request{{"schema",RequestSchema},{"command","xmzWallet"},{"tradeDir",m_root},{"xmzWallet",walletRequest}};
    ++m_running;
    connect(p,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[this,p,done](int,QProcess::ExitStatus){
        --m_running;
        const auto r=QJsonDocument::fromJson(p->readAllStandardOutput().left(65536)).object();
        p->deleteLater();done(r);
    });
    connect(p,&QProcess::errorOccurred,this,[this,p,done](QProcess::ProcessError e){
        if (e!=QProcess::FailedToStart) return;
        --m_running;p->deleteLater();done({{"ok",false},{"error","the helper did not start"}});
    });
    p->start(m_helper,QStringList{});
    p->write(QJsonDocument(request).toJson(QJsonDocument::Compact));p->closeWriteChannel();
    QTimer::singleShot(120000,p,[p]{if (p->state()!=QProcess::NotRunning) p->kill();});
    return true;
}

void DexWalletService::openWallet() {
    runHelper({{"wallet",QJsonObject{{"url",QStringLiteral("http://127.0.0.1:%1").arg(m_walletPort)},{"loginFile",m_main->loginFile()}}},
        {"action","open"},{"name",WalletName},{"passwordFile",m_root+"/xmz-wallet.password"}},[this](const QJsonObject &r){
        if (!r.value("ok").toBool()) {m_state="failed";m_error=r.value("error").toString().left(300);qWarning("DEX trading wallet: %s",qPrintable(m_error));emit changed();return;}
        m_wallet=r;m_state="ready";m_error.clear();m_poll.start();emit changed();
    });
}

bool DexWalletService::refresh() {
    if (!ready() || m_running>0) return false;
    return runHelper({{"wallet",mainEndpoint()},{"action","status"},{"name",WalletName},{"passwordFile",m_root+"/xmz-wallet.password"}},
        [this](const QJsonObject &r){
            if (r.value("ok").toBool()) {m_wallet=r;m_error.clear();} else m_error=r.value("error").toString().left(300);
            emit changed();
        });
}

bool DexWalletService::send(const QString &address,const QString &atoms) {
    static const QRegularExpression addressPattern("\\A[0-9A-Za-z]{90,110}\\z"),digits("\\A[1-9][0-9]{0,19}\\z");
    if (!ready() || !addressPattern.match(address).hasMatch() || (!atoms.isEmpty() && !digits.match(atoms).hasMatch())) return false;
    QJsonObject request{{"wallet",mainEndpoint()},{"action","send"},{"name",WalletName},{"passwordFile",m_root+"/xmz-wallet.password"},{"to",address}};
    if (!atoms.isEmpty()) request.insert("atoms",atoms);
    return runHelper(request,[this](const QJsonObject &r){
        if (r.value("ok").toBool()) {m_wallet=r;m_error.clear();} else m_error=r.value("error").toString().left(300);
        emit changed();
    });
}

void DexWalletService::tradeService(const QString &tradeDir,const QString &tradeId,std::function<void(const QJsonObject&)> done) {
    if (m_state=="unconfigured" || !QDir::isAbsolutePath(tradeDir)) {done({});return;}
    if (auto existing=m_trades.value(tradeDir)) {
        done(QJsonObject{{"url",QStringLiteral("http://127.0.0.1:%1").arg(m_tradePorts.value(tradeDir))},{"loginFile",existing->loginFile()}});
        return;
    }
    const auto dir=tradeDir+"/swap-service",id=serviceId(tradeId);
    if (id.isEmpty() || (!QFileInfo::exists(dir) && !privateDir(dir))) {done({});return;}
    auto *service=new SwapWalletRpcProcess(this);
    const auto port=keptPort(tradeDir+"/swap-service.port",freePort());
    service->setNetworkFlag(m_networkFlag);
    connect(service,&SwapWalletRpcProcess::processStarted,this,[this,service,tradeDir,port,done]{
        auto *wait=new QTimer(this);wait->setInterval(250);
        auto tries=std::make_shared<int>(0);
        connect(wait,&QTimer::timeout,this,[this,wait,tries,service,tradeDir,port,done]{
            if (QFile::exists(service->loginFile())) {
                wait->stop();wait->deleteLater();
                m_trades.insert(tradeDir,service);m_tradePorts.insert(tradeDir,port);
                done(QJsonObject{{"url",QStringLiteral("http://127.0.0.1:%1").arg(port)},{"loginFile",service->loginFile()}});
            } else if (++*tries>240) {wait->stop();wait->deleteLater();done({});}
        });
        wait->start();
    });
    if (!service->startBoundWithDigest(m_walletRpc,m_walletRpcSha256,dir,id,m_daemonPort,port)) {service->deleteLater();done({});}
}

QVariantMap DexWalletService::status() const {
    const auto atoms=[](const QString &a){
        QString d=a.isEmpty()?QStringLiteral("0"):a;while (d.size()<=11) d.prepend('0');
        QString w=d.left(d.size()-11),f=d.mid(d.size()-11);while (f.endsWith('0')) f.chop(1);
        return f.isEmpty()?w:w+"."+f;
    };
    return {{"state",m_state},{"error",m_error},{"busy",m_running>0},{"address",m_wallet.value("address").toString()},
        {"balance",atoms(m_wallet.value("balanceAtoms").toString())},{"unlocked",atoms(m_wallet.value("unlockedAtoms").toString())},
        {"height",m_wallet.value("height").toDouble()},{"trades",m_trades.size()}};
}
