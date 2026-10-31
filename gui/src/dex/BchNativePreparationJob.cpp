#include "BchNativePreparationJob.h"
#include "BchAdmissionProtocol.h"
#include "BchNativePreparationProtocol.h"
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

BchNativePreparationJob::BchNativePreparationJob(QObject *parent):QObject(parent) {
    m_deadline.setSingleShot(true);
    connect(&m_deadline,&QTimer::timeout,this,[this]{finish(false);});
    connect(&m_process,&QProcess::started,this,[this]{
        if (!m_busy || m_process.write(m_input)!=m_input.size()) {finish(false);return;}
        m_process.closeWriteChannel();m_input.clear();
    });
    connect(&m_process,&QProcess::readyReadStandardOutput,this,&BchNativePreparationJob::drain);
    connect(&m_process,&QProcess::readyReadStandardError,this,&BchNativePreparationJob::drain);
    connect(&m_process,&QProcess::errorOccurred,this,[this](QProcess::ProcessError){finish(false);});
    connect(&m_process,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,
        [this](int code,QProcess::ExitStatus status){
            QPointer<BchNativePreparationJob> guard(this);const auto request=m_request;
            drain();if (guard && request==m_request) finish(code==0 && status==QProcess::NormalExit);
        });
}
BchNativePreparationJob::~BchNativePreparationJob() {
    disconnect(&m_process,nullptr,this,nullptr);
    if (m_process.state()!=QProcess::NotRunning) {m_process.kill();m_process.waitForFinished(1000);}
}
namespace {
bool decimalTime(const QJsonValue &value,quint64 *out) {
    if (!value.isString()) return false;
    bool ok=false;const auto n=value.toString().toULongLong(&ok);
    if (!ok || QString::number(n)!=value.toString()) return false;
    *out=n;return true;
}
}
bool BchNativePreparationJob::configure(const QString &path) {
    if (!m_path.isEmpty() || m_busy) return false;
    const auto bytes=LitecoinJournalProtocol::privateConfig(path);QJsonObject wrapper,config,record,admission;
    if (!canonicalObject(bytes,&wrapper)
        || !BchNativePreparationProtocol::configurationVersion(wrapper)) return false;
    const auto admissionPath=wrapper.value("admissionConfigFile").toString();
    const auto configBytes=LitecoinJournalProtocol::privateConfig(admissionPath);
    if (!canonicalObject(configBytes,&config) || config.value("role")!="native-funder") return false;
    const auto quotePath=config.value("quoteFile").toString();
    const auto quoteBytes=LitecoinJournalProtocol::privateConfig(quotePath);
    if (!canonicalObject(quoteBytes,&record) || !expectedReview(config,record,&admission)
        || !BackendExecutable::matches(config.value("helper").toString(),config.value("helperDigest").toString())) return false;
    quint64 account=0,priority=0,margin=0;
    if (!hex(wrapper.value("deploymentCommitment").toString(),64)
        || !BchNativePreparationProtocol::address(wrapper.value("wallet"))
        || !integer(wrapper.value("account"),4294967295ULL,&account)
        || !integer(wrapper.value("priority"),4,&priority) || !priority
        || !integer(wrapper.value("safetyBlocks"),4294967295ULL,&margin) || !margin) return false;
    QStringList paths{path,admissionPath,quotePath};
    for (const auto field:{"bchRpcFile","nativeRpcFile"}) {
        const auto rpc=wrapper.value(field).toString();
        if (!QDir::isAbsolutePath(rpc) || QDir::cleanPath(rpc)!=rpc || paths.contains(rpc)) return false;
        paths.append(rpc);
    }
    quint64 admitted=0,expires=0;
    if (!integer(config.value("admittedAt"),9007199254740991ULL,&admitted)
        || !decimalTime(record.value("terms").toObject().value("expiresAt"),&expires) || expires<=admitted) return false;
    m_sources={{path,bytes},{admissionPath,configBytes},{quotePath,quoteBytes}};
    m_path=path;m_program=config.value("helper").toString();m_digest=config.value("helperDigest").toString();
    m_trade=config.value("tradeId").toString();m_quote=config.value("quoteCommitment").toString();
    m_peer=record.value("terms").toObject().value("bchFunder").toString();m_admitted=admitted;m_expires=expires;
    m_config=config;m_record=record;m_source=wrapper;
    m_sourcesValid=true;m_state="ready";emit changed();return true;
}
bool BchNativePreparationJob::sourcesMatch() const {
    if (!m_sourcesValid || !BackendExecutable::matches(m_program,m_digest)) return false;
    for (auto i=m_sources.cbegin();i!=m_sources.cend();++i)
        if (LitecoinJournalProtocol::privateConfig(i.key())!=i.value()) return false;
    return true;
}
bool BchNativePreparationJob::validReview(const QJsonObject &review) const {
    return BchNativePreparationProtocol::review(review,m_config,m_record,m_source);
}
QVariantMap BchNativePreparationJob::status() const {
    const bool available=!m_path.isEmpty() && m_sourcesValid && !m_busy;
    return {{"configured",!m_path.isEmpty()},{"busy",m_busy},{"state",m_state},{"tradeId",m_trade},
        {"quoteCommitment",m_quote},{"peerIdentity",m_peer},{"review",m_review.toVariantMap()},
        {"preparation",m_preparation.toVariantMap()},{"checkAvailable",available},{"checked",m_checked},
        {"reviewAvailable",available && m_checked && !m_submitAttempted && m_preparation.isEmpty()},
        {"submitAvailable",available && m_checked && !m_submitAttempted && !m_review.isEmpty() && m_preparation.isEmpty()},
        {"submitAttempted",m_submitAttempted},{"executable",false},{"chainObserved",false}};
}
bool BchNativePreparationJob::review() {
    return status().value("reviewAvailable").toBool() && run("review");
}
bool BchNativePreparationJob::submit(const QString &commitment) {
    return status().value("submitAvailable").toBool() && commitment==m_review.value("commitment").toString()
        && run("submit",commitment);
}
bool BchNativePreparationJob::refresh() {return run("status");}
bool BchNativePreparationJob::run(const QString &operation,const QString &commitment) {
    if (m_busy || m_path.isEmpty() || m_process.state()!=QProcess::NotRunning) return false;
    const bool same=sourcesMatch();
    const auto now=quint64(QDateTime::currentSecsSinceEpoch());
    if (!same || (operation!="status" && (now<m_admitted || now>=m_expires))) {
        m_sourcesValid=same;m_review={};m_preparation={};m_checked=false;m_state="unknown";emit changed();return false;
    }
    m_request=QUuid::createUuid().toString(QUuid::WithoutBraces);m_operation=operation;
    QJsonObject request{{"schema","monzero-bch-native-preparation-"+operation+"-request-v1"},{"requestId",m_request}};
    m_approved={};
    if (operation=="submit") {request["commitment"]=commitment;m_approved=m_review;m_submitAttempted=true;}
    m_input=QJsonDocument(request).toJson(QJsonDocument::Compact);
    m_output.clear();m_errors=0;m_review={};m_preparation={};m_busy=true;
    m_state=operation=="review"?"reviewing":operation=="submit"?"submitting":"checking";
    QProcessEnvironment environment;environment.insert("LANG","C");
    m_process.setProcessEnvironment(environment);m_process.setProcessChannelMode(QProcess::SeparateChannels);
    m_deadline.start(180000);
    m_process.start(m_program,{"--native-preparation-"+operation+"-stdio-v1",m_path});emit changed();return true;
}
void BchNativePreparationJob::drain() {
    if (!m_busy) return;
    m_process.setReadChannel(QProcess::StandardOutput);m_output+=m_process.read(16385-m_output.size());
    m_process.setReadChannel(QProcess::StandardError);m_errors+=m_process.read(1025-m_errors).size();
    if (m_output.size()>16384 || m_errors>1024) finish(false);
}
void BchNativePreparationJob::finish(bool success) {
    if (!m_busy) return;
    m_busy=false;m_deadline.stop();
    if (m_process.state()!=QProcess::NotRunning) m_process.kill();
    QJsonObject response;const bool same=sourcesMatch();m_sourcesValid=same;
    bool valid=success && same && m_errors==0 && canonicalObject(m_output,&response)
        && response.size()==7 && response.value("schema")=="monzero-bch-native-preparation-"+m_operation+"-response-v1"
        && response.value("requestId")==m_request && response.value("tradeId")==m_trade
        && response.value("executable").isBool() && !response.value("executable").toBool()
        && response.value("chainObserved").isBool() && !response.value("chainObserved").toBool();
    const auto publicReview=response.value("review");
    const auto saved=response.value("preparation");
    if (valid && m_operation=="status" && saved.isNull()) valid=publicReview.isNull();
    else if (valid) valid=publicReview.isObject() && validReview(publicReview.toObject())
        && (m_operation!="submit" || publicReview.toObject()==m_approved);
    QJsonObject preparation;
    if (valid && saved.isObject()) {
        preparation=saved.toObject();quint64 attempted=0;
        valid=BchNativePreparationProtocol::saved(preparation,publicReview.toObject(),m_config,
            quint64(QDateTime::currentSecsSinceEpoch()))
            && decimalTime(preparation.value("attemptedAt"),&attempted) && attempted<m_expires
            && m_operation!="review" && (m_operation!="submit" || preparation.value("state")=="prepared");
    } else if (valid) valid=saved.isNull() && m_operation!="submit";
    if (!valid) {m_state="unknown";m_review={};m_preparation={};m_checked=false;}
    else {
        m_checked=true;m_preparation=preparation;
        if (!preparation.isEmpty()) {m_review=response.value("review").toObject();m_state=preparation.value("state").toString();}
        else if (m_operation=="review") {m_review=response.value("review").toObject();m_state="reviewed";}
        else m_state="not-attempted";
    }
    m_output.clear();m_input.clear();m_approved={};emit changed();
}
