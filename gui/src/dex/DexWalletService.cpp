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
#include <QThread>
#ifdef Q_OS_LINUX
#include <csignal>
#include <sys/types.h>
#endif
#include <sodium.h>

namespace {
const QString RequestSchema="monzero-xmz-evm-helper-request-v1";
const QString WalletName="dex";
// The trading wallet's service binding (a fixed id; the folder is the app's own)
const QString MainServiceId="6d6f6e7a-6572-4f64-8578-77616c6c6574";
const QString ClaimServiceId="6d6f6e7a-6572-4f64-8578-636c61696d73";
const QString ObserverServiceId="6d6f6e7a-6572-4f64-8578-6f6273657276";
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

void DexWalletService::writePid(const QString &path,qint64 pid) {
    QFile f(path);
    if (pid>0 && f.open(QIODevice::WriteOnly|QIODevice::Truncate)) {f.setPermissions(QFileDevice::ReadOwner|QFileDevice::WriteOwner);f.write(QByteArray::number(pid));}
}

bool DexWalletService::makePasswordFile(const QString &path) {
    if (QFile::exists(path)) return true;
    QByteArray secret(24,'\0');randombytes_buf(secret.data(),secret.size());
    auto hex=secret.toHex();sodium_memzero(secret.data(),secret.size());
    QFile f(path);
    const bool ok=f.open(QIODevice::WriteOnly|QIODevice::NewOnly) && f.setPermissions(QFileDevice::ReadOwner|QFileDevice::WriteOwner) && f.write(hex)==hex.size();
    sodium_memzero(hex.data(),hex.size());
    return ok;
}

// After the app was closed abruptly, an app service is left "active" or "recovery-required". The
// app's own services hold no swap state (only wallet files), so once nothing holds the lock or
// port they may resume. Per-trade swap services never take this path.
// A service left running by an app that was killed: stopped only when it is verifiably ours (the
// wallet RPC the app runs, serving this very folder), never another program on the port
bool DexWalletService::stopLeftover(const QString &serviceDir,const QString &pidPath) {
#ifdef Q_OS_LINUX
    QFile f(pidPath);
    if (!f.open(QIODevice::ReadOnly)) return false;
    bool ok=false;const auto pid=f.readAll().trimmed().toLongLong(&ok);
    if (!ok || pid<=1) return false;
    const auto proc=QStringLiteral("/proc/%1").arg(pid);
    if (QFileInfo(proc+"/exe").symLinkTarget()!=QFileInfo(m_walletRpc).canonicalFilePath()) return false;
    QFile cmd(proc+"/cmdline");
    if (!cmd.open(QIODevice::ReadOnly) || !cmd.readAll().split('\0').contains(QFile::encodeName(serviceDir+"/wallets"))) return false;
    ::kill(pid_t(pid),SIGTERM);
    for (int i=0;i<100 && QFileInfo::exists(proc);++i) QThread::msleep(100);
    if (QFileInfo::exists(proc)) {::kill(pid_t(pid),SIGKILL);QThread::msleep(300);}
    qWarning("DEX wallet service %s: stopped one left running by an earlier session",qPrintable(QFileInfo(serviceDir).fileName()));
    return !QFileInfo::exists(proc);
#else
    Q_UNUSED(serviceDir);Q_UNUSED(pidPath);return false;
#endif
}

bool DexWalletService::resumeAfterUncleanExit(const QString &serviceDir,const QString &portPath) {
    if (!QFileInfo::exists(serviceDir+"/service.binding")) return true;
    const auto statePath=serviceDir+"/service.state";
    QFile state(statePath);
    const auto recorded=state.open(QIODevice::ReadOnly) ? state.read(65) : QByteArray();state.close();
    if (recorded=="monzero-swap-rpc-v1\nstopped\n") return true;
    QLockFile lock(serviceDir+"/service.lock");lock.setStaleLockTime(0);
    const quint16 port=keptPort(portPath,0);
    const auto portFree=[port]{QTcpServer probe;const bool free=port && probe.listen(QHostAddress::LocalHost,port);probe.close();return free;};
    // Another running copy of the app holds the lock: its service is live, never a leftover
    if (!lock.tryLock(0)) {m_error=tr("The DEX wallets are in use by another copy of the app.");return false;}
    if (!portFree()) {
        // Possibly our own service, orphaned when the app was killed
        const auto pidPath=QString(portPath).replace(QStringLiteral(".port"),QStringLiteral(".pid"));
        if (stopLeftover(serviceDir,pidPath)) for (int i=0;i<50 && !portFree();++i) QThread::msleep(100);
    }
    if (!portFree()) {m_error=tr("A DEX wallet service is still running from an earlier session.");return false;}
    for (const auto &name:QDir(serviceDir).entryList({"monzero-wallet-rpc.*.login"},QDir::Files)) QFile::remove(serviceDir+"/"+name);
    QSaveFile reset(statePath);
    const QByteArray stopped="monzero-swap-rpc-v1\nstopped\n";
    const bool ok=reset.open(QIODevice::WriteOnly) && reset.setPermissions(QFileDevice::ReadOwner|QFileDevice::WriteOwner)
        && reset.write(stopped)==stopped.size() && reset.commit();
    lock.unlock();
    if (!ok) {m_error=tr("A DEX wallet service could not be resumed.");return false;}
    qWarning("DEX wallet service %s: resumed after an unclean exit",qPrintable(QFileInfo(serviceDir).fileName()));
    return true;
}

bool DexWalletService::startService(const QString &name,const QString &id,QPointer<SwapWalletRpcProcess> &slot,quint16 &port,std::function<void()> listening) {
    const auto serviceDir=m_root+"/"+name,portPath=m_root+"/"+name+".port";
    if ((!QFileInfo::exists(serviceDir) && !privateDir(serviceDir)) || !resumeAfterUncleanExit(serviceDir,portPath)) return false;
    auto *service=new SwapWalletRpcProcess(this);
    slot=service;port=keptPort(portPath,freePort());
    service->setNetworkFlag(m_networkFlag);
    const auto pidPath=m_root+"/"+name+".pid";
    connect(service,&SwapWalletRpcProcess::processStarted,this,[this,service,name,listening,pidPath]{
        writePid(pidPath,service->processId());
        // The login file appears once the service is listening
        auto *wait=new QTimer(this);wait->setInterval(250);
        auto tries=std::make_shared<int>(0);
        connect(wait,&QTimer::timeout,this,[this,wait,tries,service,name,listening]{
            if (QFile::exists(service->loginFile())) {wait->stop();wait->deleteLater();listening();return;}
            if (++*tries>240) {
                wait->stop();wait->deleteLater();m_state="failed";m_error=tr("The DEX wallet service did not start.");
                qWarning("DEX wallet service %s: no login file at %s",qPrintable(name),qPrintable(service->loginFile()));emit changed();
            }
        });
        wait->start();
    });
    connect(service,&SwapWalletRpcProcess::stopped,this,[this](bool clean){
        if (m_stopping) return;
        m_poll.stop();m_state=clean?"stopped":"failed";if (!clean) m_error=tr("A DEX wallet service stopped unexpectedly.");emit changed();
    });
    if (!service->startBoundWithDigest(m_walletRpc,m_walletRpcSha256,serviceDir,id,m_daemonPort,port)) {
        m_error=tr("A DEX wallet service was refused (its folder is in use or was not stopped cleanly).");return false;
    }
    return true;
}

bool DexWalletService::start() {
    if (m_state!="configured" && m_state!="stopped" && m_state!="failed") return false;
    const auto fail=[this](const QString &why){m_state="failed";if (!why.isEmpty()) m_error=why;qWarning("DEX wallets: %s",qPrintable(m_error));emit changed();return false;};
    if (!privateDir(m_root)) return fail(tr("The DEX folder could not be created."));
    // Passwords made once: the trading wallet's, and the one the swap journal gives the wallets it creates
    if (!makePasswordFile(m_root+"/xmz-wallet.password") || !makePasswordFile(m_root+"/claim-wallet.password"))
        return fail(tr("The DEX wallet password could not be saved."));
    m_state="starting";m_claimUp=m_observerUp=false;m_stopping=false;emit changed();
    // The trading wallet (also the XMZ source when selling XMZ for BTC/LTC), the claim service the
    // swap journal creates claim wallets in, and the observer service it watches XMZ locks with
    if (!startService("xmz-wallet",MainServiceId,m_main,m_walletPort,[this]{openWallet();})) return fail({});
    if (!startService("claim-wallets",ClaimServiceId,m_claim,m_claimPort,[this]{m_claimUp=true;emit changed();})) return fail({});
    if (!startService("observer-wallets",ObserverServiceId,m_observer,m_observerPort,[this]{m_observerUp=true;emit changed();})) return fail({});
    return true;
}

QJsonObject DexWalletService::utxoLocal() const {
    if (!ready() || !m_claimUp || !m_observerUp || !m_claim || !m_observer || !m_main) return {};
    const auto address=m_wallet.value("address").toString();
    if (address.isEmpty()) return {};
    return {{"daemonPort",m_daemonPort},{"claimWalletPort",m_claimPort},{"claimLoginFile",m_claim->loginFile()},
        {"sourceWalletPort",m_walletPort},{"sourceLoginFile",m_main->loginFile()},{"sourceAddress",address},
        {"observerWalletPort",m_observerPort},{"observerLoginFile",m_observer->loginFile()},
        {"walletPasswordFile",m_root+"/claim-wallet.password"}};
}

void DexWalletService::stop() {
    m_poll.stop();m_stopping=true;
    if (m_main) m_main->stop();
    if (m_claim) m_claim->stop();
    if (m_observer) m_observer->stop();
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
    // The trade's journal resumes it; its swap wallet holds no state of its own, so after a crash the
    // service may resume the same way as the app's own services
    if (id.isEmpty() || (!QFileInfo::exists(dir) && !privateDir(dir)) || !resumeAfterUncleanExit(dir,tradeDir+"/swap-service.port")) {done({});return;}
    auto *service=new SwapWalletRpcProcess(this);
    const auto port=keptPort(tradeDir+"/swap-service.port",freePort());
    service->setNetworkFlag(m_networkFlag);
    connect(service,&SwapWalletRpcProcess::processStarted,this,[this,service,tradeDir,port,done]{
        writePid(tradeDir+"/swap-service.pid",service->processId());
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

void DexWalletService::releaseTradeService(const QString &tradeDir) {
    const auto service=m_trades.take(tradeDir);m_tradePorts.remove(tradeDir);
    if (!service) return;
    connect(service.data(),&SwapWalletRpcProcess::stopped,service.data(),&QObject::deleteLater);
    service->stop();
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
