#include "BchAdmissionJob.h"
#include "BchAdmissionProtocol.h"
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

BchAdmissionJob::BchAdmissionJob(QObject *parent):QObject(parent) {
    m_deadline.setSingleShot(true);
    connect(&m_deadline,&QTimer::timeout,this,[this]{finish(false);});
    connect(&m_process,&QProcess::started,this,[this]{
        if (!m_busy || m_process.write(m_input)!=m_input.size()) {finish(false);return;}
        m_process.closeWriteChannel();m_input.clear();
    });
    connect(&m_process,&QProcess::readyReadStandardOutput,this,&BchAdmissionJob::drain);
    connect(&m_process,&QProcess::readyReadStandardError,this,&BchAdmissionJob::drain);
    connect(&m_process,&QProcess::errorOccurred,this,[this](QProcess::ProcessError){finish(false);});
    connect(&m_process,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,
        [this](int code,QProcess::ExitStatus status){
            QPointer<BchAdmissionJob> guard(this);const auto request=m_request;
            drain();if (guard && request==m_request) finish(code==0 && status==QProcess::NormalExit);
        });
}
BchAdmissionJob::~BchAdmissionJob() {
    disconnect(&m_process,nullptr,this,nullptr);
    if (m_process.state()!=QProcess::NotRunning) {m_process.kill();m_process.waitForFinished(1000);}
}
bool BchAdmissionJob::configure(const QString &path) {
    if (!m_path.isEmpty() || m_busy) return false;
    const auto bytes=LitecoinJournalProtocol::privateConfig(path);QJsonObject config,record,review;
    if (!canonicalObject(bytes,&config)) return false;
    const auto quotePath=config.value("quoteFile").toString();
    const auto quote=LitecoinJournalProtocol::privateConfig(quotePath);
    if (!canonicalObject(quote,&record) || !expectedReview(config,record,&review)
        || !BackendExecutable::matches(config.value("helper").toString(),config.value("helperDigest").toString())) return false;
    m_path=path;m_configBytes=bytes;m_quotePath=quotePath;m_quoteBytes=quote;
    m_program=config.value("helper").toString();m_digest=config.value("helperDigest").toString();
    m_trade=config.value("tradeId").toString();m_expectedReview=review;
    m_sourcesValid=true;m_state="ready";emit changed();return true;
}
bool BchAdmissionJob::sourcesMatch() const {
    return m_sourcesValid && BackendExecutable::matches(m_program,m_digest)
        && LitecoinJournalProtocol::privateConfig(m_path)==m_configBytes
        && LitecoinJournalProtocol::privateConfig(m_quotePath)==m_quoteBytes;
}
QJsonObject BchAdmissionJob::lightFundingContext() const {
    if(m_busy || m_state!="admitted" || m_operation!="status" || m_admission.isEmpty() || !sourcesMatch()
        || m_expectedReview.value("details").toObject().value("role")!="bch-funder")return {};
    auto context=QJsonDocument::fromJson(m_configBytes).object();
    context.remove("helper");context.remove("helperDigest");context.remove("role");
    context["schema"]="monzero-private-bch-light-dex-v1";
    return context;
}
QVariantMap BchAdmissionJob::status() const {
    const bool available=!m_path.isEmpty() && m_sourcesValid && !m_busy;
    return {{"configured",!m_path.isEmpty()},{"busy",m_busy},{"state",m_state},{"tradeId",m_trade},
        {"role",m_expectedReview.value("details").toObject().value("role").toString()},
        {"review",m_review.toVariantMap()},{"admission",m_admission.toVariantMap()},
        {"reviewAvailable",available && !m_submitAttempted && m_admission.isEmpty()},
        {"submitAvailable",available && !m_submitAttempted && !m_review.isEmpty() && m_admission.isEmpty()},
        {"checkAvailable",available},{"submitAttempted",m_submitAttempted},{"executable",false}};
}
bool BchAdmissionJob::review() {
    if (!status().value("reviewAvailable").toBool()) return false;
    return run("review");
}
bool BchAdmissionJob::submit(const QString &commitment) {
    if (!status().value("submitAvailable").toBool() || commitment!=m_review.value("commitment").toString()) return false;
    return run("submit",commitment);
}
bool BchAdmissionJob::refresh() { return run("status"); }
bool BchAdmissionJob::run(const QString &operation,const QString &commitment) {
    if (m_busy || m_path.isEmpty() || m_process.state()!=QProcess::NotRunning) return false;
    if (!sourcesMatch()) {
        m_sourcesValid=false;m_review={};m_admission={};m_state="unknown";emit changed();return false;
    }
    m_request=QUuid::createUuid().toString(QUuid::WithoutBraces);m_operation=operation;
    QJsonObject request{{"schema","monzero-bch-admission-"+operation+"-request-v1"},{"requestId",m_request}};
    if (operation=="submit") {request["commitment"]=commitment;m_submitAttempted=true;}
    m_input=QJsonDocument(request).toJson(QJsonDocument::Compact);
    m_output.clear();m_errors=0;m_review={};m_admission={};m_busy=true;
    m_state=operation=="review"?"reviewing":operation=="submit"?"submitting":"checking";
    QProcessEnvironment environment;environment.insert("LANG","C");
    m_process.setProcessEnvironment(environment);m_process.setProcessChannelMode(QProcess::SeparateChannels);
    m_deadline.start(180000);
    m_process.start(m_program,{"--admission-"+operation+"-stdio-v1",m_path});emit changed();return true;
}
void BchAdmissionJob::drain() {
    if (!m_busy) return;
    m_process.setReadChannel(QProcess::StandardOutput);m_output+=m_process.read(16385-m_output.size());
    m_process.setReadChannel(QProcess::StandardError);m_errors+=m_process.read(1025-m_errors).size();
    if (m_output.size()>16384 || m_errors>1024) finish(false);
}
void BchAdmissionJob::finish(bool success) {
    if (!m_busy) return;
    m_busy=false;m_deadline.stop();
    if (m_process.state()!=QProcess::NotRunning) m_process.kill();
    QJsonObject response;
    const bool same=sourcesMatch();m_sourcesValid=same;
    bool valid=success && same && m_errors==0 && canonicalObject(m_output,&response)
        && response.size()==(m_operation=="status"?7:6)
        && response.value("schema")=="monzero-bch-admission-"+m_operation+"-response-v1"
        && response.value("requestId")==m_request && response.value("tradeId")==m_trade
        && response.value("executable").isBool() && !response.value("executable").toBool()
        && response.value("admitted").isBool() && response.value("admitted").toBool()==(m_operation!="review")
        && response.value("review").isObject() && response.value("review").toObject()==m_expectedReview;
    if (valid && m_operation=="status") {
        quint64 admitted=0;
        const auto config=QJsonDocument::fromJson(m_configBytes).object();
        bool expiresOk=false;
        const auto expires=m_expectedReview.value("details").toObject().value("terms").toObject()
            .value("expiresAt").toString().toULongLong(&expiresOk);
        valid=integer(response.value("admittedAt"),quint64(QDateTime::currentSecsSinceEpoch()),&admitted)
            && admitted>=quint64(config.value("admittedAt").toDouble()) && expiresOk && admitted<expires;
    }
    if (!valid) {m_state="unknown";m_review={};m_admission={};}
    else if (m_operation=="review") {m_review=m_expectedReview;m_state="reviewed";}
    else {m_admission=response;m_state="admitted";}
    m_output.clear();m_input.clear();emit changed();
}
