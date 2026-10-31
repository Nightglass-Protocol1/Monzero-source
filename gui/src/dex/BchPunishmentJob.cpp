#include "BchPunishmentJob.h"
#include "BchPunishmentProtocol.h"
#include "LitecoinJournalProtocol.h"
#include <QDateTime>
#include <QPointer>
#include <QProcessEnvironment>
using namespace BchAdmissionProtocol;

BchPunishmentJob::BchPunishmentJob(QObject *parent):QObject(parent) {
    m_deadline.setSingleShot(true);
    connect(&m_deadline,&QTimer::timeout,this,[this]{finish(false);});
    connect(&m_process,&QProcess::started,this,[this]{
        if (!m_busy || m_process.write(m_input)!=m_input.size()) {finish(false);return;}
        m_process.closeWriteChannel();m_input.clear();
    });
    connect(&m_process,&QProcess::readyReadStandardOutput,this,&BchPunishmentJob::drain);
    connect(&m_process,&QProcess::readyReadStandardError,this,&BchPunishmentJob::drain);
    connect(&m_process,&QProcess::errorOccurred,this,[this](QProcess::ProcessError){finish(false);});
    connect(&m_process,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,
        [this](int code,QProcess::ExitStatus status){
            QPointer<BchPunishmentJob> guard(this);const auto request=m_request;
            drain();if (guard && request==m_request) {
                if (m_busy) finish(code==0 && status==QProcess::NormalExit);
                else notifyChanged();
            }
        });
}
BchPunishmentJob::~BchPunishmentJob() {
    disconnect(&m_process,nullptr,this,nullptr);
    if (m_process.state()!=QProcess::NotRunning) {m_process.kill();m_process.waitForFinished(1000);}
}
bool BchPunishmentJob::configure(const QString &path) {
    if (!m_path.isEmpty() || m_busy) return false;
    QJsonObject wrapper,config,record,admission;
    const auto bytes=LitecoinJournalProtocol::privateConfig(path);
    if (!canonicalObject(bytes,&wrapper)) return false;
    if (wrapper.size()!=3 || wrapper.value("schema")!="monzero-private-bch-punish-v1") return false;
    const auto deployment=wrapper.value("deploymentCommitment").toString();
    if (!hex(deployment,64) || deployment==QString(64,'0')) return false;
    const auto admissionPath=wrapper.value("admissionConfigFile").toString();
    const auto admissionBytes=LitecoinJournalProtocol::privateConfig(admissionPath);
    if (!canonicalObject(admissionBytes,&config) || config.value("role")!="native-funder") return false;
    const auto quotePath=config.value("quoteFile").toString();
    const auto quoteBytes=LitecoinJournalProtocol::privateConfig(quotePath);
    if (!canonicalObject(quoteBytes,&record) || !expectedReview(config,record,&admission)
        || !BackendExecutable::matches(config.value("helper").toString(),config.value("helperDigest").toString())) return false;
    m_path=path;m_wrapperBytes=bytes;m_admissionPath=admissionPath;m_admissionBytes=admissionBytes;
    m_quotePath=quotePath;m_quoteBytes=quoteBytes;m_deployment=deployment;
    m_program=config.value("helper").toString();m_digest=config.value("helperDigest").toString();
    m_trade=config.value("tradeId").toString();
    m_sourcesValid=true;m_state="ready";notifyChanged();return true;
}
bool BchPunishmentJob::configureObservation(const QString &path) {
    if (m_busy || m_path.isEmpty() || !m_observationPath.isEmpty() || !sourcesMatch()) return false;
    QJsonObject config,rpc;
    const auto bytes=LitecoinJournalProtocol::privateConfig(path);
    if (!canonicalObject(bytes,&config) || config.size()!=4
        || config.value("schema")!="monzero-private-bch-punish-observation-v1"
        || config.value("bchObserverKind")!="owned-node" || config.value("punishConfigFile")!=m_path) return false;
    const auto rpcPath=config.value("rpcFile").toString();
    const auto rpcBytes=LitecoinJournalProtocol::privateConfig(rpcPath);
    if (!canonicalObject(rpcBytes,&rpc) || rpc.size()!=6
        || rpc.value("schema")!="monzero-private-bch-observer-rpc-v1") return false;
    quint64 port=0,height=0;
    const auto cookie=rpc.value("cookieFile").toString(),checkpoint=rpc.value("checkpoint").toString();
    const auto version=rpc.value("subversion").toString();
    if (!integer(rpc.value("port"),65535,&port) || !port
        || !integer(rpc.value("checkpointHeight"),2147483647,&height) || !height
        || !hex(checkpoint,64) || checkpoint==QString(64,'0')
        || !QDir::isAbsolutePath(cookie) || QDir::cleanPath(cookie)!=cookie
        || !version.startsWith("/Bitcoin Cash Node:") || !version.endsWith('/') || version.size()>128) return false;
    for (const auto c:version) if (c.unicode()<32 || c.unicode()>126) return false;
    const QString genesis="0f9188f13cb7b2c71f2a335e3a4fc328bf5beb436012afca590b1a11466e2206";
    const auto terms=QJsonDocument::fromJson(m_quoteBytes).object().value("terms").toObject();
    const QJsonObject deployment{{"schema","monzero-bch-chain-observer-v1"},{"host","127.0.0.1"},
        {"port",rpc.value("port")},{"network","bch-regtest"},{"genesis",genesis},
        {"checkpointHeight",rpc.value("checkpointHeight")},{"checkpoint",checkpoint},{"subversion",version}};
    const auto digest=QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(deployment).toJson(QJsonDocument::Compact),
        QCryptographicHash::Sha256).toHex());
    if (terms.value("bchNetwork")!="bch-regtest" || terms.value("bchGenesis")!=genesis || digest!=m_deployment) return false;
    m_observationPath=path;m_observationBytes=bytes;m_rpcPath=rpcPath;m_rpcBytes=rpcBytes;
    m_observationValid=true;notifyChanged();return true;
}
bool BchPunishmentJob::configureSubmission(const QString &path) {
    if (m_busy || m_submitAttempted || !m_submissionPath.isEmpty() || !sourcesMatch() || !observationSourcesMatch()) return false;
    QJsonObject config,native;
    const auto bytes=LitecoinJournalProtocol::privateConfig(path);
    if (!canonicalObject(bytes,&config) || config.size()!=5
        || config.value("schema")!="monzero-private-bch-punish-submission-v1"
        || config.value("bchObserverKind")!="owned-node" || config.value("punishConfigFile")!=m_path
        || config.value("rpcFile")!=m_rpcPath) return false;
    const auto nativePath=config.value("nativeRpcFile").toString();
    const auto nativeBytes=LitecoinJournalProtocol::privateConfig(nativePath);
    if (!canonicalObject(nativeBytes,&native) || native.size()!=4
        || native.value("schema")!="monzero-private-bch-native-rpc-v1") return false;
    quint64 daemon=0,wallet=0;const auto login=native.value("loginFile").toString();
    if (!integer(native.value("daemonPort"),65535,&daemon) || !daemon
        || !integer(native.value("walletPort"),65535,&wallet) || !wallet || daemon==wallet
        || !QDir::isAbsolutePath(login) || QDir::cleanPath(login)!=login) return false;
    m_submissionPath=path;m_submissionBytes=bytes;m_nativeRpcPath=nativePath;m_nativeRpcBytes=nativeBytes;
    m_submissionValid=true;m_review={};m_result={};m_state="ready";notifyChanged();return true;
}
bool BchPunishmentJob::submissionSourcesMatch() const {
    return m_submissionValid && observationSourcesMatch()
        && LitecoinJournalProtocol::privateConfig(m_submissionPath)==m_submissionBytes
        && LitecoinJournalProtocol::privateConfig(m_nativeRpcPath)==m_nativeRpcBytes;
}
bool BchPunishmentJob::submit(const QString &commitment) {
    return status().value("submitAvailable").toBool() && commitment==m_review.value("commitment").toString() && run("submit");
}
bool BchPunishmentJob::observationSourcesMatch() const {
    return m_observationValid && LitecoinJournalProtocol::privateConfig(m_observationPath)==m_observationBytes
        && LitecoinJournalProtocol::privateConfig(m_rpcPath)==m_rpcBytes;
}
bool BchPunishmentJob::observe() {
    return status().value("observeAvailable").toBool() && run("observe");
}
bool BchPunishmentJob::sourcesMatch() const {
    return m_sourcesValid && BackendExecutable::matches(m_program,m_digest)
        && LitecoinJournalProtocol::privateConfig(m_path)==m_wrapperBytes
        && LitecoinJournalProtocol::privateConfig(m_admissionPath)==m_admissionBytes
        && LitecoinJournalProtocol::privateConfig(m_quotePath)==m_quoteBytes;
}
QVariantMap BchPunishmentJob::status() const {
    const bool available=!m_path.isEmpty() && m_sourcesValid && !m_busy && m_process.state()==QProcess::NotRunning;
    return {{"configured",!m_path.isEmpty()},{"busy",m_busy},{"state",m_state},{"tradeId",m_trade},
        {"review",m_review.toVariantMap()},{"result",m_result.toVariantMap()},
        {"savedReview",m_retainedReview.toVariantMap()},{"savedRelayState",m_retainedRelayState},
        {"reviewAvailable",available},{"checkAvailable",available},{"executable",false},
        {"chainObserved",!m_observation.isEmpty()},{"observation",m_observation.toVariantMap()},
        {"observeAvailable",available && m_observationValid},{"submissionConfigured",!m_submissionPath.isEmpty()},{"submitAttempted",m_submitAttempted},
        {"submitAvailable",available && m_submissionValid && m_observationValid && !m_submitAttempted
            && m_state=="reviewed" && m_retainedRelayState=="not-attempted" && !m_review.isEmpty()}};
}
bool BchPunishmentJob::review() {return run("review");}
bool BchPunishmentJob::refresh() {return run("status");}
bool BchPunishmentJob::run(const QString &operation) {
    if (m_busy || m_path.isEmpty() || m_process.state()!=QProcess::NotRunning) return false;
    if (!sourcesMatch()) {
        m_observation={};m_sourcesValid=false;m_review={};m_result={};m_state="unknown";notifyChanged();return false;
    }
    if (operation=="observe" && !observationSourcesMatch()) {
        m_observation={};m_observationValid=false;m_review={};m_result={};m_state="unknown";notifyChanged();return false;
    }
    if (operation=="submit" && !submissionSourcesMatch()) {
        m_observation={};m_submissionValid=false;m_review={};m_result={};m_state="unknown";notifyChanged();return false;
    }
    m_startedAt=QDateTime::currentSecsSinceEpoch();m_observation={};
    m_request=QUuid::createUuid().toString(QUuid::WithoutBraces);m_operation=operation;
    QJsonObject request{{"schema","monzero-bch-punish-"+operation+"-request-v1"},{"requestId",m_request}};
    m_approvedReview={};
    if (operation=="submit") {m_submitAttempted=true;m_approvedReview=m_review;request["commitment"]=m_review.value("commitment");}
    m_input=QJsonDocument(request).toJson(QJsonDocument::Compact);
    m_output.clear();m_errors=0;m_review={};m_result={};m_busy=true;
    m_state=operation=="submit"?"submitting":operation=="observe"?"observing":operation=="status"?"checking":"reviewing";
    QProcessEnvironment environment;environment.insert("LANG","C");
    m_process.setProcessEnvironment(environment);m_process.setProcessChannelMode(QProcess::SeparateChannels);
    m_deadline.start(180000);
    m_process.start(m_program,{"--punish-"+operation+"-stdio-v1",operation=="submit"?m_submissionPath:operation=="observe"?m_observationPath:m_path});
    notifyChanged();return true;
}
void BchPunishmentJob::notifyChanged() {
    QMetaObject::invokeMethod(this,"changed",Qt::QueuedConnection);
}
void BchPunishmentJob::drain() {
    if (!m_busy) return;
    m_process.setReadChannel(QProcess::StandardOutput);m_output+=m_process.read(16385-m_output.size());
    m_process.setReadChannel(QProcess::StandardError);m_errors+=m_process.read(1025-m_errors).size();
    if (m_output.size()>16384 || m_errors>1024) finish(false);
}
void BchPunishmentJob::finish(bool success) {
    if (!m_busy) return;
    m_busy=false;m_deadline.stop();
    if (m_process.state()!=QProcess::NotRunning) m_process.kill();
    QJsonObject response;
    m_sourcesValid=sourcesMatch();
    const auto config=QJsonDocument::fromJson(m_admissionBytes).object();
    const auto quote=QJsonDocument::fromJson(m_quoteBytes).object();
    const bool observationSame=m_operation!="observe" || observationSourcesMatch();
    if (!observationSame) m_observationValid=false;
    const bool submissionSame=m_operation!="submit" || submissionSourcesMatch();
    if (!submissionSame) m_submissionValid=false;
    bool valid=submissionSame && observationSame && success && m_sourcesValid && m_errors==0 && canonicalObject(m_output,&response)
        && response.size()==8 && response.value("schema")=="monzero-bch-punish-"+m_operation+"-response-v1"
        && response.value("requestId")==m_request && response.value("tradeId")==m_trade
        && response.value("executable").isBool() && !response.value("executable").toBool()
        && response.value("chainObserved").isBool() && response.value("chainObserved").toBool()==(m_operation=="observe")
        && response.value("review").isObject()
        && BchPunishmentProtocol::review(response.value("review").toObject(),config,quote,m_deployment,
            quint64(QDateTime::currentSecsSinceEpoch()))
        && response.value("relay").isObject()
        && BchPunishmentProtocol::relay(response.value("relay").toObject(),response.value("review").toObject())
        && (m_operation=="observe"
            ? response.value("observation").isObject() && BchPunishmentProtocol::observation(response.value("observation").toObject(),
                response.value("review").toObject(),quint64(m_startedAt),quint64(QDateTime::currentSecsSinceEpoch()))
            : response.value("observation").isNull());
    const auto reviewed=response.value("review").toObject();
    const auto relayState=response.value("relay").toObject().value("state").toString();
    if (valid && !m_retainedReview.isEmpty()) {
        valid=reviewed==m_retainedReview
            && !(m_retainedRelayState=="acknowledged" && relayState!="acknowledged")
            && !(m_retainedRelayState=="attempted" && relayState=="not-attempted");
    }
    if (valid && m_operation=="submit") valid=relayState=="acknowledged" && reviewed==m_approvedReview;
    if (valid && m_operation=="observe") valid=relayState!="not-attempted";
    if (!valid) {m_observation={};m_review={};m_result={};m_state="unknown";}
    else {
        m_retainedReview=reviewed;m_retainedRelayState=relayState;
        m_review=reviewed;m_result=response;
        m_state=response.value("relay").toObject().value("state").toString();
        if (m_operation=="observe") m_observation=response.value("observation").toObject();
        if (m_state=="not-attempted") m_state=m_operation=="review"?"reviewed":"checked";
    }
    m_output.clear();m_input.clear();m_approvedReview={};notifyChanged();
}
