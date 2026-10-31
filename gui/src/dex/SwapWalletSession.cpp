#include "SwapWalletSession.h"
#include "NativeWalletControlJob.h"
#include "NativeFeePreviewJob.h"
#include "NativeRecoveryJob.h"
#include "BackendExecutable.h"
#include <QFileInfo>
#include <QTimer>
#include <sodium.h>

namespace {
void erase(QByteArray &password) {
    if (!password.isEmpty()) sodium_memzero(password.data(),password.size());
    password.clear();
}
}

SwapWalletSession::SwapWalletSession(SwapWalletRpcProcess *owner,const QString &helper,
    const QString &genesis,QObject *parent)
    : QObject(parent),m_owner(owner),m_preview(new NativeFeePreviewController(this)),
      m_helper(helper),m_genesis(genesis) {
    m_recoveryExpiry=new QTimer(this);
    m_recoveryExpiry->setSingleShot(true);
    m_recoveryExpiry->setTimerType(Qt::PreciseTimer);
    connect(m_recoveryExpiry,&QTimer::timeout,this,&SwapWalletSession::recoveryChanged);
    connect(this,&SwapWalletSession::stateChanged,this,&SwapWalletSession::recoveryChanged);
    connect(m_preview,&NativeFeePreviewController::previewChanged,this,&SwapWalletSession::previewChanged);
    if (!owner) { m_state=Stopped; return; }
    const auto stopped=[this] {
        m_state=Stopped;
        QPointer<SwapWalletSession> guard(this);
        m_preview->cancel();
        if (guard) emit stateChanged();
    };
    connect(owner,&QObject::destroyed,this,stopped);
    connect(owner,&SwapWalletRpcProcess::stopped,this,[this](bool clean) {
        m_state=clean ? Stopped : RecoveryRequired;
        QPointer<SwapWalletSession> guard(this);
        m_preview->cancel();
        if (guard) emit stateChanged();
    });
}

SwapWalletSession::~SwapWalletSession() {
    // Tear down jobs before members and the controller disappear. Job destruction
    // accounts for uncertain operations but must not emit session UI callbacks.
    m_state=Stopped;
    if (m_owner) disconnect(m_owner,nullptr,this,nullptr);
    disconnect(m_preview,nullptr,this,nullptr);
    const auto controls=findChildren<NativeWalletControlJob*>(QString(),Qt::FindDirectChildrenOnly);
    for (auto *job : controls) { disconnect(job,nullptr,this,nullptr); delete job; }
    const auto previews=findChildren<NativeFeePreviewJob*>(QString(),Qt::FindDirectChildrenOnly);
    for (auto *job : previews) { disconnect(job,nullptr,this,nullptr); delete job; }
    const auto recoveries=findChildren<NativeRecoveryJob*>(QString(),Qt::FindDirectChildrenOnly);
    for (auto *job : recoveries) { disconnect(job,nullptr,this,nullptr); delete job; }
}

bool SwapWalletSession::pinRecoveryDirectory(const QString &directory) {
    if (!m_recoveryDirectory.isEmpty()) return directory==m_recoveryDirectory;
    const QFileInfo file(directory);
    if (m_state!=Unknown || m_helperDigest.isEmpty() || !file.isAbsolute()
        || !file.isDir() || file.isSymLink() || file.canonicalFilePath()!=directory) return false;
    // Native reader independently enforces ownership and private permissions.
    m_recoveryDirectory=directory;
    return true;
}

QVariantMap SwapWalletSession::recovery() const {
    QVariantMap result{{"valid",false},{"status","unresolved"},{"retryAllowed",false},
        {"executable",false},{"transportAuthenticated",false}};
    if (m_state==Open && m_recovery) result=m_recovery->result();
    result["checkAvailable"]=m_owner && m_state==Open && !m_recoveryLock.isEmpty()
        && m_recoveryWallet==m_address;
    return result;
}

bool SwapWalletSession::pinRecoveryTarget(const QString &lock,const QString &txid,const QString &minimum) {
    if (!m_owner || m_state!=Open || m_helperDigest.isEmpty() || m_recoveryDirectory.isEmpty()) return false;
    if (!m_recoveryLock.isEmpty()) return lock==m_recoveryLock && txid==m_recoveryTx
        && minimum==m_recoveryMinimum && m_address==m_recoveryWallet;
    NativeRecoveryReceiver validate;
    if (validate.begin({m_genesis,m_address,lock,txid,minimum}).isEmpty()) return false;
    m_recoveryLock=lock; m_recoveryTx=txid; m_recoveryMinimum=minimum; m_recoveryWallet=m_address;
    emit recoveryChanged();
    return true;
}

bool SwapWalletSession::requestPinnedRecovery() {
    if (!recovery().value("checkAvailable").toBool()) return false;
    return checkRecovery(m_recoveryLock,m_recoveryTx,m_recoveryMinimum);
}

