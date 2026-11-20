#include "SwapWalletRpcProcess.h"
#include "BackendExecutable.h"
#include "LitecoinJournalProtocol.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <QTimer>
#include <QLockFile>
#include <QSaveFile>
#include <QUuid>
#include <QTemporaryFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <sodium.h>
#ifdef Q_OS_LINUX
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#elif defined(Q_OS_WIN)
#include <io.h>
#endif

namespace {
bool syncDirectory(const QString &directory) {
#ifdef Q_OS_LINUX
    const int fd=::open(QFile::encodeName(directory).constData(),O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (fd<0) return false;
    const bool synced=::fsync(fd)==0;::close(fd);return synced;
#elif defined(Q_OS_WIN)
    // NTFS journals folder changes (create, rename) itself; Windows has no fsync for a folder.
    Q_UNUSED(directory);return true;
#else
    Q_UNUSED(directory);return false;
#endif
}
bool durableCommit(QSaveFile &file,const QString &directory) {
#ifdef Q_OS_LINUX
    return file.flush() && ::fsync(file.handle())==0 && file.commit() && syncDirectory(directory);
#elif defined(Q_OS_WIN)
    // _commit flushes the file to disk (FlushFileBuffers); the rename in commit() is journaled by NTFS
    const int fd=file.handle();
    return file.flush() && (fd<0 || ::_commit(fd)==0) && file.commit() && syncDirectory(directory);
#else
    Q_UNUSED(file);Q_UNUSED(directory);return false;
#endif
}
bool privateDirectory(const QString &path) {
#ifdef Q_OS_LINUX
    const QFileInfo directory(path);
    return directory.isAbsolute() && directory.isDir() && !directory.isSymLink()
        && directory.ownerId() == geteuid() && !(directory.permissions() &
        (QFile::ReadGroup | QFile::WriteGroup | QFile::ExeGroup | QFile::ReadOther | QFile::WriteOther | QFile::ExeOther));
#elif defined(Q_OS_WIN)
    // A real folder (not a symlink or junction) under the app's data folder, which Windows keeps
    // private to the user by its ACL; there are no Unix owner/mode bits to check.
    const QFileInfo directory(path);
    if (!directory.isAbsolute() || !directory.isDir() || directory.isSymLink()) return false;
    const DWORD attrs=::GetFileAttributesW(QDir::toNativeSeparators(path).toStdWString().c_str());
    return attrs!=INVALID_FILE_ATTRIBUTES && (attrs&FILE_ATTRIBUTE_DIRECTORY) && !(attrs&FILE_ATTRIBUTE_REPARSE_POINT);
#else
    return false;
#endif
}
bool validProgram(const QString &path) {
    const QFileInfo file(path);
    return file.isAbsolute() && file.isFile() && file.isExecutable() && !file.isSymLink();
}
}

SwapWalletRpcProcess::SwapWalletRpcProcess(QObject *parent)
    : QObject(parent), m_process(new QProcess(this)), m_stopTimer(new QTimer(this)) {
    m_stopTimer->setSingleShot(true);
    connect(m_stopTimer,&QTimer::timeout,m_process,&QProcess::kill);
    connect(m_process,&QProcess::started,this,[this] {
        if (m_stopTimer->isActive()) m_process->terminate();
        else emit processStarted();
    });
    connect(m_process,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,
        [this](int code,QProcess::ExitStatus status) {
            m_terminal = true;
            m_stopTimer->stop();
            const bool clean = finishRecord(status == QProcess::NormalExit && code == 0);
            emit stopped(clean);
        });
    connect(m_process,&QProcess::errorOccurred,this,[this](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart || m_terminal) return;
        m_terminal = true;
        m_stopTimer->stop();
        finishRecord(false);
        emit stopped(false);
    });
}

SwapWalletRpcProcess::~SwapWalletRpcProcess() {
    disconnect(m_process,nullptr,this,nullptr);
    if (m_process->state() != QProcess::NotRunning) {
        m_process->terminate();
        if (!m_process->waitForFinished(1000)) {
            m_process->kill();
            m_process->waitForFinished(1000);
        }
    }
    if (m_used && !m_terminal) finishRecord(m_process->state() == QProcess::NotRunning
        && m_process->exitStatus() == QProcess::NormalExit && m_process->exitCode() == 0);
    // Never recursively remove the directory: it may now contain funded wallets.
}

