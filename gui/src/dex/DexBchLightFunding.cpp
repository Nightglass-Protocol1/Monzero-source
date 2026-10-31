#include "DexEngine.h"
#include "BchAdmissionJob.h"
#include "BchLightFundingController.h"
#include "BchLightFundingSetup.h"
#include <QScopedValueRollback>

namespace {
// Compare already-validated snapshots without rereading or rehashing sources.
bool boundContext(const QJsonObject &current,const QJsonObject &saved,const QVariantMap &funding) {
    return !saved.isEmpty() && current==saved && funding.value("configured").toBool()
        && funding.value("sourcesValid").toBool()
        && funding.value("tradeId").toString()==saved.value("tradeId").toString()
        && funding.value("quoteCommitment").toString()==saved.value("quoteCommitment").toString();
}
}
bool DexEngine::bchLightFundingAllowed() const {
    if(!bchOperationIdle())return false;
    return boundContext(m_bchAdmission->lightFundingContext(),m_bchLightContext,m_bchLightFunding->status());
}
QVariantMap DexEngine::bchLightFundingStatus() const {
    auto out=m_bchLightFunding->status();const auto context=m_bchAdmission->lightFundingContext();
    auto *wallet=m_lightWallets.value("bch",nullptr);QString path,helper,digest;QByteArray bytes;
    out.insert("selectionAvailable",bchOperationIdle() && !out.value("configured").toBool()
        && !context.isEmpty() && wallet && wallet->fundingConfiguration(path,bytes,helper,digest,"bch")
        && helper==m_lightSetupHelper && digest==m_lightSetupDigest && !helper.isEmpty());
    const bool allowed=bchOperationIdle() && boundContext(context,m_bchLightContext,out);out.insert("contextReady",allowed);
    if(!allowed)for(const auto key:{"termsAvailable","prepareAvailable","reviewAvailable","approvalAvailable"})out.insert(key,false);
    // Do not present old approval details as belonging to a changed admission
    // or wallet. A busy operation alone is not a context change.
    if(out.value("configured").toBool() && (context!=m_bchLightContext || !out.value("sourcesValid").toBool())) {
        for(const auto key:{"terms","review","result","retainedPayment","retainedReview"})out.insert(key,QVariantMap{});
        out.insert("state","context-mismatch");out.insert("valid",false);
    }
    if(!bchOperationIdle())out.insert("busy",true);
    return out;
}
bool DexEngine::useBchLightWallet() {
    if(!bchLightFundingStatus().value("selectionAvailable").toBool())return false;
    const auto context=m_bchAdmission->lightFundingContext();auto *wallet=m_lightWallets.value("bch",nullptr);
    QString path,helper,digest;QByteArray bytes;
    if(context.isEmpty() || !wallet || !wallet->fundingConfiguration(path,bytes,helper,digest,"bch")
        || helper!=m_lightSetupHelper || digest!=m_lightSetupDigest)return false;
    // A selected wrapper is immutable across restart. The helper owns the
    // authoritative source record and independently rejects mixed funding.
    bool configured=false;
    {
        QScopedValueRollback<bool> binding(m_bchLightBinding,true);
        const auto wrapper=BchLightFundingSetup::retain(context,path);if(wrapper.isEmpty())return false;
        m_bchLightContext=context;
        configured=m_bchLightFunding->configure(wrapper,wallet,helper,digest,
            context.value("tradeId").toString(),context.value("quoteCommitment").toString());
        if(!configured)m_bchLightContext={};
    }
    emit bchLightFundingChanged();emit bchPunishmentChanged();emit bchRefundWalletChanged();return configured;
}
bool DexEngine::selectBchLightFundingSource(){return bchLightFundingAllowed() && m_bchLightFunding->selectWalletSource();}
bool DexEngine::queryBchLightTerms(){return bchLightFundingAllowed() && m_bchLightFunding->queryTerms();}
bool DexEngine::prepareBchLightFunding(const QString &fee,int confirmations){return confirmations>0 && confirmations<=1000 && bchLightFundingAllowed() && m_bchLightFunding->prepare(fee,quint32(confirmations));}
bool DexEngine::reopenBchLightFunding(){return bchLightFundingAllowed() && m_bchLightFunding->reopen();}
bool DexEngine::reviewBchLightSignature(){return bchLightFundingAllowed() && m_bchLightFunding->reviewSignature();}
bool DexEngine::signBchLightFunding(const QString &commitment,const QString &password) {
    if(!bchLightFundingAllowed())return false;
    auto bytes=password.toUtf8();const bool started=m_bchLightFunding->sign(commitment,bytes);
    volatile char *data=bytes.data();for(int i=0;i<bytes.size();++i)data[i]=0;bytes.clear();return started;
}
bool DexEngine::reviewBchLightRecovery(){return bchLightFundingAllowed() && m_bchLightFunding->reviewRecovery();}
bool DexEngine::retainBchLightRecovery(const QString &commitment){return bchLightFundingAllowed() && m_bchLightFunding->retainRecovery(commitment);}
bool DexEngine::reviewBchLightSend(){return bchLightFundingAllowed() && m_bchLightFunding->reviewSend();}
bool DexEngine::sendBchLightFunding(const QString &commitment){return bchLightFundingAllowed() && m_bchLightFunding->send(commitment);}
bool DexEngine::observeBchLightFunding(){return bchLightFundingAllowed() && m_bchLightFunding->observe();}
bool DexEngine::reviewBchLightRetry(){return bchLightFundingAllowed() && m_bchLightFunding->reviewRetry();}
bool DexEngine::retryBchLightFunding(const QString &commitment){return bchLightFundingAllowed() && m_bchLightFunding->retry(commitment);}
bool DexEngine::reconcileBchLightAcknowledgement(){return bchLightFundingAllowed() && m_bchLightFunding->reconcileAcknowledgement();}