bool SwapWalletSession::checkRecovery(const QString &lock,const QString &txid,const QString &minimum) {
    constexpr int timeoutMs=30000;
    if (!m_owner || m_state!=Open || m_helperDigest.isEmpty() || m_recoveryDirectory.isEmpty()) return false;
    const NativeRecoveryReceiver::Expected expected{m_genesis,m_address,lock,txid,minimum};
    NativeRecoveryReceiver validate;
    if (validate.begin(expected,timeoutMs).isEmpty() || !BackendExecutable::matches(m_helper,m_helperDigest)) return false;
    if (m_recovery) { delete m_recovery.data(); m_recovery.clear(); }
    m_state=Busy;
    auto *job=new NativeRecoveryJob(this);
    m_recovery=job;
    connect(job,&NativeRecoveryJob::completed,this,[this,job](bool verified) {
        verified=verified && m_state==Busy && m_owner;
        if (m_state!=Stopped) m_state=verified ? Open : RecoveryRequired;
        if (!verified) { m_recovery.clear(); job->deleteLater(); m_recoveryExpiry->stop(); }
        QPointer<SwapWalletSession> guard(this);
        emit stateChanged();
        if (guard) emit operationCompleted(verified);
    });
    const bool started=job->start(m_helper,m_helperDigest,m_recoveryDirectory,m_owner,expected,timeoutMs);
    if (!started) { m_state=RecoveryRequired; m_recovery.clear(); job->deleteLater(); m_recoveryExpiry->stop(); }
    else m_recoveryExpiry->start(timeoutMs);
    emit stateChanged();
    return started;
}

bool SwapWalletSession::create(const QString &id,QByteArray password) {
    return control("create",id,std::move(password),{});
}
bool SwapWalletSession::pinHelperDigest(const QString &digest) {
    if (!m_helperDigest.isEmpty()) return digest==m_helperDigest;
    if (m_state!=Unknown || !BackendExecutable::matches(m_helper,digest)) return false;
    m_helperDigest=digest;
    return true;
}
bool SwapWalletSession::open(const QString &id,QByteArray password,const QString &address) {
    return control("open",id,std::move(password),address);
}
bool SwapWalletSession::close() { return control("close",{}, {},m_address); }

bool SwapWalletSession::cancelPendingOperation() {
    if (m_state!=Busy) return false;
    if (m_control && m_control->isActive()) { m_control->cancel(); return true; }
    if (m_fee && m_fee->isActive()) { m_fee->cancel(); return true; }
    if (m_recovery && m_recovery->isActive()) { m_recovery->cancel(); return true; }
    return false;
}

bool SwapWalletSession::control(const QString &operation,const QString &id,QByteArray password,
    const QString &expectedAddress) {
    if (!m_owner || !NativeWalletControlJob::validRequest(operation,id,password,expectedAddress)
        || (operation=="close" ? m_state!=Open : (m_state!=Unknown && m_state!=Closed))) {
        erase(password); return false;
    }
    if (!m_helperDigest.isEmpty() && !BackendExecutable::matches(m_helper,m_helperDigest)) {
        erase(password); return false;
    }
    const auto session=m_owner->provisionHelperSession(m_genesis);
    if (session.isEmpty()) { erase(password); return false; }
    if (m_recovery) { delete m_recovery.data(); m_recovery.clear(); }
    m_recoveryExpiry->stop();
    m_state=Busy;
    QPointer<SwapWalletSession> guard(this);
    if (m_fee) { m_fee->deleteLater(); m_fee.clear(); }
    m_preview->cancel();
    if (!guard) { erase(password); return false; }
    auto *job=new NativeWalletControlJob(m_owner,this);
    m_control=job;
    connect(job,&NativeWalletControlJob::completed,this,[this,job,operation](bool verified) {
        // Owner stop/destruction may precede a late helper callback.
        verified=verified && m_state==Busy && m_owner;
        if (verified) {
            m_address=operation=="close" ? QString() : job->result().value("address").toString();
            m_state=operation=="close" ? Closed : Open;
        } else if (m_state!=Stopped) m_state=RecoveryRequired;
        m_control.clear();
        job->deleteLater();
        QPointer<SwapWalletSession> guard(this);
        emit stateChanged();
        if (guard) emit operationCompleted(verified);
    });
    const bool started=job->start(m_helper,session,operation,id,std::move(password),expectedAddress);
    if (!started) { m_state=RecoveryRequired; m_control.clear(); job->deleteLater(); }
    emit stateChanged();
    return started;
}

bool SwapWalletSession::estimateFee(const QString &destination,const QString &amount,
    const QString &account,const QString &priority) {
    if (!m_owner || m_state!=Open) return false;
    const NativeFeePreviewReceiver::Expected expected{m_address,destination,amount,account,priority,m_genesis};
    if (!NativeFeePreviewReceiver::validExpected(expected)) return false;
    if (!m_helperDigest.isEmpty() && !BackendExecutable::matches(m_helper,m_helperDigest)) return false;
    if (m_recovery) { delete m_recovery.data(); m_recovery.clear(); }
    m_recoveryExpiry->stop();
    m_state=Busy;
    if (m_fee) { m_fee->deleteLater(); m_fee.clear(); }
    auto *job=new NativeFeePreviewJob(m_preview,this);
    m_fee=job;
    connect(job,&NativeFeePreviewJob::completed,this,[this,job](bool verified) {
        verified=verified && m_state==Busy && m_owner;
        if (m_state!=Stopped) m_state=verified ? Open : RecoveryRequired;
        // Its destructor cancels its captured preview; retain successful jobs
        // until the next operation so that the displayed result can expire normally.
        if (!verified) { job->deleteLater(); m_fee.clear(); }
        QPointer<SwapWalletSession> guard(this);
        emit stateChanged();
        if (guard) emit operationCompleted(verified);
    });
    QPointer<SwapWalletSession> guard(this);
    const bool started=job->startWithOwner(m_helper,m_owner,expected);
    if (!guard) return false;
    if (!started) { m_state=RecoveryRequired; job->deleteLater(); }
    emit stateChanged();
    return started;
}