bool SwapWalletRpcProcess::startFresh(const QString &program, const QString &parent,
    quint16 daemonPort, quint16 walletPort) {
    if (m_used || daemonPort == 0 || walletPort == 0 || daemonPort == walletPort
        || !validProgram(program) || !privateDirectory(parent)) return false;
    QTemporaryDir fresh(QDir(parent).filePath("monzero-swap-rpc-XXXXXX"));
    if (!fresh.isValid() || !QDir(fresh.path()).mkdir("wallets")) return false;
    if (!QFile::setPermissions(QDir(fresh.path()).filePath("wallets"),QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner)) return false;
    fresh.setAutoRemove(false); // Durable recovery data, even on failed startup.
    return launch(program,fresh.path(),daemonPort,walletPort,false);
}

bool SwapWalletRpcProcess::startFreshWithDigest(const QString &program,const QString &digest,
    const QString &parent,quint16 daemonPort,quint16 walletPort) {
    if (m_used || !BackendExecutable::matches(program,digest)) return false;
    return startFresh(program,parent,daemonPort,walletPort);
}

bool SwapWalletRpcProcess::resumeClean(const QString &program, const QString &directory,
    quint16 daemonPort, quint16 walletPort) {
    return launch(program,directory,daemonPort,walletPort,true);
}

bool SwapWalletRpcProcess::resumeCleanWithDigest(const QString &program,const QString &digest,
    const QString &directory,quint16 daemonPort,quint16 walletPort) {
    if (m_used || !BackendExecutable::matches(program,digest)) return false;
    return resumeClean(program,directory,daemonPort,walletPort);
}

bool SwapWalletRpcProcess::startBoundWithDigest(const QString &program,const QString &digest,
    const QString &directory,const QString &tradeId,quint16 daemonPort,quint16 walletPort) {
    const QUuid trade(tradeId);
    if (m_used || trade.isNull() || trade.toString(QUuid::WithoutBraces)!=tradeId
        || !BackendExecutable::matches(program,digest)) return false;
    const auto binding=QJsonDocument(QJsonObject{{"schema","monzero-bound-swap-rpc-v1"},
        {"tradeId",tradeId},{"program",program},{"programSha256",digest},
        {"daemonPort",daemonPort},{"walletPort",walletPort}}).toJson(QJsonDocument::Compact);
    return launch(program,directory,daemonPort,walletPort,false,binding);
}

// A new copy of the app (an upgrade, or the folder moved) brings its own wallet RPC: same trade,
// same ports, another program path or digest. The app has already checked that program against
// the digest compiled into it, so the folder may move to it, but only from a clean stop and only
// when nothing but "program" and "programSha256" differ in an intact (canonical) binding.
bool SwapWalletRpcProcess::rebindProgram(const QString &directory,const QByteArray &binding) {
    const QString bindingPath=QDir(directory).filePath("service.binding"),statePath=QDir(directory).filePath("service.state");
    const auto recorded=LitecoinJournalProtocol::privateConfig(bindingPath);
    const auto before=QJsonDocument::fromJson(recorded),after=QJsonDocument::fromJson(binding);
    if (!before.isObject() || !after.isObject() || before.toJson(QJsonDocument::Compact)!=recorded) return false;
    const auto old=before.object(),next=after.object();
    if (old.keys()!=next.keys() || old.value("schema")!="monzero-bound-swap-rpc-v1") return false;
    for (const auto &key:old.keys())
        if (key!="program" && key!="programSha256" && old.value(key)!=next.value(key)) return false;
    const QFileInfo stateInfo(statePath);
    QFile state(statePath);
    if (!stateInfo.isFile() || stateInfo.isSymLink() || stateInfo.size()>64
        || !state.open(QIODevice::ReadOnly) || state.read(65)!="monzero-swap-rpc-v1\nstopped\n") return false;
    state.close();
    QSaveFile record(bindingPath);
    if (!record.open(QIODevice::WriteOnly) || !record.setPermissions(QFile::ReadOwner | QFile::WriteOwner)
        || record.write(binding)!=binding.size() || !durableCommit(record,directory)) return false;
    qWarning("DEX wallet service %s: now run by %s",qPrintable(QFileInfo(directory).fileName()),qPrintable(next.value("program").toString()));
    return true;
}

