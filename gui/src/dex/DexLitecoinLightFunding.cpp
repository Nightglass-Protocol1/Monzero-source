#include "DexDeployment.h"
#include "DexEngine.h"
#include "LitecoinLightFundingController.h"
#include "LitecoinLightFundingSetup.h"
#include "LitecoinNegotiationSession.h"
bool DexEngine::litecoinLightFundingSelected() const {
    if(m_ltcLightFunding->status().value("configured").toBool())return true;
    if(!m_ltcNegotiation)return false;
    const auto context=m_ltcNegotiation->lightFundingContext();if(context.isEmpty())return false;
    const auto path=context.value("metadataDirectory").toString()+"/"+context.value("tradeId").toString()+".ltc-light-wallet.json";
    return QFileInfo::exists(path) || QFileInfo(path).isSymLink();
}
bool DexEngine::litecoinLightFundingAllowed(bool peerRequired) {
    if(!litecoinOperationAllowed() || litecoinAppBusy() || !m_ltcNegotiation || m_ltcNegotiation->lightFundingContext().isEmpty())return false;
    const auto peer=m_ltcNegotiation->status(),funding=m_ltcLightFunding->status();
    if(peer.value("tradeId")!=funding.value("tradeId") || peer.value("quoteCommitment")!=funding.value("quoteCommitment"))return false;
    return !peerRequired || (peer.value("complete").toBool() && peer.value("transportAuthenticated").toBool() && !peer.value("busy").toBool());
}
QVariantMap DexEngine::litecoinLightFundingStatus() const {
    auto out=m_ltcLightFunding->status();const auto peer=litecoinNegotiationStatus();
    out.insert("selectionAvailable",!out.value("configured").toBool() && !m_lightSetupHelper.isEmpty()
        && swapLight()->status().value("walletSignConfigured").toBool() && swapLight()->status().value("network")==DexDeployment::litecoinLightNetwork()
        && !swapLight()->status().value("busy").toBool() && peer.value("role")=="litecoin-funder" && !litecoinAppBusy());
    out.insert("recoveryComplete",peer.value("recoveryComplete").toBool());
    out.insert("peerReady",peer.value("complete").toBool() && peer.value("transportAuthenticated").toBool() && !peer.value("busy").toBool());
    if(litecoinAppBusy())out.insert("busy",true);
    return litecoinAppView(out);
}
bool DexEngine::useLitecoinLightWallet() {
    if(!litecoinOperationAllowed() || !litecoinLightFundingStatus().value("selectionAvailable").toBool() || !m_ltcNegotiation)return false;
    const auto context=m_ltcNegotiation->lightFundingContext();QString path,helper,digest;QByteArray bytes;
    if(context.isEmpty() || !swapLight()->fundingConfiguration(path,bytes,helper,digest,DexDeployment::litecoinLightCoin()) || helper!=m_lightSetupHelper || digest!=m_lightSetupDigest)return false;
    const auto directory=context.value("metadataDirectory").toString(),trade=context.value("tradeId").toString();
    // A pre-existing recovery intent without our selected-wallet record belongs
    // to another funding path; never reserve a second set of wallet inputs.
    const auto selected=directory+"/"+trade+".ltc-light-wallet.json";
    const auto recovery=directory+"/"+trade+".ltc-recovery-intent";
    if(!QFileInfo::exists(selected) && (QFileInfo::exists(recovery) || QFileInfo(recovery).isSymLink()))return false;
    const auto wrapper=LitecoinLightFundingSetup::retain(context,path);if(wrapper.isEmpty())return false;
    const auto peer=m_ltcNegotiation->status();
    return m_ltcLightFunding->configure(wrapper,swapLight(),helper,digest,peer.value("tradeId").toString(),peer.value("quoteCommitment").toString());
}
bool DexEngine::queryLitecoinLightTerms(){return litecoinLightFundingAllowed() && m_ltcLightFunding->queryTerms();}
bool DexEngine::prepareLitecoinLightFunding(const QString &fee,int confirmations){return confirmations>0 && confirmations<=1000 && litecoinLightFundingAllowed(true) && m_ltcLightFunding->prepare(fee,quint32(confirmations));}
bool DexEngine::reopenLitecoinLightFunding(){return litecoinLightFundingAllowed(true) && m_ltcLightFunding->reopen();}
bool DexEngine::reviewLitecoinLightSignature(){return litecoinLightFundingAllowed() && m_ltcLightFunding->reviewSignature();}
bool DexEngine::signLitecoinLightFunding(const QString &commitment,const QString &password){
    if(!litecoinLightFundingAllowed())return false;
    auto bytes=password.toUtf8();const bool result=m_ltcLightFunding->sign(commitment,bytes);
    volatile char *data=bytes.data();for(int i=0;i<bytes.size();++i)data[i]=0;bytes.clear();return result;
}
bool DexEngine::reviewLitecoinLightSend(){return litecoinLightFundingAllowed() && m_ltcLightFunding->reviewSend();}
bool DexEngine::sendLitecoinLightFunding(const QString &commitment){return litecoinLightFundingAllowed() && m_ltcLightFunding->send(commitment);}
bool DexEngine::observeLitecoinLightFunding(){return litecoinLightFundingAllowed() && m_ltcLightFunding->observe();}
bool DexEngine::reviewLitecoinLightRetry(){return litecoinLightFundingAllowed() && m_ltcLightFunding->reviewRetry();}
bool DexEngine::retryLitecoinLightFunding(const QString &commitment){return litecoinLightFundingAllowed() && m_ltcLightFunding->retry(commitment);}

bool DexEngine::resumeLitecoinLightRecovery() {
    return litecoinLightFundingAllowed(true)
        && !m_ltcLightFunding->status().value("retainedReview").toMap().isEmpty()
        && m_ltcNegotiation->resumeRecovery();
}
