#include "BchLightFundingJob.h"
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
bool cold(const QString &op) {return QStringList{"observe","retry-review","retry","reconcile-ack"}.contains(op);}
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
BchLightFundingJob::BchLightFundingJob(QObject *parent):QObject(parent) {
    m_deadline.setSingleShot(true);
    connect(&m_deadline,&QTimer::timeout,this,[this]{finish(false);});
    connect(&m_process,&QProcess::started,this,[this]{
        if(m_process.write(m_input)!=m_input.size()) {finish(false);return;}
        m_process.closeWriteChannel();erase(m_input);
    });
    connect(&m_process,&QProcess::readyReadStandardOutput,this,&BchLightFundingJob::drain);
    connect(&m_process,&QProcess::readyReadStandardError,this,&BchLightFundingJob::drain);
    connect(&m_process,&QProcess::errorOccurred,this,[this](QProcess::ProcessError){finish(false);});
    connect(&m_process,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[this](int code,QProcess::ExitStatus exit){
        QPointer<BchLightFundingJob> guard(this);const auto id=m_requestId;
        drain();if(guard && id==m_requestId)finish(code==0 && exit==QProcess::NormalExit && m_errors==0);
    });
}
BchLightFundingJob::~BchLightFundingJob() {
    disconnect(&m_process,nullptr,this,nullptr);erase(m_input);erase(m_output);
    if(m_process.state()!=QProcess::NotRunning){m_process.kill();m_process.waitForFinished(1000);}
    if(m_activeWallet)m_activeWallet->releaseFunding(this);
}
bool BchLightFundingJob::configure(const QString &path,const QString &helper,const QString &trustedDigest) {
    if(!m_path.isEmpty() || m_busy || !BackendExecutable::matches(helper,trustedDigest))return false;
    const auto bytes=LitecoinJournalProtocol::privateConfig(path,4096);const auto c=QJsonDocument::fromJson(bytes).object();
    const auto walletPath=c.value("walletConfigFile").toString();
    const auto wallet=LitecoinJournalProtocol::privateConfig(walletPath,4096);const auto w=QJsonDocument::fromJson(wallet).object();
    const auto quotePath=c.value("quoteFile").toString();
    const auto quoteBytes=LitecoinJournalProtocol::privateConfig(quotePath,16384);
    const auto quote=QJsonDocument::fromJson(quoteBytes).object();
    if(quote.value("schema")!="monzero-bch-quote-record-v1" || quote.value("commitment")!=c.value("quoteCommitment")
        || !digest(c.value("quoteCommitment")) || quote.value("terms").toObject().value("swapId")!=c.value("tradeId"))return false;
    if(c.size()!=11 || c.value("schema")!="monzero-private-bch-light-dex-v1" || !uuid(c.value("tradeId"))
        || c.value("policy").toObject().value("bch_network")!="bch-regtest"
        || !c.value("negotiationPins").isObject() || !c.value("identityPins").isArray() || !QFileInfo(c.value("metadataDirectory").toString()).isAbsolute()
        || !QFileInfo(c.value("snapshotDirectory").toString()).isAbsolute()
        || w.value("schema")!="monzero-private-bch-light-v1" || w.value("network")!="regtest"
        || !w.value("wallet").isObject() || !w.value("endpoint").isObject())return false;
    m_context=hash(QJsonDocument(QJsonObject{{"schema","monzero-bch-light-dex-context-v1"},
        {"dexDigest",hash(bytes)},{"walletDigest",hash(wallet)},{"quoteDigest",hash(quoteBytes)}}).toJson(QJsonDocument::Compact));
    if(m_walletBound) {
        m_contextDirectory=c.value("metadataDirectory").toString();
        if(!BchLightFundingContext::access(m_contextDirectory,c.value("tradeId").toString(),m_context,m_retained))return false;
    }
    m_path=path;m_walletPath=walletPath;m_quotePath=quotePath;m_quoteBytes=quoteBytes;m_configuration=bytes;m_wallet=wallet;m_program=helper;m_digest=trustedDigest;
    m_trade=c.value("tradeId").toString();m_state="ready";emit changed();return true;
}
bool BchLightFundingJob::configureWallet(const QString &path,LitecoinLightJob *wallet,const QString &helper,const QString &trustedDigest) {
    if(!wallet || wallet->thread()!=thread() || !m_path.isEmpty())return false;
    QString activePath,activeHelper,activeDigest;QByteArray bytes;
    const auto c=QJsonDocument::fromJson(LitecoinJournalProtocol::privateConfig(path,4096)).object();
    if(!wallet->fundingConfiguration(activePath,bytes,activeHelper,activeDigest,"bch")
        || c.value("walletConfigFile")!=activePath || activeHelper!=helper || activeDigest!=trustedDigest)return false;
    // Set the source guard before configure() announces readiness.
    m_activeWallet=wallet;m_walletBound=true;
    if(!configure(path,helper,trustedDigest)){m_activeWallet.clear();m_walletBound=false;return false;}
    return true;
}
bool BchLightFundingJob::sourcesMatch() const {
    if(m_walletBound) {
        QString path,helper,digest;QByteArray bytes;
        if(!m_activeWallet || !m_activeWallet->fundingConfiguration(path,bytes,helper,digest,"bch")
            || path!=m_walletPath || bytes!=m_wallet || helper!=m_program || digest!=m_digest)return false;
    }
    return !m_path.isEmpty() && BackendExecutable::matches(m_program,m_digest)
        && LitecoinJournalProtocol::privateConfig(m_path,4096)==m_configuration
        && LitecoinJournalProtocol::privateConfig(m_walletPath,4096)==m_wallet
        && LitecoinJournalProtocol::privateConfig(m_quotePath,16384)==m_quoteBytes;
}
QVariantMap BchLightFundingJob::status() const {
    return {{"configured",!m_path.isEmpty()},{"busy",m_busy || m_finishing || m_starting},{"state",m_state},{"tradeId",m_trade},
        {"sourcesValid",sourcesMatch()},{"configurationDigest",m_context},{"operation",m_operation},{"executable",false},
        {"retainedPayment",m_retained.payment.toVariantMap()},{"retainedReview",m_retained.review.toVariantMap()},{"retainedUnsignedTxid",m_retained.unsignedTxid},{"retainedTxid",m_retained.signedTxid}};
}
bool BchLightFundingJob::run(const QString &operation,const QJsonObject &provided,const QByteArray &password) {
    auto request=provided;
    if(m_busy || m_finishing || m_starting || m_process.state()!=QProcess::NotRunning || !sourcesMatch()
        || !QStringList{"terms","source-select","prepare","review","sign","send-review","send","observe","retry-review","retry","reconcile-ack","recovery-review","recovery-retain"}.contains(operation))return false;
    if(operation=="terms" || operation=="source-select") {
        if(request.size()!=3 || request.value("schema")!="monzero-bch-light-dex-funding-"+operation+"-request-v1"
            || !uuid(request.value("requestId")) || request.value("approvedConfigDigest")!=m_context || !password.isEmpty())return false;
        return launch(operation,request,{});
    }
    if(m_walletBound && !BchLightFundingContext::access(m_contextDirectory,m_trade,m_context,m_retained))return false;
    if(operation=="recovery-review" || operation=="recovery-retain") {
        const bool retain=operation=="recovery-retain";
        if((m_walletBound && m_retained.signedTxid.isEmpty()) || !password.isEmpty() || request.size()!=(retain?4:3)
            || request.value("schema")!="monzero-bch-light-dex-funding-"+operation+"-request-v1"
            || !uuid(request.value("requestId")) || request.value("approvedConfigDigest")!=m_context
            || (retain && !digest(request.value("approval"))))return false;
        return launch(operation,request,{});
    }
    if(m_walletBound) {
        if(!BchLightFundingContext::access(m_contextDirectory,m_trade,m_context,m_retained))return false;
        if(!m_retained.payment.isEmpty() && request.value("payment")!=m_retained.payment)return false;
        if(!m_retained.review.isEmpty()) {
            if(request.contains("retainedReview") && request.value("retainedReview")!=m_retained.review)return false;
            if(operation!="prepare")request.insert("retainedReview",m_retained.review);
        } else if(operation!="prepare")return false;
    }
    const bool signing=operation=="sign",approved=signing || operation=="send" || operation=="retry";
    if(signing?(password.isEmpty() || password.size()>1024):!password.isEmpty())return false;
    QStringList allowed{"schema","requestId","approvedConfigDigest","maximumDownloadHeaders","payment","retainedReview"};
    allowed.append(cold(operation)?"maximumHistoryEntries":"walletScan");if(approved)allowed.append("approval");
    for(auto it=request.begin();it!=request.end();++it)if(!allowed.contains(it.key()))return false;
    if(request.value("schema")!="monzero-bch-light-dex-funding-"+operation+"-request-v1"
        || !uuid(request.value("requestId")) || request.value("approvedConfigDigest")!=m_context
        || !number(request.value("maximumDownloadHeaders"),10000000)
        || (approved && !digest(request.value("approval"))))return false;
    const auto payment=request.value("payment").toObject();
    if(payment.size()!=5 || !payment.value("address").isString() || payment.value("address").toString().isEmpty()
        || payment.value("address").toString().size()>128 || !atoms(payment.value("amountAtomic"))
        || !atoms(payment.value("feeAtomic")) || !atoms(payment.value("minimumChangeAtomic"))
        || !number(payment.value("minimumConfirmations"),1000))return false;
    const auto quoteTerms=QJsonDocument::fromJson(m_quoteBytes).object().value("terms").toObject();
    const auto policy=QJsonDocument::fromJson(m_configuration).object().value("policy").toObject();
    if(payment.value("amountAtomic")!=quoteTerms.value("bchAtoms")
        || payment.value("feeAtomic").toString().toULongLong()>quoteTerms.value("bchFundingFeeMaxAtoms").toString().toULongLong()
        || payment.value("minimumChangeAtomic").toString().toULongLong()<quoteTerms.value("bchMinimumOutputAtoms").toString().toULongLong()
        || payment.value("minimumConfirmations").toInt()<policy.value("bch_confirmations").toInt())return false;
    if(request.contains("retainedReview")) {
        const auto retained=request.value("retainedReview").toObject();
        if(retained.size()!=2 || !digest(retained.value("signingCommitment"))
            || (!retained.value("changeAddress").isNull() && (!retained.value("changeAddress").isString()
                || retained.value("changeAddress").toString().isEmpty() || retained.value("changeAddress").toString().size()>128)))return false;
    } else if(operation!="prepare")return false;
    if(signing && request.value("approval")!=request.value("retainedReview").toObject().value("signingCommitment"))return false;
    if(cold(operation)) {if(!number(request.value("maximumHistoryEntries"),1000))return false;}
    else {
        const auto scan=request.value("walletScan").toObject();
        if(scan.size()!=3 || !number(scan.value("gap"),500) || !number(scan.value("addressesPerBranch"),500)
            || !number(scan.value("historyEntriesPerBranch"),1000))return false;
    }
    return launch(operation,request,password);
}
bool BchLightFundingJob::launch(const QString &operation,const QJsonObject &request,const QByteArray &password) {
    m_input=QJsonDocument(request).toJson(QJsonDocument::Compact);
    if(operation=="sign"){m_input.chop(1);m_input.append(",\"password\":");appendPassword(m_input,password);m_input.append('}');}
    if(m_input.size()>4096){erase(m_input);return false;}
    if(m_walletBound && (!m_activeWallet || !m_activeWallet->acquireFunding(this,"bch"))){erase(m_input);return false;}
    if(m_walletBound && !request.value("payment").toObject().isEmpty() && !BchLightFundingContext::access(m_contextDirectory,m_trade,m_context,m_retained,request.value("payment").toObject())) {
        erase(m_input);m_activeWallet->releaseFunding(this);return false;
    }
    m_request=request;m_operation=operation;m_requestId=request.value("requestId").toString();
    m_response={};erase(m_output);m_errors=0;m_busy=true;m_state="running";
    QProcessEnvironment environment;environment.insert("LANG","C");const auto system=QProcessEnvironment::systemEnvironment();
    for(const auto key:{"SSL_CERT_FILE","SSL_CERT_DIR"})if(system.contains(key))environment.insert(key,system.value(key));
    m_process.setProcessEnvironment(environment);m_process.setProcessChannelMode(QProcess::SeparateChannels);
    QPointer<BchLightFundingJob> guard(this);
    m_starting=true;m_deadline.start(3600000);m_process.start(m_program,{"--bch-dex-funding-"+operation+"-stdio-v1",m_path});
    if(!guard)return false;
    m_starting=false;const bool started=m_busy;
    if(m_activeWallet)emit m_activeWallet->changed();
    if(guard)emit changed();return started;
}
void BchLightFundingJob::drain() {
    if(!m_busy)return;
    m_process.setReadChannel(QProcess::StandardOutput);m_output+=m_process.read(262145-m_output.size());
    m_process.setReadChannel(QProcess::StandardError);auto errors=m_process.read(1025);m_errors+=errors.size();erase(errors);
    if(m_output.size()>262144 || m_errors>1024)finish(false);
}
bool BchLightFundingJob::validResponse(const QJsonObject &r) const {
    if(r.size()!=6 || r.value("schema")!="monzero-bch-light-dex-funding-"+m_operation+"-response-v1"
        || r.value("requestId")!=m_requestId || r.value("tradeId")!=m_trade || r.value("configurationDigest")!=m_context
        || r.value("executable")!=QJsonValue(false) || !r.value("result").isObject())return false;
    const auto result=r.value("result").toObject();
    const auto quote=QJsonDocument::fromJson(m_quoteBytes).object(),terms=quote.value("terms").toObject();
    if(result.value("broadcastAuthorized")!=QJsonValue(false))return false;
    if(m_operation=="source-select")
        return result.size()==3 && result.value("walletSourceSelected")==QJsonValue(true)
            && result.value("watchOnly")==QJsonValue(true);
    if(m_operation=="terms") {
        if(result.size()!=16 || result.value("schema")!="monzero-bch-light-funding-terms-v1"
            || result.value("tradeId")!=m_trade || result.value("quoteCommitment")!=quote.value("commitment")
            || result.value("network")!="bch-regtest" || result.value("genesis")!=terms.value("bchGenesis")
            || result.value("watchOnly")!=QJsonValue(true) || !atoms(result.value("amountAtomic"))
            || !atoms(result.value("maximumFeeAtomic")) || !atoms(result.value("minimumChangeAtomic"))
            || !atoms(result.value("expiresAt")) || !number(result.value("lockConfirmations"),1000))return false;
        for(const auto key:{"contractAddress","refundAddress"})
            if(!result.value(key).toString().startsWith("bchreg:") || result.value(key).toString().size()>128)return false;
        return result.value("refundScript")==terms.value("bchRefundScript") && result.value("expiresAt")==terms.value("expiresAt");
    }
    const bool recovery=m_operation.startsWith("recovery-");
    const bool relay=QStringList{"send-review","send","retry-review","retry"}.contains(m_operation);
    if(result.contains("review")!=(recovery || relay))return false;
    if(recovery) {
        if(result.size()!=(m_operation=="recovery-retain"?4:3)
            || result.value("privateRecoveryRetained")!=QJsonValue(m_operation=="recovery-retain")
            || (m_operation=="recovery-retain" && !digest(result.value("recoveryCommitment"))))return false;
    } else {
        const auto payment=m_request.value("payment").toObject(),retained=m_request.value("retainedReview").toObject();
        if(result.value("paymentAddress")!=payment.value("address") || result.value("paymentAtomic")!=payment.value("amountAtomic")
            || result.value("feeAtomic")!=payment.value("feeAtomic") || result.value("spendable")!=QJsonValue(false)
            || result.value("prepared")!=QJsonValue(true) || !digest(result.value("signingCommitment"))
            || !atoms(result.value("changeAtomic"),false) || !number(result.value("inputCount"),1000))return false;
        const QJsonObject responseReview{{"signingCommitment",result.value("signingCommitment")},{"changeAddress",result.value("changeAddress")}};
        if(!LitecoinLightFundingContext::validReview(responseReview) || (!retained.isEmpty() && responseReview!=retained))return false;
        const bool unsignedOnly=m_operation=="prepare" || m_operation=="review";
        if(result.value("signed")!=QJsonValue(!unsignedOnly) || result.value("watchOnly")!=QJsonValue(m_operation!="sign"))return false;
        if(unsignedOnly) {
            if(result.contains("txid") || !digest(result.value("unsignedTxid")))return false;
        } else if(!digest(result.value("txid")) || result.value("tradeFundingRetained")!=QJsonValue(true) && m_operation!="reconcile-ack")return false;
        if(!m_retained.unsignedTxid.isEmpty() && result.contains("unsignedTxid") && result.value("unsignedTxid")!=m_retained.unsignedTxid)return false;
        if(!m_retained.signedTxid.isEmpty() && !unsignedOnly && result.value("txid")!=m_retained.signedTxid)return false;
        if(!cold(m_operation) && (result.value("fundingVerified")!=QJsonValue(true) || !number(result.value("sourceTransactions"),1000)))return false;
        const QStringList keys{"watchOnly","spendable","prepared","signed","paymentAddress","paymentAtomic","feeAtomic",
            "changeAddress","changeAtomic","inputCount","txid","unsignedTxid","signingCommitment","fundingVerified","sourceTransactions",
            "operation","tradeId","selectedHeight","broadcastAuthorized","broadcast","tradeFundingRetained","review","recovered",
            "broadcastStatus","confirmationStatus","broadcastEndpoint","observation","selectedTip","tradeAcknowledgementRetained"};
        for(auto it=result.begin();it!=result.end();++it)if(!keys.contains(it.key()))return false;
        if(m_operation=="sign" && (result.value("broadcast")!=QJsonValue(false)
            || result.value("signingCommitment")!=m_request.value("approval")))return false;
        if(m_operation=="send" || m_operation=="retry" || m_operation=="reconcile-ack") {
            if(result.value("broadcastStatus")!="server-acknowledged"
                || result.value("broadcast")!=QJsonValue(m_operation!="reconcile-ack"))return false;
        }
        if(m_operation=="reconcile-ack" && result.value("tradeAcknowledgementRetained")!=QJsonValue(true))return false;
        if(cold(m_operation) && m_operation!="reconcile-ack") {
            const auto position=result.value("observation").toObject();const auto state=position.value("position").toString();
            if(!digest(result.value("selectedTip")) || !number(result.value("selectedHeight"),2147483647)
                || !QStringList{"not-reported","reported-pending","included"}.contains(state)
                || !QStringList{"no-local-attempt","outcome-unknown","server-acknowledged"}.contains(result.value("broadcastStatus").toString()))return false;
            if(state=="included" && (!digest(position.value("blockHash")) || !number(position.value("height"),2147483647)
                || !number(position.value("confirmations"),2147483647)))return false;
            if(result.value("confirmationStatus")!=(state=="included"?"included":"not-confirmed"))return false;
        }
    }
    if(recovery || QStringList{"send-review","send","retry-review","retry"}.contains(m_operation)) {
        const auto review=result.value("review").toObject(),details=review.value("details").toObject();
        const auto domain=recovery?QByteArray("monzero-bch-recovery-retention-review-v1"):QByteArray("monzero-bch-light-relay-review-v1");
        QByteArray preimage=domain;preimage.append('\0');preimage+=QJsonDocument(details).toJson(QJsonDocument::Compact);
        if(review.size()!=2 || review.value("commitment")!=hash(preimage) || details.value("schema")!=QString::fromLatin1(domain))return false;
        if(recovery) {
            if(details.value("swapId")!=m_trade || details.value("quote")!=quote.value("commitment")
                || details.value("action")!="retain-private-recovery-only" || !digest(details.value("fundingTxid"))
                || (!m_retained.signedTxid.isEmpty() && details.value("fundingTxid")!=m_retained.signedTxid))return false;
            for(const auto key:{"cancelTxid","refundTxid","punishTxid"})if(!digest(details.value(key)))return false;
        } else {
            const bool retry=m_operation.startsWith("retry");
            const auto endpoint=details.value("endpoint").toObject(),configured=QJsonDocument::fromJson(m_wallet).object().value("endpoint").toObject();
            if(details.value("tradeId")!=m_trade || details.value("quoteCommitment")!=quote.value("commitment")
                || details.value("action")!=(retry?"retry":"send") || details.value("txid")!=result.value("txid")
                || details.value("claimCommitment")!=result.value("signingCommitment")
                || endpoint.value("host")!=configured.value("host") || endpoint.value("port")!=configured.value("port"))return false;
        }
        if((m_operation=="send" || m_operation=="retry" || m_operation=="recovery-retain")
            && review.value("commitment")!=m_request.value("approval"))return false;
    }
    return true;
}
void BchLightFundingJob::finish(bool success) {
    if(!m_busy)return;
    m_finishing=true;m_busy=false;m_deadline.stop();
    if(m_process.state()!=QProcess::NotRunning){m_process.kill();m_process.waitForFinished(1000);}
    const auto document=QJsonDocument::fromJson(m_output);
    bool valid=success && sourcesMatch() && document.isObject() && validResponse(document.object());
    if(valid && m_walletBound && m_operation!="terms" && m_operation!="source-select" && !m_operation.startsWith("recovery-")) {
        const auto result=document.object().value("result").toObject();
        const QJsonObject review{{"signingCommitment",result.value("signingCommitment")},{"changeAddress",result.value("changeAddress")}};
        valid=BchLightFundingContext::access(m_contextDirectory,m_trade,m_context,m_retained,
            m_request.value("payment").toObject(),review,
            result.contains("unsignedTxid")?result.value("unsignedTxid").toString():m_retained.unsignedTxid,
            result.value("signed")==QJsonValue(true)?result.value("txid").toString():QString{});
    }
    m_response=valid?document.object():QJsonObject{};
    // Cancellation/error cannot prove a transaction was not signed or sent.
    m_state=valid?"complete":"outcome-unconfirmed";erase(m_input);erase(m_output);
    QPointer<BchLightFundingJob> guard(this);
    if(m_activeWallet)m_activeWallet->releaseFunding(this);
    if(!guard)return;m_finishing=false;emit changed();
}
void BchLightFundingJob::cancel(){if(m_busy)finish(false);}
