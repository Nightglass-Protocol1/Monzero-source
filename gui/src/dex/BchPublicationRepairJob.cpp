#include "BchPublicationRepairJob.h"
#include "BchPublicationRepairProtocol.h"
#include "LitecoinJournalProtocol.h"
#include <QDateTime>
#include <QPointer>
#include <QProcessEnvironment>
using namespace BchAdmissionProtocol;
BchPublicationRepairJob::BchPublicationRepairJob(QObject *parent):QObject(parent) {
    m_deadline.setSingleShot(true);
    connect(&m_deadline,&QTimer::timeout,this,[this]{finish(false);});
    connect(&m_process,&QProcess::started,this,[this]{
        if (!m_busy || m_process.write(m_input)!=m_input.size()) {finish(false);return;}
        m_process.closeWriteChannel();m_input.clear();
    });
    connect(&m_process,&QProcess::readyReadStandardOutput,this,&BchPublicationRepairJob::drain);
    connect(&m_process,&QProcess::readyReadStandardError,this,&BchPublicationRepairJob::drain);
    connect(&m_process,&QProcess::errorOccurred,this,[this](QProcess::ProcessError){finish(false);});
    connect(&m_process,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[this](int code,QProcess::ExitStatus status){
        if (!m_busy) {QMetaObject::invokeMethod(this,"changed",Qt::QueuedConnection);return;}
        QPointer<BchPublicationRepairJob> guard(this);const auto request=m_request;
        drain();if (guard && request==m_request) finish(code==0 && status==QProcess::NormalExit);
    });
}
BchPublicationRepairJob::~BchPublicationRepairJob() {
    disconnect(&m_process,nullptr,this,nullptr);
    if (m_process.state()!=QProcess::NotRunning) {m_process.kill();m_process.waitForFinished(1000);}
}
bool BchPublicationRepairJob::configure(const QString &path) {
    if (!m_path.isEmpty() || m_busy) return false;
    const auto bytes=LitecoinJournalProtocol::privateConfig(path);QJsonObject wrapper,config,record,admission,rpc;
    if (!canonicalObject(bytes,&wrapper) || wrapper.size()!=5
        || wrapper.value("schema")!="monzero-private-bch-success-publication-repair-v1"
        || !QStringList{"node-wallet","local-wallet"}.contains(wrapper.value("fundingSource").toString())
        || !hex(wrapper.value("deploymentCommitment").toString(),64)) return false;
    const auto configPath=wrapper.value("admissionConfigFile").toString(),rpcPath=wrapper.value("rpcFile").toString();
    const auto configBytes=LitecoinJournalProtocol::privateConfig(configPath),rpcBytes=LitecoinJournalProtocol::privateConfig(rpcPath);
    if (!canonicalObject(configBytes,&config) || config.value("role")!="bch-funder" || !canonicalObject(rpcBytes,&rpc)) return false;
    const auto quotePath=config.value("quoteFile").toString();const auto quoteBytes=LitecoinJournalProtocol::privateConfig(quotePath);
    if (!canonicalObject(quoteBytes,&record) || !expectedReview(config,record,&admission)
        || !BackendExecutable::matches(config.value("helper").toString(),config.value("helperDigest").toString())) return false;
    const bool local=wrapper.value("fundingSource")=="local-wallet";
    if (rpc.value("schema")!=(local?"monzero-private-bch-observer-rpc-v1":"monzero-private-bch-funding-rpc-v1")) return false;
    m_path=path;m_program=config.value("helper").toString();m_digest=config.value("helperDigest").toString();
    m_sources={{path,bytes},{configPath,configBytes},{quotePath,quoteBytes},{rpcPath,rpcBytes}};
    m_config=config;m_record=record;m_source=wrapper;m_sourcesValid=true;m_state="ready";emit changed();return true;
}
bool BchPublicationRepairJob::sourcesMatch() const {
    if (!m_sourcesValid || !BackendExecutable::matches(m_program,m_digest)) return false;
    for (auto i=m_sources.cbegin();i!=m_sources.cend();++i)
        if (LitecoinJournalProtocol::privateConfig(i.key())!=i.value()) return false;
    return true;
}
void BchPublicationRepairJob::invalidate() {
    if (m_busy) {finish(false);return;}
    if (m_review.isEmpty() && m_approved.isEmpty() && (m_path.isEmpty() || m_state=="unknown")) return;
    m_review={};m_approved.clear();if (!m_path.isEmpty()) m_state="unknown";emit changed();
}
QVariantMap BchPublicationRepairJob::status() const {
    return {{"configured",!m_path.isEmpty()},{"busy",m_busy || m_process.state()!=QProcess::NotRunning},{"state",m_state},{"stage",m_stage},
        {"tradeId",m_config.value("tradeId").toString()},{"quoteCommitment",m_config.value("quoteCommitment").toString()},
        {"review",m_review.toVariantMap()},{"attempted",m_attempted},
        {"reviewAvailable",!m_path.isEmpty() && m_sourcesValid && !m_busy && m_process.state()==QProcess::NotRunning},
        {"submitAvailable",m_sourcesValid && !m_busy && m_process.state()==QProcess::NotRunning && m_state=="reviewed" && !m_review.isEmpty()},
        {"executable",false},{"chainObserved",false},{"releasesPrivateMaterial",false}};
}
bool BchPublicationRepairJob::review(const QString &stage) {
    if (m_busy || m_path.isEmpty() || m_process.state()!=QProcess::NotRunning || !BchPublicationRepairProtocol::stage(stage)) return false;
    m_review={};m_approved.clear();m_sourcesValid=sourcesMatch();
    if (!m_sourcesValid) {m_state="unknown";emit changed();return false;}
    m_stage=stage;return start(false);
}
bool BchPublicationRepairJob::submit(const QString &approved) {
    if (m_busy || m_path.isEmpty() || m_process.state()!=QProcess::NotRunning || m_state!="reviewed") return false;
    m_sourcesValid=sourcesMatch();
    if (!m_sourcesValid || !hex(approved,64) || m_review.value("commitment")!=approved
        || !BchPublicationRepairProtocol::review(m_review,m_config,m_record,m_source,m_stage,quint64(QDateTime::currentSecsSinceEpoch()))) {
        invalidate();return false;
    }
    m_approved=approved;return start(true);
}
bool BchPublicationRepairJob::start(bool submit) {
    m_submit=submit;m_request=QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString action=submit?"submit":"review";
    QJsonObject request{{"schema",QString("monzero-bch-success-publication-repair-%1-request-v1").arg(action)},
        {"requestId",m_request},{"stage",m_stage}};
    if (submit) request["commitment"]=m_approved;
    m_input=QJsonDocument(request).toJson(QJsonDocument::Compact);m_output.clear();m_errors=0;m_busy=true;
    if (submit) m_attempted=true;
    m_state=submit?"repairing":"reviewing";
    QProcessEnvironment environment;environment.insert("LANG","C");
    m_process.setProcessEnvironment(environment);m_process.setProcessChannelMode(QProcess::SeparateChannels);
    m_deadline.start(180000);
    m_process.start(m_program,{QString("--success-publication-repair-%1-stdio-v1").arg(action),m_path});
    QMetaObject::invokeMethod(this,"changed",Qt::QueuedConnection);return m_busy;
}
void BchPublicationRepairJob::drain() {
    if (!m_busy) return;
    m_process.setReadChannel(QProcess::StandardOutput);m_output+=m_process.read(16385-m_output.size());
    m_process.setReadChannel(QProcess::StandardError);m_errors+=m_process.read(1025-m_errors).size();
    if (m_output.size()>16384 || m_errors>1024) finish(false);
}
void BchPublicationRepairJob::finish(bool success) {
    if (!m_busy) return;
    m_busy=false;m_deadline.stop();if (m_process.state()!=QProcess::NotRunning) m_process.kill();
    QJsonObject value;m_sourcesValid=sourcesMatch();
    const bool valid=success && m_sourcesValid && m_errors==0 && canonicalObject(m_output,&value)
        && BchPublicationRepairProtocol::response(value,m_request,m_config,m_record,m_source,m_stage,
            quint64(QDateTime::currentSecsSinceEpoch()),m_submit,m_approved)
        && (!m_submit || value.value("review").toObject()==m_review);
    m_review={};m_state="unknown";
    if (valid) {m_review=value.value("review").toObject();m_state=m_submit?"repaired":"reviewed";}
    m_output.clear();m_input.clear();m_approved.clear();
    // Consumers may delete this job when notified. Do not let that destroy a
    // QProcess/QTimer while one of its own signal callbacks is still executing.
    QMetaObject::invokeMethod(this,"changed",Qt::QueuedConnection);
}
