#include "BchRefundProtectionJob.h"
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

BchRefundProtectionJob::BchRefundProtectionJob(QObject *parent):QObject(parent) {
    m_deadline.setSingleShot(true);
    connect(&m_deadline,&QTimer::timeout,this,[this]{finish(false);});
    connect(&m_process,&QProcess::started,this,[this]{
        if (!m_busy || m_process.write(m_input)!=m_input.size()) {finish(false);return;}
        m_process.closeWriteChannel();m_input.clear();
    });
    connect(&m_process,&QProcess::readyReadStandardOutput,this,&BchRefundProtectionJob::drain);
    connect(&m_process,&QProcess::readyReadStandardError,this,&BchRefundProtectionJob::drain);
    connect(&m_process,&QProcess::errorOccurred,this,[this](QProcess::ProcessError){finish(false);});
    connect(&m_process,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,
        [this](int code,QProcess::ExitStatus status){
            QPointer<BchRefundProtectionJob> guard(this);const auto request=m_request;
            drain();if (guard && request==m_request) finish(code==0 && status==QProcess::NormalExit);
        });
}
BchRefundProtectionJob::~BchRefundProtectionJob() {
    disconnect(&m_process,nullptr,this,nullptr);
    if (m_process.state()!=QProcess::NotRunning) {m_process.kill();m_process.waitForFinished(1000);}
}
namespace {
QString boundDigest(const QByteArray &domain,const QByteArray &bytes) {
    QByteArray value=domain;value.append('\0');value+=bytes;
    return QString::fromLatin1(QCryptographicHash::hash(value,QCryptographicHash::Sha256).toHex());
}
bool byteArray(const QJsonValue &value,int minimum,int maximum,QByteArray *out) {
    if (!value.isArray() || value.toArray().size()<minimum || value.toArray().size()>maximum) return false;
    out->clear();
    for (const auto item:value.toArray()) {
        quint64 byte=0;if (!integer(item,255,&byte)) return false;out->append(char(byte));
    }
    return true;
}
bool decimalTime(const QJsonValue &value,quint64 *out) {
    if (!value.isString()) return false;
    bool ok=false;const auto n=value.toString().toULongLong(&ok);
    if (!ok || QString::number(n)!=value.toString()) return false;
    *out=n;return true;
}
}
bool BchRefundProtectionJob::configure(const QString &path) {
    if (!m_path.isEmpty() || m_busy) return false;
    const auto bytes=LitecoinJournalProtocol::privateConfig(path);QJsonObject wrapper,config,record,admission;
    if (!canonicalObject(bytes,&wrapper) || wrapper.size()!=3
        || wrapper.value("schema")!="monzero-private-bch-refund-protection-v1") return false;
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
    quint64 admitted=0,expires=0;
    if (!integer(config.value("admittedAt"),9007199254740991ULL,&admitted)
        || !decimalTime(record.value("terms").toObject().value("expiresAt"),&expires) || expires<=admitted) return false;
    m_sources={{path,bytes},{admissionPath,configBytes},{quotePath,quoteBytes}};
    m_path=path;m_messagePath=messagePath;m_program=config.value("helper").toString();m_digest=config.value("helperDigest").toString();
    m_trade=config.value("tradeId").toString();m_quote=config.value("quoteCommitment").toString();
    m_peer=record.value("terms").toObject().value("xmzFunder").toString();m_admitted=admitted;m_expires=expires;
    m_sourcesValid=true;m_state="ready";emit changed();return true;
}
bool BchRefundProtectionJob::sourcesMatch() const {
    if (!m_sourcesValid || !BackendExecutable::matches(m_program,m_digest)) return false;
    for (auto i=m_sources.cbegin();i!=m_sources.cend();++i)
        if (LitecoinJournalProtocol::privateConfig(i.key())!=i.value()) return false;
    return true;
}
bool BchRefundProtectionJob::readMessage() {
    const auto bytes=LitecoinJournalProtocol::privateConfig(m_messagePath,65536);
    QJsonObject message,frame;QByteArray raw,signature;
    if (!canonicalObject(bytes,&message) || message.size()!=3
        || message.value("schema")!="monzero-bch-peer-refund-protection-v1"
        || !byteArray(message.value("frame"),1,8192,&raw) || !byteArray(message.value("signature"),64,64,&signature)
        || !canonicalObject(raw,&frame) || frame.size()!=5
        || frame.value("schema")!="monzero-bch-refund-adaptor-v1"
        || frame.value("swap")!=m_trade || pin(frame.value("quote"))!=m_quote
        || pin(frame.value("agreement")).isEmpty()) return false;
    m_message=bytes;
    m_expected={{"schema","monzero-bch-refund-protection-review-v1"},{"action","accept-peer-refund-protection-only"},
        {"swapId",m_trade},{"quote",m_quote},{"agreement",frame.value("agreement")},{"peerIdentity",m_peer},
        {"frame",boundDigest("monzero-bch-refund-frame-v1",raw)},
        {"signature",boundDigest("monzero-bch-refund-identity-signature-v1",signature)}};
    return true;
}
bool BchRefundProtectionJob::validReview(const QJsonObject &review) const {
    if (review.size()!=2 || !review.value("details").isObject()) return false;
    auto details=review.value("details").toObject();
    if (details.size()!=9 || !hex(details.value("admission").toString(),64)
        || review.value("commitment")!=boundDigest("monzero-bch-refund-protection-review-v1",
            QJsonDocument(details).toJson(QJsonDocument::Compact))) return false;
    details.remove("admission");return details==m_expected;
}
QVariantMap BchRefundProtectionJob::status() const {
    const bool available=!m_path.isEmpty() && m_sourcesValid && !m_busy;
    return {{"configured",!m_path.isEmpty()},{"busy",m_busy},{"state",m_state},{"tradeId",m_trade},
        {"quoteCommitment",m_quote},{"peerIdentity",m_peer},{"review",m_review.toVariantMap()},
        {"receipt",m_receipt.toVariantMap()},{"checkAvailable",available},{"checked",m_checked},
        {"reviewAvailable",available && m_checked && !m_submitAttempted && m_receipt.isEmpty()},
        {"submitAvailable",available && m_checked && !m_submitAttempted && !m_review.isEmpty() && m_receipt.isEmpty()},
        {"submitAttempted",m_submitAttempted},{"executable",false},{"chainObserved",false}};
}
bool BchRefundProtectionJob::review() {
    return status().value("reviewAvailable").toBool() && run("review");
}
bool BchRefundProtectionJob::submit(const QString &commitment) {
    return status().value("submitAvailable").toBool() && commitment==m_review.value("commitment").toString()
        && run("submit",commitment);
}
bool BchRefundProtectionJob::refresh() {return run("status");}
bool BchRefundProtectionJob::run(const QString &operation,const QString &commitment) {
    if (m_busy || m_path.isEmpty() || m_process.state()!=QProcess::NotRunning) return false;
    const bool same=sourcesMatch();
    const bool messageOk=operation=="status" || (operation=="review"?readMessage():
        !m_message.isEmpty() && LitecoinJournalProtocol::privateConfig(m_messagePath,65536)==m_message);
    if (!same || !messageOk) {
        m_sourcesValid=same;m_review={};m_receipt={};m_checked=false;m_state="unknown";emit changed();return false;
    }
    m_request=QUuid::createUuid().toString(QUuid::WithoutBraces);m_operation=operation;
    QJsonObject request{{"schema","monzero-bch-refund-protection-"+operation+"-request-v1"},{"requestId",m_request}};
    m_approved={};
    if (operation=="submit") {request["commitment"]=commitment;m_approved=m_review;m_submitAttempted=true;}
    m_input=QJsonDocument(request).toJson(QJsonDocument::Compact);
    m_output.clear();m_errors=0;m_review={};m_receipt={};m_busy=true;
    m_state=operation=="review"?"reviewing":operation=="submit"?"submitting":"checking";
    QProcessEnvironment environment;environment.insert("LANG","C");
    m_process.setProcessEnvironment(environment);m_process.setProcessChannelMode(QProcess::SeparateChannels);
    m_deadline.start(180000);
    m_process.start(m_program,{"--refund-protection-"+operation+"-stdio-v1",m_path});emit changed();return true;
}
void BchRefundProtectionJob::drain() {
    if (!m_busy) return;
    m_process.setReadChannel(QProcess::StandardOutput);m_output+=m_process.read(16385-m_output.size());
    m_process.setReadChannel(QProcess::StandardError);m_errors+=m_process.read(1025-m_errors).size();
    if (m_output.size()>16384 || m_errors>1024) finish(false);
}
void BchRefundProtectionJob::finish(bool success) {
    if (!m_busy) return;
    m_busy=false;m_deadline.stop();
    if (m_process.state()!=QProcess::NotRunning) m_process.kill();
    QJsonObject response;const bool same=sourcesMatch();m_sourcesValid=same;
    bool valid=success && same && m_errors==0 && canonicalObject(m_output,&response)
        && response.size()==7 && response.value("schema")=="monzero-bch-refund-protection-"+m_operation+"-response-v1"
        && response.value("requestId")==m_request && response.value("tradeId")==m_trade
        && response.value("executable").isBool() && !response.value("executable").toBool()
        && response.value("chainObserved").isBool() && !response.value("chainObserved").toBool();
    if (valid && m_operation=="status") valid=response.value("review").isNull();
    else if (valid) valid=LitecoinJournalProtocol::privateConfig(m_messagePath,65536)==m_message
        && response.value("review").isObject() && validReview(response.value("review").toObject())
        && (m_operation!="submit" || response.value("review").toObject()==m_approved);
    QJsonObject receipt;
    if (valid && response.value("receipt").isObject()) {
        receipt=response.value("receipt").toObject();quint64 received=0;
        valid=receipt.size()==2 && hex(receipt.value("commitment").toString(),64)
            && decimalTime(receipt.value("receivedAt"),&received) && received>=m_admitted && received<m_expires
            && received<=quint64(QDateTime::currentSecsSinceEpoch());
    } else if (valid) valid=response.value("receipt").isNull() && m_operation!="submit";
    if (!valid) {m_state="unknown";m_review={};m_receipt={};m_checked=false;}
    else {
        m_checked=true;m_receipt=receipt;
        if (!receipt.isEmpty()) m_state="saved";
        else if (m_operation=="review") {m_review=response.value("review").toObject();m_state="reviewed";}
        else m_state="not-saved";
    }
    m_output.clear();m_input.clear();m_approved={};emit changed();
}
