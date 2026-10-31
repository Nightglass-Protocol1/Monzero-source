#include "BchFundingJob.h"
#include "BchAdmissionProtocol.h"
#include "LitecoinJournalProtocol.h"
#include <QDateTime>
#include <QPointer>
#include <QProcessEnvironment>
#include <QRegularExpression>
using namespace BchAdmissionProtocol;

BchFundingJob::BchFundingJob(QObject *parent):QObject(parent) {
    m_deadline.setSingleShot(true);
    connect(&m_deadline,&QTimer::timeout,this,[this]{finish(false);});
    connect(&m_process,&QProcess::started,this,[this]{
        if (!m_busy || m_process.write(m_input)!=m_input.size()) {finish(false);return;}
        m_process.closeWriteChannel();m_input.clear();
    });
    connect(&m_process,&QProcess::readyReadStandardOutput,this,&BchFundingJob::drain);
    connect(&m_process,&QProcess::readyReadStandardError,this,&BchFundingJob::drain);
    connect(&m_process,&QProcess::errorOccurred,this,[this](QProcess::ProcessError){finish(false);});
    connect(&m_process,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,
        [this](int code,QProcess::ExitStatus status){
            QPointer<BchFundingJob> guard(this);const auto request=m_request;
            drain();if (guard && request==m_request) finish(code==0 && status==QProcess::NormalExit);
        });
}
BchFundingJob::~BchFundingJob() {
    disconnect(&m_process,nullptr,this,nullptr);
    if (m_process.state()!=QProcess::NotRunning) {m_process.kill();m_process.waitForFinished(1000);}
}
bool BchFundingJob::configure(const QString &path) {
    if (!m_path.isEmpty() || m_busy) return false;
    QJsonObject wrapper,config,record,admission;
    const auto bytes=LitecoinJournalProtocol::privateConfig(path);
    if (!canonicalObject(bytes,&wrapper) || wrapper.size()!=3
        || wrapper.value("schema")!="monzero-private-bch-funding-v1") return false;
    const auto deployment=wrapper.value("deploymentCommitment").toString();
    if (!hex(deployment,64) || deployment==QString(64,'0')) return false;
    const auto admissionPath=wrapper.value("admissionConfigFile").toString();
    const auto admissionBytes=LitecoinJournalProtocol::privateConfig(admissionPath);
    if (!canonicalObject(admissionBytes,&config) || config.value("role")!="bch-funder") return false;
    const auto quotePath=config.value("quoteFile").toString();
    const auto quoteBytes=LitecoinJournalProtocol::privateConfig(quotePath);
    if (!canonicalObject(quoteBytes,&record) || !expectedReview(config,record,&admission)
        || !BackendExecutable::matches(config.value("helper").toString(),config.value("helperDigest").toString())) return false;
    m_path=path;m_wrapperBytes=bytes;m_admissionPath=admissionPath;m_admissionBytes=admissionBytes;
    m_quotePath=quotePath;m_quoteBytes=quoteBytes;m_deployment=deployment;
    m_program=config.value("helper").toString();m_digest=config.value("helperDigest").toString();
    m_trade=config.value("tradeId").toString();m_expectedAdmission=admission;
    m_sourcesValid=true;m_state="ready";emit changed();return true;
}
bool BchFundingJob::configureSubmission(const QString &path) {
    if (!m_submissionPath.isEmpty() || m_busy || m_submitAttempted) return false;
    QJsonObject wrapper,rpc;
    const auto bytes=LitecoinJournalProtocol::privateConfig(path);
    if (!canonicalObject(bytes,&wrapper) || wrapper.size()!=3
        || wrapper.value("schema")!="monzero-private-bch-funding-submit-v1") return false;
    const auto fundingPath=wrapper.value("fundingConfigFile").toString();
    const auto rpcPath=wrapper.value("rpcFile").toString();
    const auto rpcBytes=LitecoinJournalProtocol::privateConfig(rpcPath);
    if (!canonicalObject(rpcBytes,&rpc) || rpc.size()!=9
        || rpc.value("schema")!="monzero-private-bch-funding-rpc-v1") return false;
    quint64 port=0,height=0;
    const auto wallet=rpc.value("wallet").toString(),anchor=rpc.value("anchor").toString();
    const auto script=rpc.value("anchorScript").toString(),checkpoint=rpc.value("checkpoint").toString();
    const auto cookie=rpc.value("cookieFile").toString(),version=rpc.value("subversion").toString();
    if (!integer(rpc.value("port"),65535,&port) || !port
        || !integer(rpc.value("checkpointHeight"),2147483647,&height) || !height
        || !QRegularExpression("\\A[a-zA-Z0-9_-]{1,64}\\z").match(wallet).hasMatch()
        || !QRegularExpression("\\Abchreg:[a-z0-9]{1,200}\\z").match(anchor).hasMatch()
        || script.isEmpty() || script.size()>70 || script.size()%2 || !hex(script,script.size())
        || !hex(checkpoint,64) || checkpoint==QString(64,'0')
        || !QDir::isAbsolutePath(cookie) || QDir::cleanPath(cookie)!=cookie
        || !version.startsWith("/Bitcoin Cash Node:") || !version.endsWith('/') || version.size()>128) return false;
    for (const auto c:version) if (c.unicode()<32 || c.unicode()>126) return false;
    if (m_path.isEmpty()) {if (!configure(fundingPath)) return false;}
    else if (fundingPath!=m_path || !sourcesMatch()) return false;
    const QString genesis="0f9188f13cb7b2c71f2a335e3a4fc328bf5beb436012afca590b1a11466e2206";
    const auto terms=m_expectedAdmission.value("details").toObject().value("terms").toObject();
    const QJsonObject deployment{{"schema","monzero-bch-wallet-deployment-v1"},{"host","127.0.0.1"},
        {"port",rpc.value("port")},{"network","bch-regtest"},{"genesis",genesis},
        {"checkpointHeight",rpc.value("checkpointHeight")},{"checkpoint",checkpoint},
        {"wallet",wallet},{"anchor",anchor},{"script",script},{"subversion",version}};
    const auto digest=QString::fromLatin1(QCryptographicHash::hash(
        QJsonDocument(deployment).toJson(QJsonDocument::Compact),QCryptographicHash::Sha256).toHex());
    if (terms.value("bchGenesis")!=genesis || digest!=m_deployment) return false;
    m_submissionPath=path;m_submissionBytes=bytes;m_rpcPath=rpcPath;m_rpcBytes=rpcBytes;
    m_submissionSourcesValid=true;m_review={};m_savedReview={};m_result={};m_statusKnown=false;m_relayState.clear();
    emit changed();return true;
}
bool BchFundingJob::submissionSourcesMatch() const {
    return m_submissionSourcesValid && !m_submissionPath.isEmpty()
        && LitecoinJournalProtocol::privateConfig(m_submissionPath)==m_submissionBytes
        && LitecoinJournalProtocol::privateConfig(m_rpcPath)==m_rpcBytes;
}
bool BchFundingJob::sourcesMatch() const {
    return m_sourcesValid && BackendExecutable::matches(m_program,m_digest)
        && LitecoinJournalProtocol::privateConfig(m_path)==m_wrapperBytes
        && LitecoinJournalProtocol::privateConfig(m_admissionPath)==m_admissionBytes
        && LitecoinJournalProtocol::privateConfig(m_quotePath)==m_quoteBytes;
}
QVariantMap BchFundingJob::status() const {
    const bool available=!m_path.isEmpty() && m_sourcesValid && !m_busy;
    const bool unsent=!m_chainSeen && !m_submitAttempted && (!m_statusKnown || m_relayState=="not-attempted");
    return {{"configured",!m_path.isEmpty()},{"busy",m_busy},{"state",m_state},{"tradeId",m_trade},
        {"review",m_review.toVariantMap()},{"savedReview",m_savedReview.toVariantMap()},
        {"result",m_result.toVariantMap()},{"relayState",m_relayState},{"statusKnown",m_statusKnown},
        {"reviewAvailable",available && unsent},{"checkAvailable",available},
        {"submissionConfigured",!m_submissionPath.isEmpty()},{"submitAttempted",m_submitAttempted},
        {"submitAvailable",available && unsent && m_statusKnown && m_relayState=="not-attempted"
            && m_submissionSourcesValid && !m_review.isEmpty() && validReview(m_review,true)},
        {"observeAvailable",available && m_submissionSourcesValid && !m_submissionPath.isEmpty()},
        {"observation",m_observation.toVariantMap()},{"chainObserved",!m_observation.isEmpty()},{"executable",false}};
}
bool BchFundingJob::review() {
    if (!status().value("reviewAvailable").toBool()) return false;
    return run("review");
}
bool BchFundingJob::refresh() {return run("status");}
bool BchFundingJob::observe() {
    if (!status().value("observeAvailable").toBool()) return false;
    return run("observe");
}
bool BchFundingJob::submit(const QString &commitment) {
    if (!status().value("submitAvailable").toBool() || commitment!=m_review.value("commitment").toString()) return false;
    return run("submit",commitment);
}
bool BchFundingJob::run(const QString &operation,const QString &commitment) {
    if (m_busy || m_path.isEmpty() || m_process.state()!=QProcess::NotRunning) return false;
    if (!sourcesMatch()) {
        m_observation={};m_sourcesValid=false;m_review={};m_savedReview={};m_result={};m_statusKnown=false;
        m_relayState.clear();m_state="unknown";emit changed();return false;
    }
    if ((operation=="submit" || operation=="observe") && !submissionSourcesMatch()) {
        m_observation={};m_submissionSourcesValid=false;m_review={};m_savedReview={};m_result={};
        m_statusKnown=false;m_relayState.clear();m_state="unknown";emit changed();return false;
    }
    m_request=QUuid::createUuid().toString(QUuid::WithoutBraces);m_operation=operation;
    QJsonObject request{{"schema","monzero-bch-funding-"+operation+"-request-v1"},{"requestId",m_request}};
    m_approvedReview={};
    if (operation=="submit") {request["commitment"]=commitment;m_submitAttempted=true;m_approvedReview=m_review;}
    m_input=QJsonDocument(request).toJson(QJsonDocument::Compact);
    m_startedAt=QDateTime::currentSecsSinceEpoch();m_observation={};
    m_output.clear();m_errors=0;m_review={};m_savedReview={};m_result={};m_busy=true;
    m_state=operation=="observe"?"observing":operation=="submit"?"submitting":operation=="status"?"checking":"reviewing";
    QProcessEnvironment environment;environment.insert("LANG","C");
    m_process.setProcessEnvironment(environment);m_process.setProcessChannelMode(QProcess::SeparateChannels);
    m_deadline.start(180000);
    m_process.start(m_program,{"--funding-"+operation+"-stdio-v1",(operation=="submit" || operation=="observe")?m_submissionPath:m_path});
    emit changed();return true;
}
bool BchFundingJob::validReview(const QJsonObject &review,bool fresh) const {
    if (review.size()!=2 || !review.value("details").isObject()
        || !hex(review.value("commitment").toString(),64)) return false;
    const auto details=review.value("details").toObject();
    const auto admission=m_expectedAdmission.value("details").toObject();
    const auto terms=admission.value("terms").toObject();
    if (details.size()!=19 || details.value("schema")!="monzero-bch-funding-relay-review-v1"
        || details.value("action")!="broadcast-funding" || details.value("swapId")!=m_trade
        || details.value("network")!=terms.value("bchNetwork") || details.value("genesis")!=terms.value("bchGenesis")
        || details.value("expiresAt")!=terms.value("expiresAt")
        || pin(details.value("quote"))!=admission.value("quoteCommitment").toString()
        || pin(details.value("deployment"))!=m_deployment) return false;
    for (const auto key:{"admission","signedBytes","preparation","privateRecovery"})
        if (pin(details.value(key)).isEmpty()) return false;
    for (const auto key:{"fundingTxid","cancelTxid","refundTxid","punishTxid"})
        if (!hex(details.value(key).toString(),64) || details.value(key).toString()==QString(64,'0')) return false;
    bool valid=false;const auto expiry=terms.value("expiresAt").toString().toULongLong(&valid);
    if (!valid || (fresh && expiry<=quint64(QDateTime::currentSecsSinceEpoch()))) return false;
    const auto cap=terms.value("bchFundingFeeMaxAtoms").toString().toULongLong(&valid);
    quint64 fee=0,vout=0;
    if (!valid || !integer(details.value("feeAtoms"),2100000000000000ULL,&fee) || !fee || fee>cap
        || !integer(details.value("contractVout"),1,&vout) || !details.value("outputs").isArray()) return false;
    const auto outputs=details.value("outputs").toArray();
    if (outputs.isEmpty() || outputs.size()>2 || vout>=quint64(outputs.size())) return false;
    quint64 total=fee;
    for (const auto &value:outputs) {
        const auto output=value.toObject();quint64 atoms=0;
        const auto script=output.value("script").toString();
        if (output.size()!=2 || !integer(output.value("atoms"),2100000000000000ULL,&atoms) || !atoms
            || script.isEmpty() || script.size()>10000 || script.size()%2 || !hex(script,script.size())) return false;
        total+=atoms;if (total>2100000000000000ULL) return false;
    }
    const auto principal=terms.value("bchAtoms").toString().toULongLong(&valid);
    if (!valid || quint64(outputs[int(vout)].toObject().value("atoms").toDouble())!=principal) return false;
    QByteArray binding("monzero-bch-funding-relay-review-v1");binding.append('\0');
    binding+=QJsonDocument(details).toJson(QJsonDocument::Compact);
    return QString::fromLatin1(QCryptographicHash::hash(binding,QCryptographicHash::Sha256).toHex())
        ==review.value("commitment").toString();
}
bool BchFundingJob::validObservation(const QJsonObject &observation,const QJsonObject &review) const {
    if (observation.size()!=6 || observation.value("txid")!=review.value("details").toObject().value("fundingTxid")) return false;
    const auto tip=observation.value("tip").toString();
    if (!hex(tip,64) || tip==QString(64,'0')) return false;
    bool valid=false;const auto time=observation.value("observedAt").toString();
    const auto seconds=time.toULongLong(&valid);
    if (!valid || QString::number(seconds)!=time || seconds<quint64(m_startedAt)
        || seconds>quint64(QDateTime::currentSecsSinceEpoch())) return false;
    const auto state=observation.value("state").toString();
    const auto depth=observation.value("confirmations"),unspent=observation.value("lockOutputUnspent");
    if (state=="confirmed-unspent") {
        const auto text=depth.toString();const auto confirmations=text.toULongLong(&valid);
        return valid && confirmations>0 && QString::number(confirmations)==text && unspent.isBool() && unspent.toBool();
    }
    if (state=="mempool") return depth.isNull() && unspent.isBool();
    return state=="unobserved" && depth.isNull() && unspent.isNull();
}
void BchFundingJob::drain() {
    if (!m_busy) return;
    m_process.setReadChannel(QProcess::StandardOutput);m_output+=m_process.read(16385-m_output.size());
    m_process.setReadChannel(QProcess::StandardError);m_errors+=m_process.read(1025-m_errors).size();
    if (m_output.size()>16384 || m_errors>1024) finish(false);
}
void BchFundingJob::finish(bool success) {
    if (!m_busy) return;
    m_busy=false;m_deadline.stop();
    if (m_process.state()!=QProcess::NotRunning) m_process.kill();
    QJsonObject response;
    const bool same=sourcesMatch();m_sourcesValid=same;
    const bool rpcSame=(m_operation!="submit" && m_operation!="observe") || submissionSourcesMatch();
    if (!rpcSame) m_submissionSourcesValid=false;
    bool valid=success && same && rpcSame && m_errors==0 && canonicalObject(m_output,&response)
        && response.size()==(m_operation=="review"?6:m_operation=="observe"?8:7)
        && response.value("schema")=="monzero-bch-funding-"+m_operation+"-response-v1"
        && response.value("requestId")==m_request && response.value("tradeId")==m_trade
        && response.value("executable").isBool() && !response.value("executable").toBool()
        && response.value("chainObserved").isBool() && response.value("chainObserved").toBool()==(m_operation=="observe")
        && response.value("review").isObject() && validReview(response.value("review").toObject(),m_operation=="review");
    QString relayState;
    if (valid && m_operation!="review") {
        const auto relay=response.value("relay").toObject();relayState=relay.value("state").toString();
        valid=relay.size()==3 && QStringList{"not-attempted","attempted","acknowledged"}.contains(relayState)
            && relay.value("confirmed").isBool() && !relay.value("confirmed").toBool()
            && relay.value("txid")==response.value("review").toObject().value("details").toObject().value("fundingTxid");
        if (m_operation=="submit") valid=valid && relayState=="acknowledged" && response.value("review").toObject()==m_approvedReview;
    }
    if (valid && m_operation=="observe") valid=response.value("observation").isObject()
        && validObservation(response.value("observation").toObject(),response.value("review").toObject());
    if (!valid) {
        m_observation={};m_review={};m_savedReview={};m_result={};m_statusKnown=false;m_relayState.clear();m_state="unknown";
    } else if (m_operation=="review") {m_review=response.value("review").toObject();m_state="reviewed";}
    else {
        m_savedReview=response.value("review").toObject();m_result=response;m_statusKnown=true;m_relayState=relayState;
        m_state=relayState=="not-attempted"?"checked":relayState;
        if (m_operation=="observe") {
            m_observation=response.value("observation").toObject();m_state="observed";
            if (m_observation.value("state")!="unobserved") m_chainSeen=true;
        }
    }
    m_output.clear();m_input.clear();m_approvedReview={};emit changed();
}
