#include "BchLightFundingController.h"
#include <QDateTime>
#include <QUuid>
#include <QJsonArray>
BchLightFundingController::BchLightFundingController(QObject *parent):QObject(parent),m_job(this) {
    connect(&m_job,&BchLightFundingJob::changed,this,&BchLightFundingController::update);

}
bool BchLightFundingController::configure(const QString &path,LitecoinLightJob *wallet,const QString &helper,
    const QString &digest,const QString &trade,const QString &quote) {
    if(!m_trade.isEmpty() || QUuid(trade).isNull() || QUuid(trade).toString(QUuid::WithoutBraces)!=trade
        || !LitecoinLightFundingContext::hash(quote))return false;
    const auto config=QJsonDocument::fromJson(LitecoinJournalProtocol::privateConfig(path,4096)).object();
    if(config.value("tradeId")!=trade || config.value("quoteCommitment")!=quote)return false;
    const auto record=QJsonDocument::fromJson(LitecoinJournalProtocol::privateConfig(config.value("quoteFile").toString(),16384)).object();
    bool validExpiry=false;m_expires=record.value("terms").toObject().value("expiresAt").toString().toULongLong(&validExpiry);
    if(!validExpiry || !m_expires)return false;
    m_trade=trade;m_quote=quote;
    if(!m_job.configureWallet(path,wallet,helper,digest)){m_trade.clear();m_quote.clear();return false;}
    return true;
}
QVariantMap BchLightFundingController::status() const {
    auto out=m_job.status();out.insert("quoteCommitment",m_quote);out.insert("valid",!m_failed);
    out.insert("terms",m_terms.toVariantMap());out.insert("review",m_review.toVariantMap());out.insert("result",m_result.toVariantMap());
    out.insert("reviewOperation",m_reviewOperation);
    const bool available=!m_trade.isEmpty() && !m_failed && out.value("sourcesValid").toBool() && !out.value("busy").toBool();
    out.insert("termsAvailable",available);
    out.insert("walletSourceSelected",m_sourceSelected);
    out.insert("prepareAvailable",available && m_sourceSelected && !m_terms.isEmpty()
        && m_terms.value("expiresAt").toString().toULongLong()>quint64(QDateTime::currentSecsSinceEpoch()));
    out.insert("reviewAvailable",available && !out.value("retainedReview").toMap().isEmpty());
    out.insert("approvalAvailable",available && !m_review.isEmpty() && quint64(QDateTime::currentSecsSinceEpoch())<m_expires);
    if(m_failed)out.insert("state","context-mismatch");else if(m_rejected)out.insert("state","request-rejected");
    return out;
}
bool BchLightFundingController::setObservationLimits(quint32 gap,quint32 addresses,quint32 entries,quint32 headers) {
    if(m_job.status().value("busy").toBool() || gap==0 || gap>500 || addresses==0 || addresses>500
        || entries==0 || entries>1000 || headers==0 || headers>10000000)return false;
    m_gap=gap;m_addresses=addresses;m_entries=entries;m_headers=headers;return true;
}
bool BchLightFundingController::quoteMatches(const QJsonObject &review) const {
    const auto details=review.value("details").toObject();
    if(details.value("action")=="retain-private-recovery-only")
        return details.value("swapId")==m_trade && details.value("quote")==m_quote;
    return details.value("tradeId")==m_trade && details.value("quoteCommitment")==m_quote;
}
void BchLightFundingController::update() {
    const auto state=m_job.status();const auto response=m_job.response();
    const auto id=response.value("requestId").toString();
    if(!state.value("busy").toBool() && state.value("state")=="complete" && !id.isEmpty() && id==m_expected && id!=m_seen) {
        const auto operation=state.value("operation").toString();
        if(operation!=m_operation){m_failed=true;emit changed();return;}
        m_seen=id;const auto result=response.value("result").toObject();
        if(m_operation=="source-select")m_sourceSelected=true;
        if(m_operation=="terms") {
            if(result.value("quoteCommitment")!=m_quote || result.value("tradeId")!=m_trade)m_failed=true;
            else m_terms=result;
        }
        if(m_operation=="review") {
            // The helper revalidated this exact claim against the admitted quote.
            // BCH signing approves the wallet commitment, not a LTC PSBT frame.
            m_review={{"commitment",result.value("signingCommitment")},
                {"details",QJsonObject{{"action","sign"},{"tradeId",m_trade},{"quoteCommitment",m_quote},
                    {"unsignedTxid",result.value("unsignedTxid")},{"paymentAddress",result.value("paymentAddress")},
                    {"paymentAtomic",result.value("paymentAtomic")},{"feeAtomic",result.value("feeAtomic")},
                    {"changeAddress",result.value("changeAddress")},{"changeAtomic",result.value("changeAtomic")}}}};
            m_reviewOperation="review";
        }
        if(QStringList{"recovery-review","send-review","retry-review"}.contains(operation) && result.contains("review")) {
            const auto review=result.value("review").toObject();
            if(!quoteMatches(review))m_failed=true;
            else {m_review=review;m_reviewOperation=m_operation;}
        }
        if(m_failed){m_terms={};m_review={};m_result={};m_reviewOperation.clear();}
        else m_result=result;
    }
    emit changed();
}
bool BchLightFundingController::run(const QString &operation,const QJsonObject &payment,const QString &approval,const QByteArray &password) {
    const auto state=m_job.status();if(m_trade.isEmpty() || m_failed || state.value("busy").toBool())return false;
    QJsonObject request{{"schema","monzero-bch-light-dex-funding-"+operation+"-request-v1"},
        {"requestId",QUuid::createUuid().toString(QUuid::WithoutBraces)},{"approvedConfigDigest",state.value("configurationDigest").toString()}};
    if(operation!="terms" && operation!="source-select" && !operation.startsWith("recovery-")) {
        const auto retained=QJsonObject::fromVariantMap(state.value("retainedPayment").toMap());
        if(payment.isEmpty() && retained.isEmpty())return false;
        request.insert("payment",payment.isEmpty()?retained:payment);request.insert("maximumDownloadHeaders",double(m_headers));
        if(QStringList{"observe","retry-review","retry","reconcile-ack"}.contains(operation))request.insert("maximumHistoryEntries",double(m_entries));
        else request.insert("walletScan",QJsonObject{{"gap",double(m_gap)},{"addressesPerBranch",double(m_addresses)},{"historyEntriesPerBranch",double(m_entries)}});
    }
    if(!approval.isEmpty())request.insert("approval",approval);
    m_expected=request.value("requestId").toString();
    m_rejected=false;m_operation=operation;m_review={};m_reviewOperation.clear();m_result={};if(operation=="terms")m_terms={};
    const bool started=m_job.run(operation,request,password);if(!started){m_rejected=true;emit changed();}return started;
}
bool BchLightFundingController::selectWalletSource(){return run("source-select");}
bool BchLightFundingController::queryTerms(){return run("terms");}
bool BchLightFundingController::prepare(const QString &fee,quint32 confirmations) {
    if(!status().value("prepareAvailable").toBool() || confirmations<quint32(m_terms.value("lockConfirmations").toInt()) || confirmations>1000)return false;
    bool ok=false;const auto atomic=fee.toULongLong(&ok);
    if(!ok || !atomic || QString::number(atomic)!=fee || atomic>m_terms.value("maximumFeeAtomic").toString().toULongLong())return false;
    return run("prepare",{{"address",m_terms.value("contractAddress")},{"amountAtomic",m_terms.value("amountAtomic")},
        {"feeAtomic",fee},{"minimumChangeAtomic",m_terms.value("minimumChangeAtomic")},{"minimumConfirmations",double(confirmations)}});
}
bool BchLightFundingController::reopen() {
    if(!status().value("prepareAvailable").toBool())return false;
    const auto payment=m_job.status().value("retainedPayment").toMap();
    if(payment.value("address")!=m_terms.value("contractAddress").toVariant() || payment.value("amountAtomic")!=m_terms.value("amountAtomic").toVariant())return false;
    return run("prepare");
}
bool BchLightFundingController::reviewSignature(){return run("review");}
bool BchLightFundingController::approve(const QString &review,const QString &operation,const QString &commitment,const QByteArray &password) {
    if(quint64(QDateTime::currentSecsSinceEpoch())>=m_expires || m_reviewOperation!=review || m_review.isEmpty() || commitment!=m_review.value("commitment").toString())return false;
    return run(operation,{},commitment,password);
}
bool BchLightFundingController::sign(const QString &commitment,const QByteArray &password) {
    if(password.isEmpty() || password.size()>1024)return false;
    return approve("review","sign",commitment,password);
}
bool BchLightFundingController::reviewRecovery(){return run("recovery-review");}
bool BchLightFundingController::retainRecovery(const QString &commitment){return approve("recovery-review","recovery-retain",commitment);}
bool BchLightFundingController::reconcileAcknowledgement(){return run("reconcile-ack");}
bool BchLightFundingController::reviewSend(){return run("send-review");}
bool BchLightFundingController::send(const QString &commitment){return approve("send-review","send",commitment);}
bool BchLightFundingController::observe(){return run("observe");}
bool BchLightFundingController::reviewRetry(){return run("retry-review");}
bool BchLightFundingController::retry(const QString &commitment){return approve("retry-review","retry",commitment);}
void BchLightFundingController::cancel(){m_review={};m_reviewOperation.clear();m_job.cancel();emit changed();}
