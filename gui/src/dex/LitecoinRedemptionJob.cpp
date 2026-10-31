#include "DexDeployment.h"
#include "LitecoinRedemptionJob.h"
#include "LitecoinJournalJob.h"
#include "LitecoinJournalProtocol.h"
#include "DexUtils.h"
#include <QJsonDocument>
#include <QPointer>
#include <QProcessEnvironment>
#include <QUuid>
#include <limits>

namespace {
bool hashValid(const QJsonValue &value) {
    static const QRegularExpression pattern("\\A[0-9a-f]{64}\\z");
    return value.isString() && pattern.match(value.toString()).hasMatch();
}
bool integer(const QJsonValue &value,quint64 &number) {
    bool ok=false;number=value.toString().toULongLong(&ok);
    return value.isString() && ok && QString::number(number)==value.toString();
}
}
LitecoinRedemptionJob::LitecoinRedemptionJob(QObject *parent):QObject(parent) {
    m_deadline.setSingleShot(true);
    connect(&m_deadline,&QTimer::timeout,this,[this]{finish(false);});
    connect(&m_process,&QProcess::started,this,[this]{
        if (m_process.write(m_input)!=m_input.size()) {finish(false);return;}
        m_process.closeWriteChannel();m_input.clear();
    });
    connect(&m_process,&QProcess::readyReadStandardOutput,this,&LitecoinRedemptionJob::drain);
    connect(&m_process,&QProcess::readyReadStandardError,this,&LitecoinRedemptionJob::drain);
    connect(&m_process,&QProcess::errorOccurred,this,[this](QProcess::ProcessError){finish(false);});
    connect(&m_process,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[this](int code,QProcess::ExitStatus exit){
        QPointer<LitecoinRedemptionJob> guard(this);const auto request=m_request;
        drain();if (guard && request==m_request) finish(code==0 && exit==QProcess::NormalExit && m_errors==0);
    });
}
LitecoinRedemptionJob::~LitecoinRedemptionJob() {
    disconnect(&m_process,nullptr,this,nullptr);
    if (m_process.state()!=QProcess::NotRunning) {m_process.kill();m_process.waitForFinished(1000);}
}
bool LitecoinRedemptionJob::configure(const QString &path) {
    if (!m_path.isEmpty() || m_busy) return false;
    LitecoinJournalJob validator;
    if (!validator.configure(path)) return false;
    const auto bytes=LitecoinJournalProtocol::privateConfig(path);
    const auto c=QJsonDocument::fromJson(bytes).object(),policy=c.value("policy").toObject();
    const auto rpcPath=c.value("fundingRpcFile").toString();const auto rpc=LitecoinJournalProtocol::privateConfig(rpcPath);
    const auto minimum=policy.value("ltc_confirmations").toDouble(-1);
    if (c.value("schema")!="monzero-private-ltc-journal-v4" || c.value("role")!="native-funder"
        || !DexDeployment::litecoinNetworkAllowed(policy.value("ltc_network").toString()) || rpc.isEmpty() || !hashValid(policy.value("xmz_genesis"))
        || minimum<1 || minimum>std::numeric_limits<quint32>::max() || minimum!=quint32(minimum)) return false;
    m_sources={{path,bytes},{rpcPath,rpc}};m_program=c.value("helper").toString();m_digest=c.value("helperDigest").toString();
    m_trade=c.value("tradeId").toString();m_genesis=policy.value("xmz_genesis").toString();m_minimum=quint64(minimum);
    m_path=path;m_state="not-reviewed";emit changed();return true;
}
bool LitecoinRedemptionJob::sourcesMatch() const {
    if (m_path.isEmpty() || !BackendExecutable::matches(m_program,m_digest)) return false;
    for (auto it=m_sources.cbegin();it!=m_sources.cend();++it)
        if (LitecoinJournalProtocol::privateConfig(it.key())!=it.value()) return false;
    return true;
}
QVariantMap LitecoinRedemptionJob::status() const {
    return {{"configured",!m_path.isEmpty()},{"tradeId",m_trade},{"busy",m_busy},{"state",m_state},
        {"review",m_review},{"submission",m_submission},{"observation",m_observation},{"checkState",m_checkState},
        {"reviewAvailable",!m_path.isEmpty() && !m_busy},{"checkAvailable",!m_path.isEmpty() && !m_busy},
        {"submissionAvailable",!m_busy && m_state=="reviewed" && !m_review.isEmpty()},{"executable",false}};
}
bool LitecoinRedemptionJob::review() {return run("review");}
bool LitecoinRedemptionJob::check() {return run("check");}
bool LitecoinRedemptionJob::submit(const QString &commitment) {
    if (m_busy || m_state!="reviewed" || m_review.isEmpty() || commitment!=m_review.value("commitment").toString()) return false;
    return run("submit",commitment);
}
bool LitecoinRedemptionJob::run(const QString &operation,const QString &commitment) {
    if (m_path.isEmpty() || m_busy || m_process.state()!=QProcess::NotRunning) return false;
    m_pending=operation=="submit"?m_review.value("details").toMap():QVariantMap{};
    m_review.clear();m_submission.clear();m_observation.clear();m_checkState="not-checked";
    if (!sourcesMatch()) {m_state="unknown";m_checkState="unknown";m_pending.clear();emit changed();return false;}
    m_operation=operation;m_state=operation=="review"?"reviewing":operation=="submit"?"submitting":"checking";
    if (operation=="check") m_checkState="checking";
    m_request=QUuid::createUuid().toString(QUuid::WithoutBraces);
    QJsonObject request{{"schema","monzero-ltc-redemption-"+operation+"-request-v1"},{"requestId",m_request}};
    if (operation=="submit") request.insert("commitment",commitment);
    m_input=QJsonDocument(request).toJson(QJsonDocument::Compact);m_output.clear();m_errors=0;
    m_busy=true;QProcessEnvironment environment;environment.insert("LANG","C.UTF-8");
    // A seller using light servers verifies them against the platform trust store.
    const auto system=QProcessEnvironment::systemEnvironment();
    for (const auto name:{"SSL_CERT_FILE","SSL_CERT_DIR"}) if (system.contains(name)) environment.insert(name,system.value(name));
    m_process.setProcessEnvironment(environment);m_process.setWorkingDirectory("/");
    QPointer<LitecoinRedemptionJob> guard(this);emit changed();if (!guard) return false;
    m_deadline.start(180000);m_process.start(m_program,{"--redemption-"+operation+"-stdio-v1","--config-file",m_path});return true;
}
void LitecoinRedemptionJob::drain() {
    if (!m_busy) return;
    m_output+=m_process.readAllStandardOutput();m_errors+=m_process.readAllStandardError().size();
    if (m_output.size()>8192 || m_errors) finish(false);
}
bool LitecoinRedemptionJob::reviewValid(const QJsonObject &review,const QJsonObject &snapshot) const {
    const auto d=review.value("details").toObject();
    const QStringList strings{"schema","tradeId","quoteCommitment","journalCommitment","completedCommitment","transactionCommitment",
        "action","txid","wtxid","inputTxid","inputVout","sequence","destination","inputAtoms","amountAtoms","feeAtoms"};
    if (review.size()!=2 || !hashValid(review.value("commitment")) || d.size()!=strings.size()+2) return false;
    for (const auto &key:strings) if (!d.value(key).isString() || d.value(key).toString().isEmpty()) return false;
    for (const auto key:{"quoteCommitment","journalCommitment","completedCommitment","transactionCommitment","txid","wtxid","inputTxid"})
        if (!hashValid(d.value(key))) return false;
    const bool retry=d.value("action")=="retransmit";const auto payment=snapshot.value("litecoinRedemption").toObject();
    if (d.value("schema")!="monzero-ltc-redemption-review-v1" || d.value("tradeId")!=m_trade
        || d.value("quoteCommitment")!=snapshot.value("quoteCommitment") || d.value("destination")!=snapshot.value("ltcPayout")
        || d.value("inputAtoms")!=snapshot.value("ltcAtoms") || d.value("chainObserved")!=false
        || (!retry && d.value("action")!="send") || payment.value("attempted").toBool()!=retry
        || (retry?!hashValid(d.value("dispatchCommitment")):!d.value("dispatchCommitment").isNull())) return false;
    quint64 input,amount,fee,vout,sequence;
    if (!integer(d.value("inputAtoms"),input) || !integer(d.value("amountAtoms"),amount) || !integer(d.value("feeAtoms"),fee)
        || !integer(d.value("inputVout"),vout) || !integer(d.value("sequence"),sequence) || !amount || input<amount || input-amount!=fee
        || vout>std::numeric_limits<quint32>::max() || sequence>std::numeric_limits<quint32>::max()) return false;
    if (!LitecoinJournalProtocol::witnessAddress(d.value("destination").toString(),snapshot.value("ltcNetwork").toString(),
            "[qp][023456789acdefghjklmnpqrstuvwxyz]{38}(?:[023456789acdefghjklmnpqrstuvwxyz]{20})?")) return false;
    QByteArray binding("MONZERO-LTC-REDEMPTION-APPROVAL-V1");binding.append('\0');binding+=QJsonDocument(d).toJson(QJsonDocument::Compact);
    return review.value("commitment").toString().toLatin1()==QCryptographicHash::hash(binding,QCryptographicHash::Sha256).toHex();
}
void LitecoinRedemptionJob::finish(bool success) {
    if (!m_busy) return;
    m_busy=false;m_deadline.stop();m_state="unknown";m_review.clear();m_submission.clear();
    if (m_process.state()!=QProcess::NotRunning) m_process.kill();
    const auto doc=QJsonDocument::fromJson(m_output);const auto r=doc.object(),s=r.value("snapshot").toObject();
    const bool valid=success && sourcesMatch() && doc.isObject() && doc.toJson(QJsonDocument::Compact)==m_output && r.size()==4
        && r.value("schema")=="monzero-ltc-redemption-"+m_operation+"-response-v1" && r.value("requestId")==m_request
        && LitecoinJournalProtocol::snapshotValid(s,m_trade,"native-funder") && DexDeployment::litecoinNetworkAllowed(s.value("ltcNetwork").toString())
        && s.value("xmzGenesis")==m_genesis && s.value("keysReady")==true && s.value("recoveryIntentRetained")==true
        && s.value("nativeFunding").toObject().value("attempted")==true
        && s.value("litecoinRedemption").toObject().value("prepared")==true;
    if (m_operation=="review" && valid && reviewValid(r.value("review").toObject(),s)) {
        m_review=r.value("review").toObject().toVariantMap();m_state="reviewed";QVariantMap amounts;
        const auto details=m_review.value("details").toMap();
        for (const auto key:{"inputAtoms","amountAtoms","feeAtoms"}) amounts.insert(key,DexUtils::formatAtomic(details.value(key).toString().toULongLong(),8));
        m_review.insert("amounts",amounts);
    } else if (m_operation=="submit" && valid) {
        const auto o=r.value("outcome").toObject(),payment=s.value("litecoinRedemption").toObject();const auto state=o.value("state").toString();
        if (o.size()==2 && o.value("txid")==QJsonValue::fromVariant(m_pending.value("txid"))
            && QStringList{"acknowledged","uncertain","mempool"}.contains(state) && payment.value("attempted")==true
            && (state!="acknowledged" || payment.value("acknowledged")==true)
            && s.value("quoteCommitment")==QJsonValue::fromVariant(m_pending.value("quoteCommitment"))) {
            m_submission=o.toVariantMap();m_state=state;
        }
    } else if (m_operation=="check") {
        m_checkState="unknown";const auto o=r.value("observation").toObject();quint64 depth=0,required=0;
        bool observed=valid && o.size()==10 && o.value("schema")=="monzero-ltc-redemption-confirmation-v1"
            && o.value("tradeId")==m_trade && o.value("quoteCommitment")==s.value("quoteCommitment") && o.value("chainObserved")==true
            && integer(o.value("confirmations"),depth) && integer(o.value("requiredConfirmations"),required) && required>=m_minimum && depth>=required;
        for (const auto key:{"txid","wtxid","block","tip"}) observed=observed && hashValid(o.value(key));
        if (observed) {m_observation=o.toVariantMap();m_checkState="confirmed";m_state="not-reviewed";}
    }
    m_pending.clear();m_output.clear();m_input.clear();emit changed();
}
