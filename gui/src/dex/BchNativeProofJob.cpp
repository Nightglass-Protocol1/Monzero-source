#include "BchNativeProofJob.h"
#include "BchAdmissionProtocol.h"
#include "BchNativeProofProtocol.h"
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

BchNativeProofJob::BchNativeProofJob(QObject *parent):QObject(parent) {
    m_deadline.setSingleShot(true);
    connect(&m_deadline,&QTimer::timeout,this,[this]{finish(false,"timeout");});
    connect(&m_process,&QProcess::started,this,[this]{
        if (!m_busy || m_process.write(m_input)!=m_input.size()) {finish(false);return;}
        m_process.closeWriteChannel();m_input.clear();
    });
    connect(&m_process,&QProcess::readyReadStandardOutput,this,&BchNativeProofJob::drain);
    connect(&m_process,&QProcess::readyReadStandardError,this,&BchNativeProofJob::drain);
    connect(&m_process,&QProcess::errorOccurred,this,[this](QProcess::ProcessError){finish(false);});
    connect(&m_process,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,
        [this](int code,QProcess::ExitStatus status){
            QPointer<BchNativeProofJob> guard(this);const auto request=m_request;
            drain();if (guard && request==m_request) finish(code==0 && status==QProcess::NormalExit);
        });
}
BchNativeProofJob::~BchNativeProofJob() {
    disconnect(&m_process,nullptr,this,nullptr);
    if (m_process.state()!=QProcess::NotRunning) {m_process.kill();m_process.waitForFinished(1000);}
}
bool BchNativeProofJob::configure(const QString &path,const QJsonObject &fundingContext) {
    if (!m_path.isEmpty() || m_busy) return false;
    const auto bytes=LitecoinJournalProtocol::privateConfig(path);QJsonObject wrapper,config,record,admission;
    if (!canonicalObject(bytes,&wrapper) || wrapper.size()!=3
        || wrapper.value("schema")!="monzero-private-bch-native-proof-v1") return false;
    const auto admissionPath=wrapper.value("admissionConfigFile").toString();
    const auto configBytes=LitecoinJournalProtocol::privateConfig(admissionPath);
    if (!canonicalObject(configBytes,&config) || config.value("role")!="bch-funder") return false;
    const auto quotePath=config.value("quoteFile").toString();
    const auto quoteBytes=LitecoinJournalProtocol::privateConfig(quotePath);
    if (!canonicalObject(quoteBytes,&record) || !expectedReview(config,record,&admission)
        || !BackendExecutable::matches(config.value("helper").toString(),config.value("helperDigest").toString())) return false;
    const auto messagePath=wrapper.value("peerMessageFile").toString();
    if (!QDir::isAbsolutePath(messagePath) || QDir::cleanPath(messagePath)!=messagePath
        || QStringList{path,admissionPath,quotePath}.contains(messagePath)) return false;
    if (!fundingContext.isEmpty() && !BchNativeProofProtocol::fundingContext(fundingContext)) return false;
    m_sources={{path,bytes},{admissionPath,configBytes},{quotePath,quoteBytes}};
    m_path=path;m_program=config.value("helper").toString();m_digest=config.value("helperDigest").toString();
    m_trade=config.value("tradeId").toString();m_quote=config.value("quoteCommitment").toString();
    m_peer=record.value("terms").toObject().value("xmzFunder").toString();
    m_config=config;m_record=record;m_context=fundingContext;
    m_sourcesValid=true;m_state="ready";emit changed();return true;
}
bool BchNativeProofJob::sourcesMatch() const {
    if (!m_sourcesValid || !BackendExecutable::matches(m_program,m_digest)) return false;
    for (auto i=m_sources.cbegin();i!=m_sources.cend();++i)
        if (LitecoinJournalProtocol::privateConfig(i.key())!=i.value()) return false;
    return true;
}
QJsonObject BchNativeProofJob::retainedFundingContext() const {
    return m_state=="saved" && m_checked && !m_busy && m_sourcesValid ? m_context : QJsonObject{};
}
QVariantMap BchNativeProofJob::status() const {
    const bool available=!m_path.isEmpty() && m_sourcesValid && !m_busy;
    return {{"configured",!m_path.isEmpty()},{"busy",m_busy},{"state",m_state},{"tradeId",m_trade},
        {"failureReason",m_failureReason},{"operation",m_operation},
        {"quoteCommitment",m_quote},{"peerIdentity",m_peer},{"review",m_review.toVariantMap()},
        {"receipt",m_receipt.toVariantMap()},{"checkAvailable",available},{"checked",m_checked},
        {"reviewAvailable",available && m_checked && !m_submitAttempted && m_receipt.isEmpty()},
        {"submitAvailable",available && m_checked && !m_submitAttempted && m_state=="reviewed" && !m_review.isEmpty() && m_receipt.isEmpty()},
        {"submitAttempted",m_submitAttempted},{"executable",false},{"chainObserved",false}};
}
bool BchNativeProofJob::review() {
    return status().value("reviewAvailable").toBool() && run("review");
}
bool BchNativeProofJob::submit(const QString &commitment) {
    return status().value("submitAvailable").toBool() && commitment==m_review.value("commitment").toString()
        && run("submit",commitment);
}
bool BchNativeProofJob::refresh() {return run(m_context.isEmpty()?"context":"status");}
bool BchNativeProofJob::run(const QString &operation,const QString &commitment) {
    if (m_busy || m_path.isEmpty() || m_process.state()!=QProcess::NotRunning) return false;
    const bool same=sourcesMatch();
    if (!same) {
        m_sourcesValid=same;m_review={};m_receipt={};m_checked=false;m_state="unknown";
        m_failureReason="sources-changed";emit changed();return false;
    }
    m_failureReason.clear();
    m_request=QUuid::createUuid().toString(QUuid::WithoutBraces);m_operation=operation;
    QJsonObject request{{"schema","monzero-bch-native-proof-"+operation+"-request-v1"},{"requestId",m_request}};
    m_approved={};
    if (operation=="submit") {request["commitment"]=commitment;m_approved=m_review;m_submitAttempted=true;}
    m_input=QJsonDocument(request).toJson(QJsonDocument::Compact);
    m_output.clear();m_errors=0;m_review={};m_receipt={};m_busy=true;
    m_state=operation=="review"?"reviewing":operation=="submit"?"submitting":"checking";
    QProcessEnvironment environment;environment.insert("LANG","C");
    m_process.setProcessEnvironment(environment);m_process.setProcessChannelMode(QProcess::SeparateChannels);
    m_deadline.start(180000);
    m_process.start(m_program,{"--native-proof-"+operation+"-stdio-v1",m_path});emit changed();return true;
}
void BchNativeProofJob::drain() {
    if (!m_busy) return;
    m_process.setReadChannel(QProcess::StandardOutput);m_output+=m_process.read(16385-m_output.size());
    m_process.setReadChannel(QProcess::StandardError);m_errors+=m_process.read(1025-m_errors).size();
    if (m_output.size()>16384 || m_errors>1024) finish(false,"output-limit");
}
void BchNativeProofJob::finish(bool success,const QString &reason) {
    if (!m_busy) return;
    m_busy=false;m_deadline.stop();
    if (m_process.state()!=QProcess::NotRunning) m_process.kill();
    QJsonObject response;const bool same=sourcesMatch();m_sourcesValid=same;
    // Fixed public codes only: helper stderr and proof bytes remain private.
    const QString failure=!same?QString("sources-changed"):!reason.isEmpty()?reason:
        !success?QString("process-failed"):m_errors?QString("unexpected-stderr"):QString("invalid-response");
    if (m_operation=="context") {
        const bool valid=success && same && m_errors==0 && canonicalObject(m_output,&response)
            && BchNativeProofProtocol::contextResponse(response,m_request,m_config,m_record);
        m_output.clear();m_input.clear();m_approved={};
        if (valid) {
            m_context=response.value("context").toObject();
            // Keep the check busy across context recovery and saved status.
            // Neither completion notification nor consent is published between them.
            if (run("status")) return;
        }
        m_failureReason=failure;m_state="unknown";m_checked=false;m_review={};m_receipt={};emit changed();return;
    }
    const bool valid=success && same && m_errors==0 && canonicalObject(m_output,&response)
        && BchNativeProofProtocol::response(response,m_operation,m_request,m_config,m_record,
            m_context.value("bchFundingOutpoint").toString(),m_context.value("destination").toString(),
            m_context.value("admission").toString(),m_context.value("agreement").toString(),
            quint64(QDateTime::currentSecsSinceEpoch()))
        && (m_operation!="submit" || response.value("review").toObject()==m_approved);
    const auto receipt=response.value("receipt").toObject();
    if (!valid) {m_failureReason=failure;m_state="unknown";m_review={};m_receipt={};m_checked=false;}
    else {
        m_checked=true;m_receipt=receipt;
        if (!receipt.isEmpty()) {m_review=response.value("review").toObject();m_state="saved";}
        else if (m_operation=="review") {m_review=response.value("review").toObject();m_state="reviewed";}
        else m_state="not-saved";
    }
    m_output.clear();m_input.clear();m_approved={};emit changed();
}