bool SwapWalletRpcProcess::launch(const QString &program, const QString &directory,
    quint16 daemonPort, quint16 walletPort, bool resume,const QByteArray &binding) {
    if (m_used || !validProgram(program) || !privateDirectory(directory)
        || (binding.isEmpty() && !privateDirectory(QDir(directory).filePath("wallets")))
        || daemonPort == 0 || walletPort == 0 || daemonPort == walletPort) return false;
    auto lock = std::make_unique<QLockFile>(QDir(directory).filePath("service.lock"));
    lock->setStaleLockTime(0);
    if (!lock->tryLock(0)) return false;
    const QString statePath = QDir(directory).filePath("service.state");
    const QString bindingPath = QDir(directory).filePath("service.binding");
    if (!binding.isEmpty()) {
        const QFileInfo bindingInfo(bindingPath);
        if (bindingInfo.exists() || bindingInfo.isSymLink()) {
            if ((LitecoinJournalProtocol::privateConfig(bindingPath)!=binding && !rebindProgram(directory,binding))
                || !privateDirectory(QDir(directory).filePath("wallets"))) return false;
            resume=true;
        } else {
            // The held lock is the only permitted entry on first initialization.
            if (QDir(directory).entryList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot)
                !=QStringList{"service.lock"}) return false;
            QSaveFile record(bindingPath);
            if (!record.open(QIODevice::WriteOnly) || !record.setPermissions(QFile::ReadOwner | QFile::WriteOwner)
                || record.write(binding)!=binding.size() || !durableCommit(record,directory)
                || !QDir(directory).mkdir("wallets")
                || !QFile::setPermissions(QDir(directory).filePath("wallets"),QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner)
                || !syncDirectory(QDir(directory).filePath("wallets")) || !syncDirectory(directory)) return false;
        }
    } else if (QFileInfo::exists(bindingPath) || QFileInfo(bindingPath).isSymLink()) {
        // Bound directories must never be reopened through an unbound API.
        return false;
    }
    if (resume) {
        const QFileInfo stateInfo(statePath);
        QFile state(statePath);
        if (!stateInfo.isFile() || stateInfo.isSymLink() || stateInfo.size() > 64
            || !state.open(QIODevice::ReadOnly) || state.read(65) != "monzero-swap-rpc-v1\nstopped\n") return false;
    } else if (QFileInfo::exists(statePath)) return false;
    m_directory = directory;
    m_lock = std::move(lock);
    if (!recordState("active")) { m_lock.reset(); return false; }
    // The wallet RPC's login file has a name fixed in its own code: "monzero-wallet-rpc." here,
    // "monero-wallet-rpc." in Monero's (see setMoneroWallet)
    m_login = QDir(m_directory).filePath(QString("%1.%2.login").arg(m_loginPrefix).arg(walletPort));
    m_daemonPort = daemonPort;
    m_walletPort = walletPort;
    m_used = true;
    QProcessEnvironment environment;
    environment.insert("LANG","C");
    m_process->setProcessEnvironment(environment);
    m_process->setWorkingDirectory(m_directory);
    m_process->setStandardOutputFile(QProcess::nullDevice());
    m_process->setStandardErrorFile(QProcess::nullDevice());
    QStringList arguments{"--wallet-dir",QDir(m_directory).filePath("wallets"),
        "--daemon-address",QString("%1:%2").arg(m_daemonHost).arg(daemonPort),
        "--rpc-bind-ip","127.0.0.1","--rpc-bind-port",QString::number(walletPort),
        "--non-interactive","--max-concurrency","2",
        "--log-file",QDir(m_directory).filePath("rpc.log")};
    if (!m_networkFlag.isEmpty()) arguments.prepend(m_networkFlag);
    m_process->start(program,arguments);
    return true;
}

bool SwapWalletRpcProcess::setMoneroWallet() {
    if (m_used) return false;
    m_loginPrefix=QStringLiteral("monero-wallet-rpc");return true;
}

bool SwapWalletRpcProcess::setDaemonHost(const QString &host) {
    static const QRegularExpression name("\\A[A-Za-z0-9]([A-Za-z0-9.-]{0,251}[A-Za-z0-9])?\\z");
    if (m_used || !name.match(host).hasMatch()) return false;
    m_daemonHost=host;return true;
}

bool SwapWalletRpcProcess::setNetworkFlag(const QString &flag) {
    if (m_used || (!flag.isEmpty() && flag!="--testnet" && flag!="--stagenet")) return false;
    m_networkFlag=flag;return true;
}

