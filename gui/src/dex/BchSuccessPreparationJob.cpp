#include "BchSuccessPreparationJob.h"
#include "BchAdmissionProtocol.h"
#include "BchSuccessReleaseProtocol.h"
#include <algorithm>
#include "LitecoinJournalProtocol.h"
#include <QDateTime>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <QProcessEnvironment>
#include <QUuid>
#include <cmath>

using namespace BchAdmissionProtocol;

BchSuccessPreparationJob::BchSuccessPreparationJob(QObject *parent):QObject(parent) {
    m_deadline.setSingleShot(true);
    connect(&m_deadline,&QTimer::timeout,this,[this]{finish(false);});
    connect(&m_process,&QProcess::started,this,[this]{
        if (!m_busy || m_process.write(m_input)!=m_input.size()) {finish(false);return;}
        m_process.closeWriteChannel();m_input.clear();
    });
    connect(&m_process,&QProcess::readyReadStandardOutput,this,&BchSuccessPreparationJob::drain);
    connect(&m_process,&QProcess::readyReadStandardError,this,&BchSuccessPreparationJob::drain);
    connect(&m_process,&QProcess::errorOccurred,this,[this](QProcess::ProcessError){finish(false);});
    connect(&m_process,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,
        [this](int code,QProcess::ExitStatus status){
            QPointer<BchSuccessPreparationJob> guard(this);const auto request=m_request;
            drain();if (guard && request==m_request) finish(code==0 && status==QProcess::NormalExit);
        });
}
BchSuccessPreparationJob::~BchSuccessPreparationJob() {
    disconnect(&m_process,nullptr,this,nullptr);
    if (m_process.state()!=QProcess::NotRunning) {m_process.kill();m_process.waitForFinished(1000);}
}
bool BchSuccessPreparationJob::configure(const QString &path) {
    if (!m_path.isEmpty() || m_busy) return false;
    const auto bytes=LitecoinJournalProtocol::privateConfig(path);QJsonObject wrapper,config,record,admission;
    if (!canonicalObject(bytes,&wrapper) || wrapper.size()!=6
        || wrapper.value("schema")!="monzero-private-bch-success-preparation-v1") return false;
    const auto admissionPath=wrapper.value("admissionConfigFile").toString();
    const auto configBytes=LitecoinJournalProtocol::privateConfig(admissionPath);
    if (!canonicalObject(configBytes,&config) || config.value("role")!="bch-funder") return false;
    const auto quotePath=config.value("quoteFile").toString();
    const auto quoteBytes=LitecoinJournalProtocol::privateConfig(quotePath);
    if (!canonicalObject(quoteBytes,&record) || !expectedReview(config,record,&admission)
        || !BackendExecutable::matches(config.value("helper").toString(),config.value("helperDigest").toString())) return false;
    quint64 margin=0;
    if (!BchCancellationProtocol::hex(wrapper.value("deploymentCommitment").toString(),64)
        || !BchCancellationProtocol::integer(wrapper.value("safetyBlocks"),4294967295ULL,&margin) || !margin) return false;
    m_source=wrapper;
    m_sources={{path,bytes},{admissionPath,configBytes},{quotePath,quoteBytes}};
    m_rpcSources.clear();
    for (const auto key:{"bchRpcFile","nativeRpcFile"}) {
        const auto rpcPath=wrapper.value(key).toString();
        if (!QFileInfo(rpcPath).isAbsolute()) return false;
        const auto rpcBytes=LitecoinJournalProtocol::privateConfig(rpcPath);QJsonObject rpc;
        if (canonicalObject(rpcBytes,&rpc)) m_rpcSources.insert(rpcPath,rpcBytes);
    }
    m_path=path;m_program=config.value("helper").toString();m_digest=config.value("helperDigest").toString();
    m_trade=config.value("tradeId").toString();m_config=config;m_record=record;
    m_sourcesValid=true;m_state="ready";emit changed();return true;
}
bool BchSuccessPreparationJob::sourcesMatch() const {
    if (!m_sourcesValid || !BackendExecutable::matches(m_program,m_digest)) return false;
    for (auto i=m_sources.cbegin();i!=m_sources.cend();++i)
        if (LitecoinJournalProtocol::privateConfig(i.key())!=i.value()) return false;
    return true;
}
bool BchSuccessPreparationJob::rpcSourcesMatch() const {
    if (m_rpcSources.size()!=2) return false;
    for (auto i=m_rpcSources.cbegin();i!=m_rpcSources.cend();++i)
        if (LitecoinJournalProtocol::privateConfig(i.key())!=i.value()) return false;
    return true;
}
void BchSuccessPreparationJob::invalidate() {
    if (m_busy) {finish(false);return;}
    if (m_review.isEmpty()) return;
    m_review={};m_proofReview={};m_context={};m_preparation={};m_statusKnown=false;m_chainObserved=false;m_state="unknown";emit changed();
}
QVariantMap BchSuccessPreparationJob::status() const {
    return {{"configured",!m_path.isEmpty()},{"busy",m_busy},{"state",m_state},{"tradeId",m_trade},{"quoteCommitment",m_config.value("quoteCommitment").toString()},
        {"review",m_review.toVariantMap()},{"preparation",m_preparation.toVariantMap()},{"statusKnown",m_statusKnown},
        {"checkAvailable",!m_path.isEmpty() && m_sourcesValid && !m_busy},
        {"submitAvailable",!m_path.isEmpty() && m_sourcesValid && !m_busy && m_rpcSources.size()==2},{"attempted",m_attempted},
        {"executable",false},{"chainObserved",m_chainObserved}};
}
bool BchSuccessPreparationJob::submit(const QJsonObject &releaseReview,const QJsonObject &proofReview,
    const QJsonObject &fundingContext,const QString &approved) {
    if (m_busy || m_path.isEmpty() || m_process.state()!=QProcess::NotRunning) return false;
    m_review={};m_proofReview={};m_context={};m_preparation={};m_statusKnown=false;m_chainObserved=false;
    const bool same=sourcesMatch();m_sourcesValid=same;
    if (!same || !rpcSourcesMatch() || !BchSuccessReleaseProtocol::review(releaseReview,m_config,m_record,m_source,proofReview,fundingContext)
        || !BchCancellationProtocol::hex(approved,64) || releaseReview.value("commitment")!=approved) {
        m_state="unknown";emit changed();return false;
    }
    m_approved=approved;
    m_context=fundingContext;m_proofReview=proofReview;return start(false);
}
bool BchSuccessPreparationJob::refresh(const QJsonObject &proofReview,const QJsonObject &fundingContext) {
    if (m_busy || m_path.isEmpty() || m_process.state()!=QProcess::NotRunning) return false;
    m_review={};m_proofReview={};m_context={};m_preparation={};m_statusKnown=false;m_chainObserved=false;m_approved.clear();
    const bool same=sourcesMatch();m_sourcesValid=same;
    if (!same || !BchNativeProofProtocol::fundingContext(fundingContext)
        || !BchNativeProofProtocol::review(proofReview,m_config,m_record,
            fundingContext.value("bchFundingOutpoint").toString(),fundingContext.value("destination").toString(),
            fundingContext.value("admission").toString(),fundingContext.value("agreement").toString())) {
        m_state="unknown";emit changed();return false;
    }
    m_context=fundingContext;m_proofReview=proofReview;return start(true);
}
bool BchSuccessPreparationJob::start(bool statusOnly) {
    m_statusRequest=statusOnly;m_request=QUuid::createUuid().toString(QUuid::WithoutBraces);
    QJsonObject request{{"schema",statusOnly ? "monzero-bch-success-preparation-status-request-v1" : "monzero-bch-success-preparation-request-v1"},
        {"requestId",m_request}};
    if (!statusOnly) request["commitment"]=m_approved;
    m_input=QJsonDocument(request).toJson(QJsonDocument::Compact);
    m_output.clear();m_errors=0;m_busy=true;
    if (!statusOnly) m_attempted=true;
    m_state=statusOnly ? "checking" : "preparing";
    QProcessEnvironment environment;environment.insert("LANG","C");
    m_process.setProcessEnvironment(environment);m_process.setProcessChannelMode(QProcess::SeparateChannels);
    m_deadline.start(180000);
    m_process.start(m_program,{statusOnly ? "--success-preparation-status-stdio-v1" : "--success-preparation-stdio-v1",m_path});
    emit changed();return true;
}

