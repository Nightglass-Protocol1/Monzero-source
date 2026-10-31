#include "DexTradeFolders.h"
#include <QDateTime>
#include "DexDeployment.h"
#include "DexEngine.h"
#include "LitecoinAppConfig.h"
#include "LitecoinJournalJob.h"
#include "LitecoinJournalProtocol.h"
#include "LitecoinPeerStartup.h"
#include "LitecoinNegotiationSession.h"
#include "LitecoinReservationJob.h"
#include "LitecoinRedemptionJob.h"
#include "LitecoinLightFundingController.h"

bool DexEngine::configureLitecoinApp(const QString &path,const QString &expectedDigest) {
    if (!m_ltcAppSources.isEmpty() || m_ltcAppFailed || m_ltcJournal->status().value("attached").toBool()
        || m_ltcNegotiation || m_ltcRedemption->status().value("configured").toBool()
        || m_ltcReservation->status().value("configured").toBool()) return false;
    LitecoinAppConfig config;
    if (!LitecoinAppConfig::load(path,config) || (!expectedDigest.isEmpty() && config.digest()!=expectedDigest)) return false;
    m_ltcAppSources=config.sources;
    const auto failed=[this] {m_ltcAppFailed=true;emit litecoinJournalChanged();return false;};
    if (!m_ltcJournal->configure(config.journal)) return failed();
    m_ltcPayment=new LitecoinJournalJob(this);
    if (!m_ltcPayment->configure(config.payment)) return failed();
    connect(m_ltcPayment,&LitecoinJournalJob::changed,this,&DexEngine::litecoinJournalChanged);
    if (!config.funding.isEmpty()) {
        m_ltcFunding=new LitecoinJournalJob(this);
        if (!m_ltcFunding->configure(config.funding)) return failed();
        if (!m_ltcReservation->configure(config.funding,config.journal)) return failed();
        connect(m_ltcFunding,&LitecoinJournalJob::changed,this,&DexEngine::litecoinJournalChanged);
    }
    m_ltcAppPeer=new LitecoinPeerStartup(*this,this);
    if (!m_ltcAppPeer->configure(config.peer,config.journal,path)) return failed();
    if (!config.reservation.isEmpty() && !m_ltcReservation->configure(config.reservation,config.journal)) return failed();
    emit litecoinJournalChanged();return true;
}
#ifdef MONZERO_DEX_TESTING
bool DexEngine::dropLitecoinTransportForTesting() {return m_ltcAppPeer && m_ltcAppPeer->dropTransportForTesting();}
int DexEngine::litecoinReconnectsForTesting() const {return m_ltcAppPeer?m_ltcAppPeer->reconnects():0;}
#endif
LitecoinJournalJob *DexEngine::litecoinFundingJob() const {return m_ltcFunding?m_ltcFunding:m_ltcJournal;}
LitecoinJournalJob *DexEngine::litecoinPaymentJob() const {return m_ltcPayment?m_ltcPayment:m_ltcJournal;}
bool DexEngine::litecoinAppBusy() const {
    return m_ltcLightFunding->status().value("busy").toBool() || m_ltcJournal->status().value("busy").toBool()
        || (m_ltcFunding && m_ltcFunding->status().value("busy").toBool())
        || (m_ltcPayment && m_ltcPayment->status().value("busy").toBool())
        || m_ltcReservation->status().value("busy").toBool() || m_ltcRedemption->status().value("busy").toBool()
        || (m_ltcNegotiation && m_ltcNegotiation->status().value("busy").toBool());
}
bool DexEngine::litecoinOperationAllowed() {
    if (m_ltcAppFailed) return false;
    if (m_ltcAppSources.isEmpty()) return !m_ltcLightFunding->status().value("busy").toBool();
    for (auto it=m_ltcAppSources.cbegin();it!=m_ltcAppSources.cend();++it) {
        if (LitecoinJournalProtocol::privateConfig(it.key())!=it.value()) {
            m_ltcAppFailed=true;
            if (m_ltcNegotiation) m_ltcNegotiation->abort();
            emit litecoinJournalChanged();emit litecoinRedemptionChanged();emit litecoinReservationChanged();
            return false;
        }
    }
    return !litecoinAppBusy();
}
QVariantMap DexEngine::litecoinAppView(QVariantMap value) const {
    if (m_ltcAppSources.isEmpty()) return value;
    if (m_ltcAppFailed) return {{"attached",true},{"configured",true},{"valid",false},{"busy",false},
        {"state","unknown"},{"checkState","unknown"},{"snapshot",QVariantMap{}},{"executable",false}};
    const bool busy=litecoinAppBusy();value.insert("busy",busy);
    if (busy) {
        for (auto it=value.begin();it!=value.end();++it)
            if (it.key().endsWith("Available") || it.key().startsWith("can")) it.value()=false;
    }
    return value;
}
QVariantMap DexEngine::litecoinAppStatus() const {
    return {{"configured",!m_ltcAppSources.isEmpty()},{"state",m_ltcAppFailed?"unknown":m_ltcAppSources.isEmpty()?"unconfigured":"configured"},
        {"busy",!m_ltcAppSources.isEmpty() && litecoinAppBusy()},{"fundingConfigured",m_ltcFunding!=nullptr},
        {"paymentConfigured",m_ltcPayment!=nullptr},{"executable",false}};
}
QVariantMap DexEngine::litecoinFundingStatus() const {
    if (!m_ltcAppSources.isEmpty() && !m_ltcFunding) return {{"attached",false},{"busy",false},{"valid",false}};
    return litecoinAppView(litecoinFundingJob()->status());
}
QVariantMap DexEngine::litecoinPaymentStatus() const {return litecoinAppView(litecoinPaymentJob()->status());}
bool DexEngine::requestLitecoinPhaseCheck(const QString &phase) {
    if (!litecoinOperationAllowed() || m_ltcAppSources.isEmpty()) return false;
    if (phase=="funding") return m_ltcFunding && m_ltcFunding->refresh();
    if (phase=="payment") return m_ltcPayment && m_ltcPayment->refresh();
    return false;
}

