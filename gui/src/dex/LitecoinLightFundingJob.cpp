#include "DexDeployment.h"
#include "LitecoinLightFundingJob.h"
#include "LitecoinJournalProtocol.h"
#include <QJsonDocument>
#include <QPointer>
#include <QProcessEnvironment>
#include <QUuid>
#include <cmath>
namespace {
void erase(QByteArray &bytes) {
    volatile char *p=bytes.data();for(int i=0;i<bytes.size();++i)p[i]=0;bytes.clear();
}
QString hash(const QByteArray &bytes) {return QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex());}
bool digest(const QJsonValue &v) {return v.isString() && QRegularExpression("\\A[0-9a-f]{64}\\z").match(v.toString()).hasMatch();}
bool uuid(const QJsonValue &v) {const QUuid id(v.toString());return !id.isNull() && id.toString(QUuid::WithoutBraces)==v.toString();}
bool number(const QJsonValue &v,int maximum) {return v.isDouble() && v.toDouble()>=1 && v.toDouble()<=maximum && std::floor(v.toDouble())==v.toDouble();}
bool atoms(const QJsonValue &v,bool positive=true) {
    bool ok=false;const auto n=v.toString().toULongLong(&ok);
    return v.isString() && ok && (!positive || n>0) && QString::number(n)==v.toString();
}
bool cold(const QString &op) {return QStringList{"observe","retry-review","retry"}.contains(op);}
void appendPassword(QByteArray &packet,const QByteArray &password) {
    const char hex[]="0123456789abcdef";packet.append('"');
    for(const auto byte:password) {
        const auto c=static_cast<unsigned char>(byte);
        if(c=='"' || c=='\\') {packet.append('\\');packet.append(char(c));}
        else if(c<32) {packet.append("\\u00");packet.append(hex[c>>4]);packet.append(hex[c&15]);}
        else packet.append(char(c));
    }
    packet.append('"');
}
}
LitecoinLightFundingJob::LitecoinLightFundingJob(QObject *parent):QObject(parent) {
    m_deadline.setSingleShot(true);
    connect(&m_deadline,&QTimer::timeout,this,[this]{finish(false);});
    connect(&m_process,&QProcess::started,this,[this]{
        if(m_process.write(m_input)!=m_input.size()) {finish(false);return;}
        m_process.closeWriteChannel();erase(m_input);
    });
    connect(&m_process,&QProcess::readyReadStandardOutput,this,&LitecoinLightFundingJob::drain);
    connect(&m_process,&QProcess::readyReadStandardError,this,&LitecoinLightFundingJob::drain);
    connect(&m_process,&QProcess::errorOccurred,this,[this](QProcess::ProcessError){finish(false);});
    connect(&m_process,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[this](int code,QProcess::ExitStatus exit){
        QPointer<LitecoinLightFundingJob> guard(this);const auto id=m_requestId;
        drain();if(guard && id==m_requestId)finish(code==0 && exit==QProcess::NormalExit && m_errors==0);
    });
}
LitecoinLightFundingJob::~LitecoinLightFundingJob() {
    disconnect(&m_process,nullptr,this,nullptr);erase(m_input);erase(m_output);
    if(m_process.state()!=QProcess::NotRunning){m_process.kill();m_process.waitForFinished(1000);}
    if(m_activeWallet)m_activeWallet->releaseFunding(this);
}
bool LitecoinLightFundingJob::configure(const QString &path,const QString &helper,const QString &trustedDigest) {
    if(!m_path.isEmpty() || m_busy || !BackendExecutable::matches(helper,trustedDigest))return false;
    const auto bytes=LitecoinJournalProtocol::privateConfig(path,4096);const auto c=QJsonDocument::fromJson(bytes).object();
    const auto walletPath=c.value("walletConfigFile").toString();
    const auto wallet=LitecoinJournalProtocol::privateConfig(walletPath,4096);const auto w=QJsonDocument::fromJson(wallet).object();
    if(c.size()!=7 || c.value("schema")!="monzero-private-ltc-light-dex-v1" || !uuid(c.value("tradeId"))
        || !DexDeployment::litecoinNetworkAllowed(c.value("policy").toObject().value("ltc_network").toString())
        || !c.value("pins").isArray() || !QFileInfo(c.value("metadataDirectory").toString()).isAbsolute()
        || !QFileInfo(c.value("snapshotDirectory").toString()).isAbsolute()
        || w.value("schema")!="monzero-private-"+DexDeployment::litecoinLightCoin()+"-light-v1" || w.value("network")!=DexDeployment::litecoinLightNetwork()
        || !w.value("wallet").isObject() || !w.value("endpoint").isObject())return false;
    m_context=hash(QJsonDocument(QJsonObject{{"schema","monzero-ltc-light-dex-context-v1"},
        {"dexDigest",hash(bytes)},{"walletDigest",hash(wallet)}}).toJson(QJsonDocument::Compact));
    if(m_walletBound) {
        m_contextDirectory=c.value("metadataDirectory").toString();
        if(!LitecoinLightFundingContext::access(m_contextDirectory,c.value("tradeId").toString(),m_context,m_retained))return false;
    }
    m_path=path;m_walletPath=walletPath;m_configuration=bytes;m_wallet=wallet;m_program=helper;m_digest=trustedDigest;
    m_trade=c.value("tradeId").toString();m_state="ready";emit changed();return true;
}
bool LitecoinLightFundingJob::configureWallet(const QString &path,LitecoinLightJob *wallet,const QString &helper,const QString &trustedDigest) {
    if(!wallet || wallet->thread()!=thread() || !m_path.isEmpty())return false;
    QString activePath,activeHelper,activeDigest;QByteArray bytes;
    const auto c=QJsonDocument::fromJson(LitecoinJournalProtocol::privateConfig(path,4096)).object();
    if(!wallet->fundingConfiguration(activePath,bytes,activeHelper,activeDigest,DexDeployment::litecoinLightCoin())
        || c.value("walletConfigFile")!=activePath || activeHelper!=helper || activeDigest!=trustedDigest)return false;
    // Set the source guard before configure() announces readiness.
    m_activeWallet=wallet;m_walletBound=true;
    if(!configure(path,helper,trustedDigest)){m_activeWallet.clear();m_walletBound=false;return false;}
    return true;
}
bool LitecoinLightFundingJob::sourcesMatch() const {
    if(m_walletBound) {
        QString path,helper,digest;QByteArray bytes;
        if(!m_activeWallet || !m_activeWallet->fundingConfiguration(path,bytes,helper,digest,DexDeployment::litecoinLightCoin())
            || path!=m_walletPath || bytes!=m_wallet || helper!=m_program || digest!=m_digest)return false;
    }
    return !m_path.isEmpty() && BackendExecutable::matches(m_program,m_digest)
        && LitecoinJournalProtocol::privateConfig(m_path,4096)==m_configuration
        && LitecoinJournalProtocol::privateConfig(m_walletPath,4096)==m_wallet;
}
QVariantMap LitecoinLightFundingJob::status() const {
    return {{"configured",!m_path.isEmpty()},{"busy",m_busy},{"state",m_state},{"tradeId",m_trade},
        {"configurationDigest",m_context},{"operation",m_operation},{"executable",false},
        {"retainedPayment",m_retained.payment.toVariantMap()},{"retainedReview",m_retained.review.toVariantMap()},{"retainedTxid",m_retained.txid}};
}
bool LitecoinLightFundingJob::run(const QString &operation,const QJsonObject &provided,const QByteArray &password) {
    auto request=provided;
    if(m_busy || m_process.state()!=QProcess::NotRunning || !sourcesMatch()
        || !QStringList{"terms","prepare","review","sign","send-review","send","observe","retry-review","retry"}.contains(operation))return false;
    if(operation=="terms") {
        if(request.size()!=3 || request.value("schema")!="monzero-ltc-light-dex-funding-terms-request-v1"
            || !uuid(request.value("requestId")) || request.value("approvedConfigDigest")!=m_context || !password.isEmpty())return false;
        return launch(operation,request,{});
    }
    if(m_walletBound) {
        if(!LitecoinLightFundingContext::access(m_contextDirectory,m_trade,m_context,m_retained))return false;
        if(!m_retained.payment.isEmpty() && request.value("payment")!=m_retained.payment)return false;
        if(!m_retained.review.isEmpty()) {
            if(request.contains("retainedReview") && request.value("retainedReview")!=m_retained.review)return false;
            request.insert("retainedReview",m_retained.review);
        } else if(operation!="prepare")return false;
    }
    const bool signing=operation=="sign",approved=signing || operation=="send" || operation=="retry";
    if(signing?(password.isEmpty() || password.size()>1024):!password.isEmpty())return false;
    QStringList allowed{"schema","requestId","approvedConfigDigest","maximumDownloadHeaders","payment","retainedReview"};
    allowed.append(cold(operation)?"maximumHistoryEntries":"walletScan");if(approved)allowed.append("approval");
    for(auto it=request.begin();it!=request.end();++it)if(!allowed.contains(it.key()))return false;
    if(request.value("schema")!="monzero-ltc-light-dex-funding-"+operation+"-request-v1"
        || !uuid(request.value("requestId")) || request.value("approvedConfigDigest")!=m_context
        || !number(request.value("maximumDownloadHeaders"),10000000)
        || (approved && !digest(request.value("approval"))))return false;
    const auto payment=request.value("payment").toObject();
    if(payment.size()!=5 || !payment.value("address").isString() || payment.value("address").toString().isEmpty()
        || payment.value("address").toString().size()>128 || !atoms(payment.value("amountAtomic"))
        || !atoms(payment.value("feeAtomic")) || !atoms(payment.value("minimumChangeAtomic"))
        || !number(payment.value("minimumConfirmations"),1000))return false;
    if(request.contains("retainedReview")) {
        const auto retained=request.value("retainedReview").toObject();
        if(retained.size()!=2 || !digest(retained.value("signingCommitment"))
            || (!retained.value("changeAddress").isNull() && (!retained.value("changeAddress").isString()
                || retained.value("changeAddress").toString().isEmpty() || retained.value("changeAddress").toString().size()>128)))return false;
    } else if(operation!="prepare")return false;
    if(cold(operation)) {if(!number(request.value("maximumHistoryEntries"),1000))return false;}
    else {
        const auto scan=request.value("walletScan").toObject();
        if(scan.size()!=3 || !number(scan.value("gap"),500) || !number(scan.value("addressesPerBranch"),500)
            || !number(scan.value("historyEntriesPerBranch"),1000))return false;
    }
    return launch(operation,request,password);
}
bool LitecoinLightFundingJob::launch(const QString &operation,const QJsonObject &request,const QByteArray &password) {
    m_input=QJsonDocument(request).toJson(QJsonDocument::Compact);
    if(operation=="sign"){m_input.chop(1);m_input.append(",\"password\":");appendPassword(m_input,password);m_input.append('}');}
    if(m_input.size()>8192){erase(m_input);return false;}
    if(m_walletBound && (!m_activeWallet || !m_activeWallet->acquireFunding(this,DexDeployment::litecoinLightCoin()))){erase(m_input);return false;}
    if(m_walletBound && operation!="terms" && !LitecoinLightFundingContext::access(m_contextDirectory,m_trade,m_context,m_retained,request.value("payment").toObject())) {
        erase(m_input);m_activeWallet->releaseFunding(this);return false;
    }
    m_request=request;m_operation=operation;m_requestId=request.value("requestId").toString();
    m_response={};erase(m_output);m_errors=0;m_busy=true;m_state="running";
    QProcessEnvironment environment;environment.insert("LANG","C");const auto system=QProcessEnvironment::systemEnvironment();
    for(const auto key:{"SSL_CERT_FILE","SSL_CERT_DIR"})if(system.contains(key))environment.insert(key,system.value(key));
    m_process.setProcessEnvironment(environment);m_process.setProcessChannelMode(QProcess::SeparateChannels);
    m_deadline.start(3600000);m_process.start(m_program,{"--dex-funding-"+operation+"-stdio-v1",m_path});
    QPointer<LitecoinLightFundingJob> guard(this);
    if(m_activeWallet)emit m_activeWallet->changed();
    if(guard)emit changed();return true;
}
void LitecoinLightFundingJob::drain() {
    if(!m_busy)return;
    m_process.setReadChannel(QProcess::StandardOutput);m_output+=m_process.read(262145-m_output.size());
    m_process.setReadChannel(QProcess::StandardError);auto errors=m_process.read(1025);m_errors+=errors.size();erase(errors);
    if(m_output.size()>262144 || m_errors>1024)finish(false);
}
bool LitecoinLightFundingJob::validResponse(const QJsonObject &r) const {
    if(r.size()!=6 || r.value("schema")!="monzero-ltc-light-dex-funding-"+m_operation+"-response-v1"
        || r.value("requestId")!=m_requestId || r.value("tradeId")!=m_trade || r.value("configurationDigest")!=m_context
        || r.value("executable")!=QJsonValue(false) || !r.value("result").isObject())return false;
    const auto result=r.value("result").toObject(),payment=m_request.value("payment").toObject();
    if(m_operation=="terms") {
        if(result.size()!=13 || result.value("schema")!="monzero-ltc-light-funding-terms-v1" || result.value("tradeId")!=m_trade
            || !digest(result.value("quoteCommitment")) || result.value("network")!=DexDeployment::litecoinGenesis()
            || result.value("watchOnly")!=QJsonValue(true) || result.value("broadcastAuthorized")!=QJsonValue(false)
            || !atoms(result.value("amountAtomic")) || !atoms(result.value("maximumFeeAtomic")) || !atoms(result.value("minimumChangeAtomic"))
            || !atoms(result.value("expiresAt")) || !number(result.value("lockConfirmations"),1000))return false;
        for(const auto key:{"contractAddress","refundAddress"})
            if(!result.value(key).isString() || result.value(key).toString().size()>128
                || !LitecoinJournalProtocol::witnessAddress(result.value(key).toString(),DexDeployment::litecoinNetwork(),
                    "q[023456789acdefghjklmnpqrstuvwxyz]{38}(?:[023456789acdefghjklmnpqrstuvwxyz]{20})?"))return false;
        return true;
    }
    if(result.value("paymentAddress")!=payment.value("address") || result.value("paymentAtomic")!=payment.value("amountAtomic")
        || result.value("feeAtomic")!=payment.value("feeAtomic") || result.value("spendable")!=QJsonValue(false)
        || result.value("prepared")!=QJsonValue(true) || !digest(result.value("signingCommitment"))
        || !digest(result.value("txid")) || !atoms(result.value("changeAtomic"),false) || !number(result.value("inputCount"),100000))return false;
    if(!result.contains("changeAddress") || (!result.value("changeAddress").isNull()
        && (!result.value("changeAddress").isString() || result.value("changeAddress").toString().isEmpty()
            || result.value("changeAddress").toString().size()>128)))return false;
    const QStringList resultKeys{"watchOnly","spendable","prepared","signed","paymentAddress","paymentAtomic",
        "feeAtomic","changeAddress","changeAtomic","inputCount","txid","signingCommitment","recoveryFrame","fundingPsbt",
        "review","broadcast","broadcastStatus","confirmationStatus","recovered","broadcastEndpoint","observation","selectedHeight","selectedTip"};
    for(auto it=result.begin();it!=result.end();++it)if(!resultKeys.contains(it.key()))return false;
    const auto retained=m_request.value("retainedReview").toObject();
    if(!retained.isEmpty() && (result.value("signingCommitment")!=retained.value("signingCommitment")
        || result.value("changeAddress")!=retained.value("changeAddress")))return false;
    const bool signedExpected=!(m_operation=="prepare" || m_operation=="review");
    const bool watchExpected=!(m_operation=="sign" || m_operation=="send" || m_operation=="retry");
    if(result.value("signed")!=QJsonValue(signedExpected) || result.value("watchOnly")!=QJsonValue(watchExpected))return false;
    if(m_operation=="sign" && result.value("broadcast")!=QJsonValue(false))return false;
    if(m_operation=="prepare") {
        const auto psbt=result.value("fundingPsbt").toString().toLatin1();
        const auto decoded=QByteArray::fromBase64(psbt);const auto frame=result.value("recoveryFrame").toString();
        if(psbt.isEmpty() || decoded.toBase64()!=psbt || !decoded.startsWith(QByteArray::fromHex("70736274ff"))
            || frame.isEmpty() || frame.size()%2 || !QRegularExpression("\\A[0-9a-f]+\\z").match(frame).hasMatch())return false;
    }
    if(QStringList{"review","send-review","retry-review"}.contains(m_operation)) {
        const auto review=result.value("review").toObject();
        const auto details=review.value("details").toObject();
        if(review.size()!=2 || !digest(review.value("commitment")) || details.isEmpty())return false;
        QByteArray domain=m_operation=="review"?"MONZERO-LTC-LIGHT-FUNDING-APPROVAL-V1":"MONZERO-LTC-LIGHT-RELAY-APPROVAL-V1";
        domain.append('\0');domain.append(QJsonDocument(details).toJson(QJsonDocument::Compact));
        if(review.value("commitment")!=hash(domain) || details.value("txid")!=result.value("txid"))return false;
        const bool sending=m_operation=="send-review",retrying=m_operation=="retry-review";
        const auto funding=sending?details.value("funding").toObject():details;
        if(funding.value("trade")!=m_trade || funding.value("claimCommitment")!=result.value("signingCommitment"))return false;
        if(!retrying && (funding.value("action")!="sign" || funding.value("broadcastAuthorized")!=QJsonValue(false)
            || funding.value("contractAddress")!=payment.value("address") || funding.value("amountAtoms")!=payment.value("amountAtomic")
            || funding.value("feeAtoms")!=payment.value("feeAtomic")))return false;
        if(sending || retrying) {
            const auto endpoint=details.value("endpoint").toObject();const auto configured=QJsonDocument::fromJson(m_wallet).object().value("endpoint").toObject();
            if(details.value("action")!=(sending?"send":"retry") || endpoint.value("host")!=configured.value("host")
                || endpoint.value("port")!=configured.value("port"))return false;
        }
    }
    if(cold(m_operation)) {
        const auto observation=result.value("observation").toObject();const auto position=observation.value("position").toString();
        if(!digest(result.value("selectedTip")) || !result.value("selectedHeight").isDouble()
            || !QStringList{"not-reported","reported-pending","included"}.contains(position))return false;
        if(position=="included" && (!digest(observation.value("blockHash")) || !number(observation.value("confirmations"),2147483647)))return false;
    }
    return true;
}
void LitecoinLightFundingJob::finish(bool success) {
    if(!m_busy)return;
    m_busy=false;m_deadline.stop();
    if(m_process.state()!=QProcess::NotRunning){m_process.kill();m_process.waitForFinished(1000);}
    const auto document=QJsonDocument::fromJson(m_output);
    bool valid=success && sourcesMatch() && document.isObject() && validResponse(document.object());
    if(valid && m_walletBound && m_operation!="terms") {
        const auto result=document.object().value("result").toObject();
        const QJsonObject review{{"signingCommitment",result.value("signingCommitment")},{"changeAddress",result.value("changeAddress")}};
        valid=LitecoinLightFundingContext::access(m_contextDirectory,m_trade,m_context,m_retained,
            m_request.value("payment").toObject(),review,result.value("txid").toString());
    }
    m_response=valid?document.object():QJsonObject{};
    // Cancellation/error cannot prove a transaction was not signed or sent.
    m_state=valid?"complete":"outcome-unconfirmed";erase(m_input);erase(m_output);
    const auto psbt=m_response.value("result").toObject().value("fundingPsbt").toString().toLatin1();
    const auto requestId=m_requestId;QPointer<LitecoinLightFundingJob> guard(this);
    if(m_activeWallet)m_activeWallet->releaseFunding(this);
    if(!guard)return;emit changed();
    if(guard && !m_busy && requestId==m_requestId && valid && !psbt.isEmpty())emit prepared(psbt);
}
void LitecoinLightFundingJob::cancel(){if(m_busy)finish(false);}