void BchSuccessPreparationJob::drain() {
    if (!m_busy) return;
    m_process.setReadChannel(QProcess::StandardOutput);m_output+=m_process.read(16385-m_output.size());
    m_process.setReadChannel(QProcess::StandardError);m_errors+=m_process.read(1025-m_errors).size();
    if (m_output.size()>16384 || m_errors>1024) finish(false);
}
void BchSuccessPreparationJob::finish(bool success) {
    if (!m_busy) return;
    m_busy=false;m_deadline.stop();
    if (m_process.state()!=QProcess::NotRunning) m_process.kill();
    QJsonObject response;const bool same=sourcesMatch();m_sourcesValid=same;
    const bool valid=success && same && m_errors==0 && canonicalObject(m_output,&response)
        && (m_statusRequest ? BchSuccessReleaseProtocol::preparationStatusResponse(response,m_request,m_config,m_record,m_source,m_proofReview,m_context,quint64(QDateTime::currentSecsSinceEpoch()))
            : rpcSourcesMatch() && BchSuccessReleaseProtocol::preparationResponse(response,m_request,m_approved,m_config,m_record,m_source,m_proofReview,m_context));
    m_review={};m_preparation={};m_state="unknown";m_statusKnown=false;m_chainObserved=false;
    if (valid) {
        m_review=response.value("review").toObject();m_chainObserved=!m_statusRequest;
        if (m_statusRequest) {
            m_statusKnown=true;m_preparation=response.value("preparation").toObject();
            m_state=m_preparation.isEmpty() ? "checked" : "saved";
        } else m_state="prepared";
    }
    m_output.clear();m_input.clear();m_approved.clear();m_context={};m_proofReview={};emit changed();
}
