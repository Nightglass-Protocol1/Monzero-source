#include "BchSuccessReleaseReviewJob.h"
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

BchSuccessReleaseReviewJob::BchSuccessReleaseReviewJob(QObject *parent):QObject(parent) {
    m_deadline.setSingleShot(true);
    connect(&m_deadline,&QTimer::timeout,this,[this]{finish(false);});
    connect(&m_process,&QProcess::started,this,[this]{
        if (!m_busy || m_process.write(m_input)!=m_input.size()) {finish(false);return;}
        m_process.closeWriteChannel();m_input.clear();
    });
    connect(&m_process,&QProcess::readyReadStandardOutput,this,&BchSuccessReleaseReviewJob::drain);
    connect(&m_process,&QProcess::readyReadStandardError,this,&BchSuccessReleaseReviewJob::drain);
    connect(&m_process,&QProcess::errorOccurred,this,[this](QProcess::ProcessError){finish(false);});
    connect(&m_process,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,
        [this](int code,QProcess::ExitStatus status){
            QPointer<BchSuccessReleaseReviewJob> guard(this);const auto request=m_request;
            drain();if (guard && request==m_request) finish(code==0 && status==QProcess::NormalExit);
        });
}
BchSuccessReleaseReviewJob::~BchSuccessReleaseReviewJob() {
    disconnect(&m_process,nullptr,this,nullptr);
    if (m_process.state()!=QProcess::NotRunning) {m_process.kill();m_process.waitForFinished(1000);}
}
bool BchSuccessReleaseReviewJob::configure(const QString &path,BchSuccessReleaseProtocol::Action action) {
    if (!m_path.isEmpty() || m_busy || BchSuccessReleaseProtocol::actionName(action).isEmpty()) return false;
    const auto bytes=LitecoinJournalProtocol::privateConfig(path);QJsonObject wrapper,config,record,admission;
    if (!canonicalObject(bytes,&wrapper) || wrapper.size()!=4
        || wrapper.value("schema")!="monzero-private-bch-success-release-review-v1") return false;
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
    switch (action) {
    case BchSuccessReleaseProtocol::Action::Prepare: m_operation="release";break;
    case BchSuccessReleaseProtocol::Action::RetainSignature: m_operation="signature";break;
    case BchSuccessReleaseProtocol::Action::ReleaseSigned: m_operation="signed-release";break;
    }
    m_action=action;m_source=wrapper;
    m_sources={{path,bytes},{admissionPath,configBytes},{quotePath,quoteBytes}};
    m_path=path;m_program=config.value("helper").toString();m_digest=config.value("helperDigest").toString();
    m_trade=config.value("tradeId").toString();m_config=config;m_record=record;
    m_sourcesValid=true;m_state="ready";emit changed();return true;
}
bool BchSuccessReleaseReviewJob::sourcesMatch() const {
    if (!m_sourcesValid || !BackendExecutable::matches(m_program,m_digest)) return false;
    for (auto i=m_sources.cbegin();i!=m_sources.cend();++i)
        if (LitecoinJournalProtocol::privateConfig(i.key())!=i.value()) return false;
    return true;
}
void BchSuccessReleaseReviewJob::invalidate() {
    if (m_busy) {finish(false);return;}
    if (m_review.isEmpty()) return;
    m_review={};m_proofReview={};m_context={};m_state="unknown";emit changed();
}
QVariantMap BchSuccessReleaseReviewJob::status() const {
    return {{"configured",!m_path.isEmpty()},{"busy",m_busy},{"state",m_state},{"tradeId",m_trade},{"quoteCommitment",m_config.value("quoteCommitment").toString()},
        {"review",m_review.toVariantMap()},{"action",BchSuccessReleaseProtocol::actionName(m_action)},
        {"reviewAvailable",!m_path.isEmpty() && m_sourcesValid && !m_busy},
        {"executable",false},{"chainObserved",false}};
}
bool BchSuccessReleaseReviewJob::review(const QJsonObject &proofReview,const QJsonObject &fundingContext) {
    if (m_busy || m_path.isEmpty() || m_process.state()!=QProcess::NotRunning) return false;
    m_review={};m_proofReview={};m_context={};
    const bool same=sourcesMatch();m_sourcesValid=same;
    if (!same || !BchNativeProofProtocol::fundingContext(fundingContext)
        || !BchNativeProofProtocol::review(proofReview,m_config,m_record,
            fundingContext.value("bchFundingOutpoint").toString(),fundingContext.value("destination").toString(),
            fundingContext.value("admission").toString(),fundingContext.value("agreement").toString())) {
        m_state="unknown";emit changed();return false;
    }
    m_context=fundingContext;m_proofReview=proofReview;m_request=QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_input=QJsonDocument(QJsonObject{{"schema",QString("monzero-bch-success-%1-review-request-v1").arg(m_operation)},
        {"requestId",m_request}}).toJson(QJsonDocument::Compact);
    m_output.clear();m_errors=0;m_busy=true;m_state="reviewing";
    QProcessEnvironment environment;environment.insert("LANG","C");
    m_process.setProcessEnvironment(environment);m_process.setProcessChannelMode(QProcess::SeparateChannels);
    m_deadline.start(180000);
    m_process.start(m_program,{QString("--success-%1-review-stdio-v1").arg(m_operation),m_path});emit changed();return true;
}
void BchSuccessReleaseReviewJob::drain() {
    if (!m_busy) return;
    m_process.setReadChannel(QProcess::StandardOutput);m_output+=m_process.read(16385-m_output.size());
    m_process.setReadChannel(QProcess::StandardError);m_errors+=m_process.read(1025-m_errors).size();
    if (m_output.size()>16384 || m_errors>1024) finish(false);
}
void BchSuccessReleaseReviewJob::finish(bool success) {
    if (!m_busy) return;
    m_busy=false;m_deadline.stop();
    if (m_process.state()!=QProcess::NotRunning) m_process.kill();
    QJsonObject response;const bool same=sourcesMatch();m_sourcesValid=same;
    const bool valid=success && same && m_errors==0 && canonicalObject(m_output,&response)
        && BchSuccessReleaseProtocol::response(response,m_request,m_config,m_record,m_source,m_proofReview,m_context,m_action);
    m_review={};m_state="unknown";
    if (valid) {m_review=response.value("review").toObject();m_state="reviewed";}
    m_output.clear();m_input.clear();m_context={};m_proofReview={};emit changed();
}