bool DexEngine::openTradeFolders(const QString &root,const QString &requested) {
    if (!m_tradeRoot.isEmpty()) return false;
    m_tradeRoot=root;
    const auto trades=DexTradeFolders::scan(root);
    const auto network=DexDeployment::litecoinNetwork();
    const DexTradeFolders::Trade *selected=nullptr;int candidates=0;
    for (const auto &trade:trades) {
        if (!requested.isEmpty()) {
            if (trade.tradeId==requested) selected=&trade;
        } else if (trade.state=="active" && trade.network==network) {++candidates;selected=&trade;}
    }
    bool ok=true;QString failed;
    if (!requested.isEmpty() && (!selected || selected->state=="invalid" || selected->network!=network)) ok=false;
    else if (selected && (requested.isEmpty() ? candidates==1 : true)) {
        bool bound=true;
        if (!selected->setupDigest.isEmpty()) {
            const auto review=LitecoinAppConfig::review(selected->appConfig);
            bound=review.value("commitment")==selected->setupDigest && review.value("tradeId")==selected->tradeId
                && review.value("network")==selected->network;
        }
        bool loaded=bound && configureLitecoinApp(selected->appConfig,selected->setupDigest);
        LitecoinAppConfig attached;attached.sources=m_ltcAppSources;
        if (loaded && !selected->setupDigest.isEmpty() && attached.digest()!=selected->setupDigest) {
            m_ltcAppFailed=true;
            if (m_ltcNegotiation) m_ltcNegotiation->abort();
            loaded=false;
        }
        if (loaded) m_loadedTradeId=selected->tradeId;
        else {failed=selected->tradeId;ok=requested.isEmpty();} // never block wallet startup on an automatic reopen
    }
    m_tradeFolders=DexTradeFolders::describe(trades,m_loadedTradeId);
    for (auto &entry:m_tradeFolders) {
        auto map=entry.toMap();
        if (map.value("tradeId")==failed) {map["state"]="load-failed";entry=map;}
    }
    emit tradeFoldersChanged();return ok;
}
bool DexEngine::closeLoadedTrade(bool confirmed) {
    if (!confirmed || m_loadedTradeId.isEmpty() || litecoinAppBusy()
        || !DexTradeFolders::close(m_tradeRoot,m_loadedTradeId)) return false;
    m_tradeFolders=DexTradeFolders::describe(DexTradeFolders::scan(m_tradeRoot),m_loadedTradeId);
    emit tradeFoldersChanged();return true;
}

