#include "BchRefundWalletJob.h"
#include "BchRefundWalletProtocol.h"
#include "LitecoinJournalProtocol.h"
#include <QPointer>
#include <QTemporaryFile>
#include <QDir>
#include <QDateTime>
#include <QProcessEnvironment>
#include <sodium.h>
using namespace BchAdmissionProtocol;
namespace {
void clearSecret(QByteArray &bytes) {if (!bytes.isEmpty()) sodium_memzero(bytes.data(),bytes.size());bytes.clear();}
struct SecretInput {QByteArray &bytes;~SecretInput(){clearSecret(bytes);}};
struct SecretText {QString text;~SecretText(){if (!text.isEmpty()) sodium_memzero(text.data(),text.size()*sizeof(QChar));}};
bool consistentHistory(const QJsonObject &before,const QJsonObject &after) {
    if (before.isEmpty()) return true;
    auto old=before,next=after;old.remove("state");old.remove("walletIntent");next.remove("state");next.remove("walletIntent");
    const auto prior=before.value("state").toString(),current=after.value("state").toString();
    return old==next && !(prior=="ready" && current!="ready") && !(prior=="attempted" && current=="not-attempted")
        && (before.value("walletIntent").isNull() || before.value("walletIntent")==after.value("walletIntent"));
}
}

