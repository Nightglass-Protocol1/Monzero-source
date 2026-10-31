#include "BchRecoveryRetentionJob.h"
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

BchRecoveryRetentionJob::BchRecoveryRetentionJob(QObject *parent):QObject(parent) {
    m_deadline.setSingleShot(true);
    connect(&m_deadline,&QTimer::timeout,this,[this]{finish(false);});
    connect(&m_process,&QProcess::started,this,[this]{
        if (!m_busy || m_process.write(m_input)!=m_input.size()) {finish(false);return;}
        m_process.closeWriteChannel();m_input.clear();
    });
    connect(&m_process,&QProcess::readyReadStandardOutput,this,&BchRecoveryRetentionJob::drain);
    connect(&m_process,&QProcess::readyReadStandardError,this,&BchRecoveryRetentionJob::drain);
    connect(&m_process,&QProcess::errorOccurred,this,[this](QProcess::ProcessError){finish(false);});
    connect(&m_process,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,
        [this](int code,QProcess::ExitStatus status){
            QPointer<BchRecoveryRetentionJob> guard(this);const auto request=m_request;
            drain();if (guard && request==m_request) finish(code==0 && status==QProcess::NormalExit);
        });
}
BchRecoveryRetentionJob::~BchRecoveryRetentionJob() {
    disconnect(&m_process,nullptr,this,nullptr);
    if (m_process.state()!=QProcess::NotRunning) {m_process.kill();m_process.waitForFinished(1000);}
}
namespace {
QString boundDigest(const QByteArray &domain,const QByteArray &bytes) {
    QByteArray value=domain;value.append('\0');value+=bytes;
    return QString::fromLatin1(QCryptographicHash::hash(value,QCryptographicHash::Sha256).toHex());
}
bool decimalTime(const QJsonValue &value,quint64 *out) {
    if (!value.isString()) return false;
    bool ok=false;const auto n=value.toString().toULongLong(&ok);
    if (!ok || QString::number(n)!=value.toString()) return false;
    *out=n;return true;
}
}
bool BchRecoveryRetentionJob::configure(const QString &path) {
    if (!m_path.isEmpty() || m_busy) return false;
    const auto configBytes=LitecoinJournalProtocol::privateConfig(path);
    QJsonObject config,record,admission;
    if (!canonicalObject(configBytes,&config) || config.value("role")!="bch-funder") return false;
    const auto quotePath=config.value("quoteFile").toString();
    const auto quoteBytes=LitecoinJournalProtocol::privateConfig(quotePath);
    if (!canonicalObject(quoteBytes,&record) || !expectedReview(config,record,&admission)
        || !BackendExecutable::matches(config.value("helper").toString(),config.value("helperDigest").toString())) return false;
    quint64 admitted=0,expires=0;
    if (!integer(config.value("admittedAt"),9007199254740991ULL,&admitted)
        || !decimalTime(record.value("terms").toObject().value("expiresAt"),&expires) || expires<=admitted) return false;
    m_sources={{path,configBytes},{quotePath,quoteBytes}};
    m_path=path;m_program=config.value("helper").toString();m_digest=config.value("helperDigest").toString();
    m_trade=config.value("tradeId").toString();m_quote=config.value("quoteCommitment").toString();
    m_admitted=admitted;m_expires=expires;
    m_sourcesValid=true;m_state="ready";emit changed();return true;
}
bool BchRecoveryRetentionJob::sourcesMatch() const {
    if (!m_sourcesValid || !BackendExecutable::matches(m_program,m_digest)) return false;
    for (auto i=m_sources.cbegin();i!=m_sources.cend();++i)
        if (LitecoinJournalProtocol::privateConfig(i.key())!=i.value()) return false;
    return true;
}
bool BchRecoveryRetentionJob::validReview(const QJsonObject &review) const {
    if (review.size()!=2 || !review.value("details").isObject()) return false;
    const auto details=review.value("details").toObject();
    if (details.size()!=11 || details.value("schema")!="monzero-bch-recovery-retention-review-v1"
        || details.value("action")!="retain-private-recovery-only" || details.value("swapId")!=m_trade
        || details.value("quote")!=m_quote || review.value("commitment")!=boundDigest(
            "monzero-bch-recovery-retention-review-v1",QJsonDocument(details).toJson(QJsonDocument::Compact))) return false;
    for (const auto key:{"admission","funding","receipt","fundingTxid","cancelTxid","refundTxid","punishTxid"})
        if (!hex(details.value(key).toString(),64)) return false;
    return true;
}
QVariantMap BchRecoveryRetentionJob::status() const {
    const bool available=!m_path.isEmpty() && m_sourcesValid && !m_busy;
    return {{"configured",!m_path.isEmpty()},{"busy",m_busy},{"state",m_state},{"tradeId",m_trade},
        {"quoteCommitment",m_quote},{"review",m_review.toVariantMap()},
        {"recovery",m_recovery.toVariantMap()},{"checkAvailable",available},{"checked",m_checked},
        {"reviewAvailable",available && m_checked && !m_submitAttempted && m_recovery.isEmpty()},
        {"submitAvailable",available && m_checked && !m_submitAttempted && !m_review.isEmpty() && m_recovery.isEmpty()},
        {"submitAttempted",m_submitAttempted},{"executable",false},{"chainObserved",false}};
}
bool BchRecoveryRetentionJob::review() {
    return status().value("reviewAvailable").toBool() && run("review");
}
bool BchRecoveryRetentionJob::submit(const QString &commitment) {
    return status().value("submitAvailable").toBool() && commitment==m_review.value("commitment").toString()
        && run("submit",commitment);
}
bool BchRecoveryRetentionJob::refresh() {return run("status");}
bool BchRecoveryRetentionJob::run(const QString &operation,const QString &commitment) {
    if (m_busy || m_path.isEmpty() || m_process.state()!=QProcess::NotRunning) return false;
    const bool same=sourcesMatch();
    if (!same) {
        m_sourcesValid=same;m_review={};m_recovery={};m_checked=false;m_state="unknown";emit changed();return false;
    }
    m_request=QUuid::createUuid().toString(QUuid::WithoutBraces);m_operation=operation;
    QJsonObject request{{"schema","monzero-bch-recovery-retention-"+operation+"-request-v1"},{"requestId",m_request}};
    m_approved={};
    if (operation=="submit") {request["commitment"]=commitment;m_approved=m_review;m_submitAttempted=true;}
    m_input=QJsonDocument(request).toJson(QJsonDocument::Compact);
    m_output.clear();m_errors=0;m_review={};m_recovery={};m_busy=true;
    m_state=operation=="review"?"reviewing":operation=="submit"?"submitting":"checking";
    QProcessEnvironment environment;environment.insert("LANG","C");
    m_process.setProcessEnvironment(environment);m_process.setProcessChannelMode(QProcess::SeparateChannels);
    m_deadline.start(180000);
    m_process.start(m_program,{"--recovery-retention-"+operation+"-stdio-v1",m_path});emit changed();return true;
}
void BchRecoveryRetentionJob::drain() {
    if (!m_busy) return;
    m_process.setReadChannel(QProcess::StandardOutput);m_output+=m_process.read(16385-m_output.size());
    m_process.setReadChannel(QProcess::StandardError);m_errors+=m_process.read(1025-m_errors).size();
    if (m_output.size()>16384 || m_errors>1024) finish(false);
}
void BchRecoveryRetentionJob::finish(bool success) {
    if (!m_busy) return;
    m_busy=false;m_deadline.stop();
    if (m_process.state()!=QProcess::NotRunning) m_process.kill();
    QJsonObject response;const bool same=sourcesMatch();m_sourcesValid=same;
    bool valid=success && same && m_errors==0 && canonicalObject(m_output,&response)
        && response.size()==7 && response.value("schema")=="monzero-bch-recovery-retention-"+m_operation+"-response-v1"
        && response.value("requestId")==m_request && response.value("tradeId")==m_trade
        && response.value("executable").isBool() && !response.value("executable").toBool()
        && response.value("chainObserved").isBool() && !response.value("chainObserved").toBool();
    if (valid && m_operation=="status") valid=response.value("review").isNull();
    else if (valid) valid=response.value("review").isObject() && validReview(response.value("review").toObject())
        && (m_operation!="submit" || response.value("review").toObject()==m_approved);
    QJsonObject recovery;
    if (valid && response.value("recovery").isObject()) {
        recovery=response.value("recovery").toObject();quint64 received=0;
        valid=recovery.size()==5 && hex(recovery.value("commitment").toString(),64)
            && decimalTime(recovery.value("preparedAt"),&received) && received>=m_admitted && received<m_expires
            && received<=quint64(QDateTime::currentSecsSinceEpoch());
        for (const auto key:{"cancelTxid","refundTxid","punishTxid"}) {
            valid=valid && hex(recovery.value(key).toString(),64);
            if (m_operation!="status") valid=valid && recovery.value(key)==response.value("review").toObject().value("details").toObject().value(key);
        }
    } else if (valid) valid=response.value("recovery").isNull() && m_operation!="submit";
    if (!valid) {m_state="unknown";m_review={};m_recovery={};m_checked=false;}
    else {
        m_checked=true;m_recovery=recovery;
        if (!recovery.isEmpty()) m_state="saved";
        else if (m_operation=="review") {m_review=response.value("review").toObject();m_state="reviewed";}
        else m_state="not-saved";
    }
    m_output.clear();m_input.clear();m_approved={};emit changed();
}
