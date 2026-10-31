#include "LitecoinLightFundingController.h"
#include <QDateTime>
#include <QUuid>
#include <QJsonArray>
LitecoinLightFundingController::LitecoinLightFundingController(QObject *parent):QObject(parent),m_job(this) {
    connect(&m_job,&LitecoinLightFundingJob::changed,this,&LitecoinLightFundingController::update);
    connect(&m_job,&LitecoinLightFundingJob::prepared,this,[this](const QByteArray &psbt){
        if(!m_failed && m_operation=="prepare" && m_result.value("fundingPsbt").toString().toLatin1()==psbt)emit prepared(psbt);
    });
}
bool LitecoinLightFundingController::configure(const QString &path,LitecoinLightJob *wallet,const QString &helper,
    const QString &digest,const QString &trade,const QString &quote) {
    if(!m_trade.isEmpty() || QUuid(trade).isNull() || QUuid(trade).toString(QUuid::WithoutBraces)!=trade
        || !LitecoinLightFundingContext::hash(quote))return false;
    const auto config=QJsonDocument::fromJson(LitecoinJournalProtocol::privateConfig(path,4096)).object();
    if(config.value("tradeId")!=trade)return false;
    m_trade=trade;m_quote=quote;
    if(!m_job.configureWallet(path,wallet,helper,digest)){m_trade.clear();m_quote.clear();return false;}
    return true;
}
QVariantMap LitecoinLightFundingController::status() const {
    auto out=m_job.status();out.insert("quoteCommitment",m_quote);out.insert("valid",!m_failed);
    out.insert("terms",m_terms.toVariantMap());out.insert("review",m_review.toVariantMap());out.insert("result",m_result.toVariantMap());
    out.insert("reviewOperation",m_reviewOperation);
    const bool available=!m_trade.isEmpty() && !m_failed && !out.value("busy").toBool();
    out.insert("termsAvailable",available);
    out.insert("prepareAvailable",available && !m_terms.isEmpty()
        && m_terms.value("expiresAt").toString().toULongLong()>quint64(QDateTime::currentSecsSinceEpoch()));
    out.insert("reviewAvailable",available && !out.value("retainedReview").toMap().isEmpty());
    out.insert("approvalAvailable",available && !m_review.isEmpty());
    if(m_failed)out.insert("state","context-mismatch");else if(m_rejected)out.insert("state","request-rejected");
    return out;
}
bool LitecoinLightFundingController::setObservationLimits(quint32 gap,quint32 addresses,quint32 entries,quint32 headers) {
    if(m_job.status().value("busy").toBool() || gap==0 || gap>500 || addresses==0 || addresses>500
        || entries==0 || entries>1000 || headers==0 || headers>10000000)return false;
    m_gap=gap;m_addresses=addresses;m_entries=entries;m_headers=headers;return true;
}
bool LitecoinLightFundingController::quoteMatches(const QJsonObject &review) const {
    auto details=review.value("details").toObject();if(details.value("action")=="send")details=details.value("funding").toObject();
    const auto quote=details.value("quote").toArray();if(quote.size()!=32 || details.value("trade")!=m_trade)return false;
    QByteArray bytes;for(const auto value:quote) {
        if(!value.isDouble() || value.toDouble()<0 || value.toDouble()>255 || value.toDouble()!=value.toInt())return false;
        bytes.append(char(value.toInt()));
    }
    return QString::fromLatin1(bytes.toHex())==m_quote;
}
void LitecoinLightFundingController::update() {
    const auto state=m_job.status();const auto response=m_job.response();
    const auto id=response.value("requestId").toString();
    if(!state.value("busy").toBool() && state.value("state")=="complete" && !id.isEmpty() && id!=m_seen) {
        m_seen=id;const auto result=response.value("result").toObject();
        if(m_operation=="terms") {
            if(result.value("quoteCommitment")!=m_quote || result.value("tradeId")!=m_trade)m_failed=true;
            else m_terms=result;
        }
        if(result.contains("review")) {
            const auto review=result.value("review").toObject();
            if(!quoteMatches(review))m_failed=true;
            else {m_review=review;m_reviewOperation=m_operation;}
        }
        if(m_failed){m_terms={};m_review={};m_result={};m_reviewOperation.clear();}
        else m_result=result;
    }
    emit changed();
}
bool LitecoinLightFundingController::run(const QString &operation,const QJsonObject &payment,const QString &approval,const QByteArray &password) {
    const auto state=m_job.status();if(m_trade.isEmpty() || m_failed || state.value("busy").toBool())return false;
    QJsonObject request{{"schema","monzero-ltc-light-dex-funding-"+operation+"-request-v1"},
        {"requestId",QUuid::createUuid().toString(QUuid::WithoutBraces)},{"approvedConfigDigest",state.value("configurationDigest").toString()}};
    if(operation!="terms") {
        const auto retained=QJsonObject::fromVariantMap(state.value("retainedPayment").toMap());
        if(payment.isEmpty() && retained.isEmpty())return false;
        request.insert("payment",payment.isEmpty()?retained:payment);request.insert("maximumDownloadHeaders",double(m_headers));
        if(QStringList{"observe","retry-review","retry"}.contains(operation))request.insert("maximumHistoryEntries",double(m_entries));
        else request.insert("walletScan",QJsonObject{{"gap",double(m_gap)},{"addressesPerBranch",double(m_addresses)},{"historyEntriesPerBranch",double(m_entries)}});
        if(!approval.isEmpty())request.insert("approval",approval);
    }
    m_rejected=false;m_operation=operation;m_review={};m_reviewOperation.clear();m_result={};if(operation=="terms")m_terms={};
    const bool started=m_job.run(operation,request,password);if(!started){m_rejected=true;emit changed();}return started;
}
bool LitecoinLightFundingController::queryTerms(){return run("terms");}
bool LitecoinLightFundingController::prepare(const QString &fee,quint32 confirmations) {
    if(!status().value("prepareAvailable").toBool() || confirmations<1 || confirmations>1000)return false;
    bool ok=false;const auto atomic=fee.toULongLong(&ok);
    if(!ok || !atomic || QString::number(atomic)!=fee || atomic>m_terms.value("maximumFeeAtomic").toString().toULongLong())return false;
    return run("prepare",{{"address",m_terms.value("contractAddress")},{"amountAtomic",m_terms.value("amountAtomic")},
        {"feeAtomic",fee},{"minimumChangeAtomic",m_terms.value("minimumChangeAtomic")},{"minimumConfirmations",double(confirmations)}});
}
bool LitecoinLightFundingController::reopen() {
    if(!status().value("prepareAvailable").toBool())return false;
    const auto payment=m_job.status().value("retainedPayment").toMap();
    if(payment.value("address")!=m_terms.value("contractAddress").toVariant() || payment.value("amountAtomic")!=m_terms.value("amountAtomic").toVariant())return false;
    return run("prepare");
}
bool LitecoinLightFundingController::reviewSignature(){return run("review");}
bool LitecoinLightFundingController::approve(const QString &review,const QString &operation,const QString &commitment,const QByteArray &password) {
    if(m_reviewOperation!=review || m_review.isEmpty() || commitment!=m_review.value("commitment").toString())return false;
    return run(operation,{},commitment,password);
}
bool LitecoinLightFundingController::sign(const QString &commitment,const QByteArray &password) {
    if(password.isEmpty() || password.size()>1024)return false;
    return approve("review","sign",commitment,password);
}
bool LitecoinLightFundingController::reviewSend(){return run("send-review");}
bool LitecoinLightFundingController::send(const QString &commitment){return approve("send-review","send",commitment);}
bool LitecoinLightFundingController::observe(){return run("observe");}
bool LitecoinLightFundingController::reviewRetry(){return run("retry-review");}
bool LitecoinLightFundingController::retry(const QString &commitment){return approve("retry-review","retry",commitment);}
void LitecoinLightFundingController::cancel(){m_review={};m_reviewOperation.clear();m_job.cancel();emit changed();}