bool SwapWalletRpcProcess::recordState(const QByteArray &state) {
    if (!m_lock || !m_lock->isLocked()) return false;
    QSaveFile file(QDir(m_directory).filePath("service.state"));
    const QByteArray bytes = "monzero-swap-rpc-v1\n" + state + "\n";
    return file.open(QIODevice::WriteOnly) && file.setPermissions(QFile::ReadOwner | QFile::WriteOwner)
        && file.write(bytes) == bytes.size() && durableCommit(file,m_directory);
}

bool SwapWalletRpcProcess::finishRecord(bool clean) {
    if (m_process->state() != QProcess::NotRunning) return false;
    clean = removeLoginAfterExit() && clean && !m_recoveryRequired && m_operation.isEmpty();
    const bool recorded = recordState(clean ? "stopped" : "recovery-required");
    m_lock.reset();notifyAvailabilityChanged();
    return clean && recorded;
}

bool SwapWalletRpcProcess::walletOperationAvailable() const {
    return m_used && !m_terminal && !m_recoveryRequired && m_operation.isEmpty()
        && !m_stopTimer->isActive() && m_process->state()==QProcess::Running && m_lock && m_lock->isLocked();
}
QString SwapWalletRpcProcess::beginWalletOperation() {
    if (!walletOperationAvailable()) return {};
    if (!recordState("operation-pending")) {
        m_recoveryRequired = true;notifyAvailabilityChanged();
        return {};
    }
    m_operation = QUuid::createUuid().toString(QUuid::WithoutBraces);
    notifyAvailabilityChanged();return m_operation;
}

bool SwapWalletRpcProcess::completeWalletOperation(const QString &token, bool verifiedSuccess) {
    if (token.isEmpty() || token != m_operation || m_terminal || m_recoveryRequired
        || m_stopTimer->isActive() || m_process->state() != QProcess::Running) return false;
    if (!verifiedSuccess) {
        m_recoveryRequired = true;
        recordState("recovery-required");notifyAvailabilityChanged();
        return false;
    }
    if (!recordState("active")) {
        m_recoveryRequired = true;notifyAvailabilityChanged();
        return false;
    }
    m_operation.clear();notifyAvailabilityChanged();
    return true;
}

void SwapWalletRpcProcess::stop() {
    if (m_process->state() == QProcess::NotRunning || m_stopTimer->isActive()) return;
    m_stopTimer->start(10000);notifyAvailabilityChanged();
    m_process->terminate();
}

bool SwapWalletRpcProcess::removeLoginAfterExit() {
    if (m_process->state() != QProcess::NotRunning) return false;
    bool clean = true;
    for (const auto &path : {m_helperSession,m_bchSession,m_login}) {
        if (!path.isEmpty() && QFileInfo::exists(path) && !QFile::remove(path)) clean = false;
    }
    return clean;
}

bool SwapWalletRpcProcess::ownsHelperSession(const QString &path) const {
    return !path.isEmpty() && path == m_helperSession && !m_terminal && !m_stopTimer->isActive()
        && m_process->state() == QProcess::Running;
}