QVariantMap DexEngine::reviewLitecoinTradeSetup(const QString &appPath) {
    m_ltcSetupReview.clear();m_ltcSetupReviewTime=0;
    if (m_tradeRoot.isEmpty() || !m_ltcAppSources.isEmpty() || m_ltcAppFailed || litecoinAppBusy()) return {};
    m_ltcSetupReview=LitecoinAppConfig::review(appPath);
    if (!m_ltcSetupReview.isEmpty()) m_ltcSetupReviewTime=QDateTime::currentSecsSinceEpoch();
    return m_ltcSetupReview;
}

bool DexEngine::importLitecoinTradeSetup(const QString &appPath,const QString &commitment,bool confirmed) {
    const auto reviewed=m_ltcSetupReview;const auto reviewedAt=m_ltcSetupReviewTime;
    m_ltcSetupReview.clear();m_ltcSetupReviewTime=0;
    const auto now=QDateTime::currentSecsSinceEpoch();
    if (!confirmed || reviewed.isEmpty() || reviewed.value("appPath")!=appPath || reviewed.value("commitment")!=commitment
        || now<reviewedAt || now-reviewedAt>120 || m_tradeRoot.isEmpty() || !m_ltcAppSources.isEmpty()
        || m_ltcAppFailed || litecoinAppBusy() || LitecoinAppConfig::review(appPath)!=reviewed) return false;
    const auto trade=reviewed.value("tradeId").toString();
    if (!DexTradeFolders::create(m_tradeRoot,trade,"LTC/XMZ",reviewed.value("network").toString(),appPath,commitment)) return false;
    bool loaded=configureLitecoinApp(appPath,commitment);
    LitecoinAppConfig attached;attached.sources=m_ltcAppSources;
    if (loaded && attached.digest()!=commitment) {
        m_ltcAppFailed=true;
        if (m_ltcNegotiation) m_ltcNegotiation->abort();
        loaded=false;
    }
    if (loaded) m_loadedTradeId=trade;
    m_tradeFolders=DexTradeFolders::describe(DexTradeFolders::scan(m_tradeRoot),m_loadedTradeId);
    if (!loaded) for (auto &entry:m_tradeFolders) {
        auto value=entry.toMap();if(value.value("tradeId")==trade){value["state"]="load-failed";entry=value;}
    }
    emit tradeFoldersChanged();emit litecoinJournalChanged();
    if (loaded) requestLitecoinJournalCheck();
    return loaded;
}

bool DexEngine::registerConfiguredLitecoinApp(const QString &root,const QString &appPath) {
    if (root.isEmpty() || m_ltcAppSources.isEmpty() || m_ltcAppFailed || !m_loadedTradeId.isEmpty()
        || (!m_tradeRoot.isEmpty() && m_tradeRoot!=root)) return false;
    const auto review=LitecoinAppConfig::review(appPath);
    LitecoinAppConfig attached;attached.sources=m_ltcAppSources;
    if (review.isEmpty() || review.value("commitment")!=attached.digest()) return false;
    const auto id=review.value("tradeId").toString(),network=review.value("network").toString(),digest=attached.digest();
    bool exists=false;
    for (const auto &trade:DexTradeFolders::scan(root)) if (trade.tradeId==id) {
        if (trade.state=="invalid" || trade.appConfig!=appPath || trade.network!=network || trade.setupDigest!=digest) return false;
        exists=true;
    }
    if (!exists && !DexTradeFolders::create(root,id,"LTC/XMZ",network,appPath,digest)) return false;
    m_tradeRoot=root;m_loadedTradeId=id;
    m_tradeFolders=DexTradeFolders::describe(DexTradeFolders::scan(root),m_loadedTradeId);
    emit tradeFoldersChanged();return true;
}