BchRefundWalletJob::BchRefundWalletJob(QObject *parent):QObject(parent) {
    m_deadline.setSingleShot(true);m_reviewExpiry.setSingleShot(true);
    connect(&m_reviewExpiry,&QTimer::timeout,this,[this]{
        m_review={};m_result={};if (!m_busy && m_state=="reviewed") m_state="review-expired";notifyChanged();
    });
    connect(&m_deadline,&QTimer::timeout,this,[this]{finish(false);});
    connect(&m_process,&QProcess::started,this,[this]{
        if (!m_busy || m_process.write(m_input)!=m_input.size()) {finish(false);return;}
        m_process.closeWriteChannel();clearSecret(m_input);
    });
    connect(&m_process,&QProcess::readyReadStandardOutput,this,&BchRefundWalletJob::drain);
    connect(&m_process,&QProcess::readyReadStandardError,this,&BchRefundWalletJob::drain);
    connect(&m_process,&QProcess::errorOccurred,this,[this](QProcess::ProcessError){finish(false);});
    connect(&m_process,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[this](int code,QProcess::ExitStatus status){
        QPointer<BchRefundWalletJob> guard(this);const auto request=m_request;
        drain();if (guard && request==m_request) {
            if (m_busy) finish(code==0 && status==QProcess::NormalExit);else notifyChanged();
        }
    });
}
BchRefundWalletJob::~BchRefundWalletJob() {
    disconnect(&m_process,nullptr,this,nullptr);
    disconnect(m_ownerStopped);disconnect(m_ownerDestroyed);
    if (m_process.state()!=QProcess::NotRunning) {m_process.kill();m_process.waitForFinished(1000);}
    if (m_submitting && m_submissionOwner) m_submissionOwner->completeWalletOperation(m_operationToken,false);
    clearSecret(m_input);clearSecret(m_output);
}
bool BchRefundWalletJob::configure(const QString &path) {
    if (!m_path.isEmpty() || m_busy) return false;
    QJsonObject wrapper,config,record,admission;
    const auto bytes=LitecoinJournalProtocol::privateConfig(path);
    if (!canonicalObject(bytes,&wrapper) || wrapper.size()!=4
        || wrapper.value("schema")!="monzero-private-bch-native-refund-wallet-v1") return false;
    const auto deployment=wrapper.value("deploymentCommitment").toString(),refund=wrapper.value("refundTxid").toString();
    if (!hex(deployment,64) || deployment==QString(64,'0') || !BchCancellationProtocol::txid(refund)) return false;
    const auto admissionPath=wrapper.value("admissionConfigFile").toString();
    const auto admissionBytes=LitecoinJournalProtocol::privateConfig(admissionPath);
    if (!canonicalObject(admissionBytes,&config) || config.value("role")!="native-funder") return false;
    const auto quotePath=config.value("quoteFile").toString();const auto quoteBytes=LitecoinJournalProtocol::privateConfig(quotePath);
    if (!canonicalObject(quoteBytes,&record) || !expectedReview(config,record,&admission)
        || !BackendExecutable::matches(config.value("helper").toString(),config.value("helperDigest").toString())) return false;
    m_path=path;m_wrapperBytes=bytes;m_admissionPath=admissionPath;m_admissionBytes=admissionBytes;
    m_quotePath=quotePath;m_quoteBytes=quoteBytes;m_program=config.value("helper").toString();m_digest=config.value("helperDigest").toString();
    m_trade=config.value("tradeId").toString();m_deployment=deployment;m_refund=refund;m_sourcesValid=true;m_state="configured";
    notifyChanged();return true;
}
bool BchRefundWalletJob::configureReview(const QString &path) {
    if (m_path.isEmpty() || !m_reviewPath.isEmpty() || m_busy || m_process.state()!=QProcess::NotRunning) return false;
    QJsonObject wrapper,native,bch,observer;
    const auto bytes=LitecoinJournalProtocol::privateConfig(path);
    if (!canonicalObject(bytes,&wrapper) || wrapper.size()!=5
        || wrapper.value("schema")!="monzero-private-bch-native-refund-wallet-review-v1"
        || wrapper.value("statusConfigFile")!=m_path) return false;
    const auto bchPath=wrapper.value("bchRpcFile").toString(),nativePath=wrapper.value("nativeRpcFile").toString();
    const auto bchBytes=LitecoinJournalProtocol::privateConfig(bchPath),nativeBytes=LitecoinJournalProtocol::privateConfig(nativePath);
    const auto observerPath=wrapper.value("observerRpcFile").toString();
    const auto observerBytes=LitecoinJournalProtocol::privateConfig(observerPath);
    quint64 daemon=0,wallet=0,port=0,height=0,observerDaemon=0,observerWallet=0;
    // Backend parsing below owns full deployment validation; reject malformed
    // connection descriptors before enabling a desktop request.

    if (!canonicalObject(bchBytes,&bch) || bch.value("schema")!="monzero-private-bch-observer-rpc-v1"
        || !canonicalObject(nativeBytes,&native) || native.size()!=4 || native.value("schema")!="monzero-private-bch-native-rpc-v1"
        || !integer(native.value("daemonPort"),65535,&daemon) || !daemon
        || !integer(native.value("walletPort"),65535,&wallet) || !wallet || wallet==daemon
        || !QFileInfo(native.value("loginFile").toString()).isAbsolute() || !sourcesMatch()) return false;
    if (observerPath==nativePath || !canonicalObject(observerBytes,&observer) || observer.size()!=4
        || observer.value("schema")!="monzero-private-bch-native-rpc-v1"
        || !integer(observer.value("daemonPort"),65535,&observerDaemon) || observerDaemon!=daemon
        || !integer(observer.value("walletPort"),65535,&observerWallet) || !observerWallet
        || observerWallet==daemon || observerWallet==wallet
        || !QFileInfo(observer.value("loginFile").toString()).isAbsolute()) return false;
    const auto checkpoint=bch.value("checkpoint").toString(),version=bch.value("subversion").toString();
    if (bch.size()!=6 || !integer(bch.value("port"),65535,&port) || !port
        || !integer(bch.value("checkpointHeight"),2147483647,&height) || !height
        || !hex(checkpoint,64) || checkpoint==QString(64,'0')
        || !QFileInfo(bch.value("cookieFile").toString()).isAbsolute()
        || !version.startsWith("/Bitcoin Cash Node:") || !version.endsWith('/') || version.size()>128) return false;
    for (const auto ch:version) if (ch.unicode()<32 || ch.unicode()>126) return false;
    m_reviewPath=path;m_reviewBytes=bytes;m_bchPath=bchPath;m_bchBytes=bchBytes;m_nativePath=nativePath;m_nativeBytes=nativeBytes;
    m_observerPath=observerPath;m_observerBytes=observerBytes;
    m_backend={{"schema","monzero-local-wallet-endpoint-v1"},{"host","127.0.0.1"},{"daemonPort",double(daemon)},{"walletPort",double(wallet)}};
    notifyChanged();return true;
}
bool BchRefundWalletJob::provisionOwnedReview(SwapWalletRpcProcess *owner,const QString &bchRpcFile,const QString &observerRpcFile) {
    if (!owner || !owner->walletOperationAvailable() || m_path.isEmpty() || !m_reviewPath.isEmpty()
        || m_busy || m_process.state()!=QProcess::NotRunning || !sourcesMatch()) return false;
    const auto native=owner->provisionBchNativeSession();
    if (native.isEmpty() || !owner->ownsBchNativeSession(native)) return false;
    const auto bytes=QJsonDocument(QJsonObject{{"schema","monzero-private-bch-native-refund-wallet-review-v1"},
        {"statusConfigFile",m_path},{"bchRpcFile",bchRpcFile},{"observerRpcFile",observerRpcFile},
        {"nativeRpcFile",native}}).toJson(QJsonDocument::Compact);
    auto wrapper=std::make_unique<QTemporaryFile>(QDir(owner->dataDirectory()).filePath("bch-refund-wallet-review-XXXXXX"));
    if (!wrapper->open() || !wrapper->setPermissions(QFile::ReadOwner | QFile::WriteOwner)
        || wrapper->write(bytes)!=bytes.size() || !wrapper->flush()) return false;
    const auto path=wrapper->fileName();wrapper->close();
    if (!configureReview(path)) return false;
    m_provisionedReview=std::move(wrapper);
    if (m_reviewBytes!=bytes || !owner->ownsBchNativeSession(m_nativePath)) {
        discardProvisionedReview();return false;
    }
    // Session-local config, removed with this job. It contains paths, not credentials.
    return true;
}
void BchRefundWalletJob::discardProvisionedReview() {
    if (!m_provisionedReview || m_busy || !m_review.isEmpty()) return;
    m_reviewPath.clear();m_reviewBytes.clear();m_bchPath.clear();m_bchBytes.clear();
    m_nativePath.clear();m_nativeBytes.clear();m_observerPath.clear();m_observerBytes.clear();m_backend={};
    m_provisionedReview.reset();notifyChanged();
}
bool BchRefundWalletJob::requestReview(const QString &action) {
    const auto state=m_savedWallet.value("state").toString();
    if (m_savedWallet.isEmpty() || (action=="restore" && state!="not-attempted")
        || ((action=="open" || action=="check") && state!="attempted" && state!="ready")) return false;
    return !m_reviewPath.isEmpty() && (action=="restore" || action=="open" || action=="check") && start(action);
}
bool BchRefundWalletJob::ownsConfiguredService(SwapWalletRpcProcess *owner) const {
    return owner && !m_reviewPath.isEmpty() && owner->ownsBchNativeSession(m_nativePath);
}
bool BchRefundWalletJob::canSubmit(SwapWalletRpcProcess *owner) const {
    if (m_busy || !m_sourcesValid || m_submissionUncertain || m_process.state()!=QProcess::NotRunning
        || m_state!="reviewed" || m_review.isEmpty() || !m_reviewExpiry.isActive() || m_reviewExpiry.remainingTime()<=0
        || !ownsConfiguredService(owner) || !owner->walletOperationAvailable()) return false;
    const auto now=quint64(QDateTime::currentSecsSinceEpoch());
    const auto observed=quint64(m_result.value("observedAt").toDouble());
    return now>=observed && now-observed<30;
}
bool BchRefundWalletJob::submit(SwapWalletRpcProcess *owner,const QString &commitment,QByteArray &&password) {
    SecretInput wipe{password};
    if (!canSubmit(owner) || commitment!=m_review.value("commitment").toString() || !sourcesMatch()) return false;
    const auto action=m_review.value("details").toObject().value("action").toString();
    if (!BchRefundWalletProtocol::review(m_review,QJsonDocument::fromJson(m_admissionBytes).object(),
        QJsonDocument::fromJson(m_quoteBytes).object(),m_refund,m_deployment,action,m_backend)
        || password.size()>1024 || (action=="check" ? !password.isEmpty() : password.isEmpty())) return false;
    SecretText secret{QString::fromUtf8(password)};
    auto normalized=secret.text.toUtf8();SecretInput wipeNormalized{normalized};
    if (normalized!=password) return false;
    for (const auto ch:secret.text) if (ch.category()==QChar::Other_Control) return false;
    const auto requestId=QUuid::createUuid().toString(QUuid::WithoutBraces);
    auto input=QJsonDocument(QJsonObject{{"schema","monzero-bch-native-refund-wallet-submit-request-v1"},
        {"requestId",requestId},{"action",action},{"commitment",commitment}}).toJson(QJsonDocument::Compact);
    SecretInput wipeInput{input};
    // Serialize the validated UTF-8 password directly into the bounded buffer;
    // avoid making a password-bearing QJsonObject/QString copy. Controls were
    // rejected above; only quote and backslash need escaping here.
    input.reserve(4096);input.chop(1);input.append(",\"password\":\"");
    for (const char byte:password) {if (byte=='"' || byte=='\\') input.append('\\');input.append(byte);}
    input.append("\"}");
    if (input.size()>4096) return false;
    const auto token=owner->beginWalletOperation();if (token.isEmpty()) return false;
    m_submissionOwner=owner;m_operationToken=token;m_submitting=true;
    m_approved=m_review;m_action=action;m_review={};m_reviewExpiry.stop();m_result={};m_wallet={};
    m_request=requestId;m_input.swap(input);
    m_output.clear();m_errors=0;m_busy=true;m_state="submitting";
    m_ownerStopped=connect(owner,&SwapWalletRpcProcess::stopped,this,[this]{if (m_submitting) finish(false);});
    m_ownerDestroyed=connect(owner,&QObject::destroyed,this,[this]{if (m_submitting) finish(false);});
    QProcessEnvironment environment;environment.insert("LANG","C");m_process.setProcessEnvironment(environment);
    m_process.setProcessChannelMode(QProcess::SeparateChannels);m_deadline.start(180000);
    m_process.start(m_program,{"--native-refund-wallet-submit-stdio-v1",m_reviewPath});notifyChanged();return m_busy;
}
bool BchRefundWalletJob::sourcesMatch() const {
    return m_sourcesValid && BackendExecutable::matches(m_program,m_digest)
        && LitecoinJournalProtocol::privateConfig(m_path)==m_wrapperBytes
        && LitecoinJournalProtocol::privateConfig(m_admissionPath)==m_admissionBytes
        && LitecoinJournalProtocol::privateConfig(m_quotePath)==m_quoteBytes
        && (m_reviewPath.isEmpty() || (LitecoinJournalProtocol::privateConfig(m_reviewPath)==m_reviewBytes
            && LitecoinJournalProtocol::privateConfig(m_bchPath)==m_bchBytes && LitecoinJournalProtocol::privateConfig(m_nativePath)==m_nativeBytes
            && LitecoinJournalProtocol::privateConfig(m_observerPath)==m_observerBytes));
}
QVariantMap BchRefundWalletJob::status() const {
    return {{"configured",!m_path.isEmpty()},{"submissionUncertain",m_submissionUncertain},{"busy",m_busy},{"state",m_state},{"tradeId",m_trade},
        {"wallet",m_wallet.toVariantMap()},{"savedWallet",m_savedWallet.toVariantMap()},{"result",m_result.toVariantMap()},
        {"checkAvailable",!m_path.isEmpty() && m_sourcesValid && !m_busy && m_process.state()==QProcess::NotRunning},
        {"reviewConfigured",!m_reviewPath.isEmpty()},{"review",m_review.toVariantMap()},
        {"reviewAvailable",!m_reviewPath.isEmpty() && !m_savedWallet.isEmpty() && m_sourcesValid && !m_busy && m_process.state()==QProcess::NotRunning},
        {"chainObserved",!m_review.isEmpty() && m_reviewExpiry.isActive()},{"executable",false}};
}
bool BchRefundWalletJob::refresh() {return start(QString{});}
bool BchRefundWalletJob::start(const QString &action) {
    if (m_busy || m_path.isEmpty() || m_process.state()!=QProcess::NotRunning) return false;
    m_wallet={};m_result={};m_review={};m_reviewExpiry.stop();
    if (!sourcesMatch()) {m_sourcesValid=false;m_state="unknown";notifyChanged();return false;}
    m_request=QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_action=action;m_startedAt=quint64(QDateTime::currentSecsSinceEpoch());
    QJsonObject request{{"schema",action.isEmpty()?"monzero-bch-native-refund-wallet-status-request-v1":"monzero-bch-native-refund-wallet-review-request-v1"},{"requestId",m_request}};
    if (!action.isEmpty()) request["action"]=action;
    m_input=QJsonDocument(request).toJson(QJsonDocument::Compact);
    m_output.clear();m_errors=0;m_busy=true;m_state=action.isEmpty()?"checking":"reviewing";
    QProcessEnvironment environment;environment.insert("LANG","C");m_process.setProcessEnvironment(environment);
    m_process.setProcessChannelMode(QProcess::SeparateChannels);m_deadline.start(180000);
    m_process.start(m_program,{action.isEmpty()?"--native-refund-wallet-status-stdio-v1":"--native-refund-wallet-review-stdio-v1",action.isEmpty()?m_path:m_reviewPath});notifyChanged();return true;
}
void BchRefundWalletJob::notifyChanged() {QMetaObject::invokeMethod(this,"changed",Qt::QueuedConnection);}
void BchRefundWalletJob::drain() {
    if (!m_busy) return;
    m_process.setReadChannel(QProcess::StandardOutput);m_output+=m_process.read(16385-m_output.size());
    m_process.setReadChannel(QProcess::StandardError);m_errors+=m_process.read(1025-m_errors).size();
    if (m_output.size()>16384 || m_errors>1024) finish(false);
}
void BchRefundWalletJob::finish(bool success) {
    if (!m_busy) return;
    m_busy=false;m_deadline.stop();if (m_process.state()!=QProcess::NotRunning) m_process.kill();
    m_sourcesValid=sourcesMatch();QJsonObject response;
    if (m_submitting) {
        bool valid=success && m_sourcesValid && m_errors==0 && m_submissionOwner
            && m_submissionOwner->ownsBchNativeSession(m_nativePath) && canonicalObject(m_output,&response)
            && BchRefundWalletProtocol::submissionResponse(response,m_request,m_approved,
                QJsonDocument::fromJson(m_admissionBytes).object(),QJsonDocument::fromJson(m_quoteBytes).object(),
                m_refund,m_deployment,m_action,m_backend)
            && consistentHistory(m_savedWallet,response.value("wallet").toObject());
        disconnect(m_ownerStopped);disconnect(m_ownerDestroyed);
        if (m_submissionOwner) valid=m_submissionOwner->completeWalletOperation(m_operationToken,valid) && valid;
        else valid=false;
        m_submissionUncertain=!valid;m_submitting=false;m_operationToken.clear();m_submissionOwner.clear();m_approved={};
        if (valid) {m_wallet=response.value("wallet").toObject();m_savedWallet=m_wallet;m_result=response;m_state="ready";}
        else {m_wallet={};m_result={};m_state="recovery-required";}
        clearSecret(m_input);clearSecret(m_output);notifyChanged();return;
    }
    if (!m_action.isEmpty()) {
        const auto now=quint64(QDateTime::currentSecsSinceEpoch());
        bool valid=success && m_sourcesValid && m_errors==0 && canonicalObject(m_output,&response)
            && BchRefundWalletProtocol::reviewResponse(response,m_request,m_startedAt,now,
                QJsonDocument::fromJson(m_admissionBytes).object(),QJsonDocument::fromJson(m_quoteBytes).object(),
                m_refund,m_deployment,m_action,m_backend);
        if (valid) {
            auto historical=response.value("review").toObject().value("details").toObject();
            for (const auto key:{"action","restoreHeight","minimumBchConfirmations","minimumXmzConfirmations","walletBackend","refund"}) historical.remove(key);
            historical["schema"]="monzero-bch-native-refund-wallet-status-v1";
            historical["refundHint"]=response.value("review").toObject().value("details").toObject().value("refund");
            historical["chainObserved"]=false;historical["executable"]=false;
            valid=!m_savedWallet.isEmpty() && consistentHistory(m_savedWallet,historical);
        }
        if (valid) {
            m_review=response.value("review").toObject();m_result=response;m_state="reviewed";
            const auto observed=quint64(response.value("observedAt").toDouble());
            m_reviewExpiry.start(int((30-(now-observed))*1000));
        } else {m_review={};m_result={};m_state="unknown";}
        m_input.clear();m_output.clear();notifyChanged();return;
    }
    bool valid=success && m_sourcesValid && m_errors==0 && canonicalObject(m_output,&response)
        && response.size()==6 && response.value("schema")=="monzero-bch-native-refund-wallet-status-response-v1"
        && response.value("requestId")==m_request && response.value("tradeId")==m_trade
        && response.value("executable")==QJsonValue(false) && response.value("chainObserved")==QJsonValue(false)
        && response.value("wallet").isObject()
        && BchRefundWalletProtocol::status(response.value("wallet").toObject(),QJsonDocument::fromJson(m_admissionBytes).object(),
            QJsonDocument::fromJson(m_quoteBytes).object(),m_refund,m_deployment);
    const auto wallet=response.value("wallet").toObject();
    if (valid) valid=consistentHistory(m_savedWallet,wallet);
    if (!valid) {m_wallet={};m_result={};m_state="unknown";}
    else {m_wallet=wallet;m_savedWallet=wallet;m_result=response;m_state=wallet.value("state").toString();}
    m_input.clear();m_output.clear();notifyChanged();
}