QString SwapWalletRpcProcess::provisionHelperSession(const QString &genesis) {
#ifndef Q_OS_LINUX
    return {};
#else
    if (m_terminal || m_recoveryRequired || !m_operation.isEmpty() || m_stopTimer->isActive()
        || m_process->state() != QProcess::Running || genesis.size()!=64) return {};
    for (const auto ch : genesis) if (!QString("0123456789abcdef").contains(ch)) return {};
    if (!m_helperSession.isEmpty()) return genesis == m_helperGenesis ? m_helperSession : QString();
    const int fd = ::open(QFile::encodeName(m_login).constData(),O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
    if (fd<0) return {};
    struct stat info;
    if (fstat(fd,&info)!=0 || !S_ISREG(info.st_mode) || info.st_uid!=geteuid()
        || ((info.st_mode & 07777)!=0400 && (info.st_mode & 07777)!=0600)
        || info.st_nlink!=1 || info.st_size>1024) { ::close(fd); return {}; }
    QFile login;
    if (!login.open(fd,QIODevice::ReadOnly,QFileDevice::AutoCloseHandle)) { ::close(fd); return {}; }
    struct PrivateBytes {
        QByteArray bytes;
        ~PrivateBytes() { if (!bytes.isEmpty()) sodium_memzero(bytes.data(),bytes.size()); }
    };
    struct PrivateText {
        QString text;
        ~PrivateText() { if (!text.isEmpty()) sodium_memzero(text.data(),text.size()*sizeof(QChar)); }
    };
    PrivateBytes data{login.read(1025)};
    const int colon = data.bytes.indexOf(':');
    if (data.bytes.size()>1024 || colon<=0 || colon==data.bytes.size()-1) return {};
    for (const char ch : data.bytes) if (static_cast<unsigned char>(ch)<33 || static_cast<unsigned char>(ch)>126) return {};
    PrivateText username{QString::fromLatin1(data.bytes.constData(),colon)};
    PrivateText password{QString::fromLatin1(data.bytes.constData()+colon+1,data.bytes.size()-colon-1)};
    PrivateBytes json{QJsonDocument(QJsonObject{{"schema","monzero-native-fee-session-v1"},
        {"network","mainnet"},{"genesis",genesis},{"daemonPort",m_daemonPort},{"walletPort",m_walletPort},
        {"username",username.text},{"password",password.text}}).toJson(QJsonDocument::Compact)};
    if (json.bytes.size()>2048) return {};
    QTemporaryFile session(QDir(m_directory).filePath("helper-session-XXXXXX"));
    if (!session.open() || session.write(json.bytes)!=json.bytes.size() || !session.flush()) return {};
    m_helperSession = session.fileName();
    m_helperGenesis = genesis;
    session.setAutoRemove(false);
    return m_helperSession;
#endif
}


bool SwapWalletRpcProcess::ownsBchNativeSession(const QString &path) const {
#ifndef Q_OS_LINUX
    Q_UNUSED(path);
    return false;
#else
    if (path.isEmpty() || path != m_bchSession || m_terminal || m_recoveryRequired
        || m_stopTimer->isActive() || m_process->state() != QProcess::Running
        || !m_lock || !m_lock->isLocked()) return false;
    const int fd = ::open(QFile::encodeName(path).constData(),O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
    if (fd<0) return false;
    struct stat info;
    if (fstat(fd,&info)!=0 || !S_ISREG(info.st_mode) || info.st_uid!=geteuid()
        || (info.st_mode & 07777)!=0600 || info.st_nlink!=1
        || info.st_size!=m_bchSessionBytes.size()) { ::close(fd); return false; }
    QFile file;
    if (!file.open(fd,QIODevice::ReadOnly,QFileDevice::AutoCloseHandle)) { ::close(fd); return false; }
    return file.read(m_bchSessionBytes.size()+1)==m_bchSessionBytes;
#endif
}

QString SwapWalletRpcProcess::provisionBchNativeSession() {
#ifndef Q_OS_LINUX
    return {};
#else
    if (m_terminal || m_recoveryRequired || !m_operation.isEmpty() || m_stopTimer->isActive()
        || m_process->state()!=QProcess::Running || !m_lock || !m_lock->isLocked()) return {};
    if (!m_bchSession.isEmpty()) return ownsBchNativeSession(m_bchSession) ? m_bchSession : QString();
    // The child creates credentials after its started signal. Refuse until the
    // bounded private regular file exists; helpers validate its credential text.
    const int fd = ::open(QFile::encodeName(m_login).constData(),O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
    if (fd<0) return {};
    struct stat info;
    const bool valid=fstat(fd,&info)==0 && S_ISREG(info.st_mode) && info.st_uid==geteuid()
        && ((info.st_mode & 07777)==0400 || (info.st_mode & 07777)==0600)
        && info.st_nlink==1 && info.st_size>0 && info.st_size<=1024;
    ::close(fd);
    if (!valid) return {};
    const auto bytes=QJsonDocument(QJsonObject{{"schema","monzero-private-bch-native-rpc-v1"},
        {"daemonPort",m_daemonPort},{"walletPort",m_walletPort},{"loginFile",m_login}}).toJson(QJsonDocument::Compact);
    QTemporaryFile session(QDir(m_directory).filePath("bch-native-session-XXXXXX"));
    if (!session.open() || !session.setPermissions(QFile::ReadOwner | QFile::WriteOwner)
        || session.write(bytes)!=bytes.size() || !session.flush()) return {};
    m_bchSession=session.fileName();m_bchSessionBytes=bytes;
    session.setAutoRemove(false);
    return m_bchSession;
#endif
}

void SwapWalletRpcProcess::notifyAvailabilityChanged() {
    QMetaObject::invokeMethod(this,"availabilityChanged",Qt::QueuedConnection);
}

qint64 SwapWalletRpcProcess::processId() const {return m_process->processId();}
