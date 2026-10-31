#include "LitecoinNegotiationSession.h"
#include "LitecoinJournalJob.h"
#include "LitecoinJournalProtocol.h"
#include "OfferIdentityStore.h"
#include "PrivateQuoteChannel.h"
#include <QJsonArray>
#include <QJsonDocument>

namespace {
bool encoded(const QString &value,int maximum,int exact=0) {
    if (value.isEmpty() || value.size()>maximum*2 || value.size()%2 || (exact && value.size()!=exact*2)) return false;
    for (const auto c:value) if (!QStringLiteral("0123456789abcdef").contains(c)) return false;
    return true;
}
QByteArray digest(const QByteArray &bytes) {return QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex();}
void erase(QByteArray &bytes) {sodium_memzero(bytes.data(),bytes.size());bytes.clear();}
}
bool LitecoinNegotiationSession::configureSettlement(const QString &path) {
    if (m_path.isEmpty() || !m_settlementPath.isEmpty() || m_settlementStarted || m_busy || m_failed) return false;
    const auto bytes=LitecoinJournalProtocol::privateConfig(path);const auto doc=QJsonDocument::fromJson(bytes);
    if (!doc.isObject() || doc.toJson(QJsonDocument::Compact)!=bytes) return false;
    const auto original=QJsonDocument::fromJson(m_config).object();auto c=doc.object();QString rpcPath;
    if (m_role=="litecoin-funder") {
        if (c.size()!=3 || c.value("schema")!="monzero-private-ltc-redeem-v1" || c.value("journalConfigFile")!=m_path
            || !c.value("rpcFile").isString()) return false;
        rpcPath=c.value("rpcFile").toString();c=original;
    } else {
        LitecoinJournalJob validator;
        if (c.value("schema")!="monzero-private-ltc-journal-v4" || !validator.configure(path)) return false;
        for (const auto key:{"metadataDirectory","snapshotDirectory","tradeId","role","policy","pins"})
            if (c.value(key)!=original.value(key)) return false;
        rpcPath=c.value("fundingRpcFile").toString();
    }
    const auto rpc=LitecoinJournalProtocol::privateConfig(rpcPath);const auto r=QJsonDocument::fromJson(rpc).object();
    if (rpc.isEmpty() || (m_role=="litecoin-funder" ? r.value("schema")!="monzero-private-ltc-redeem-rpc-v1"
        : r.value("schema")!="monzero-private-ltc-funding-rpc-v1" && r.value("schema")!="monzero-private-ltc-funding-rpc-v2")) return false;
    if (!BackendExecutable::matches(c.value("helper").toString(),c.value("helperDigest").toString())) return false;
    m_settlementPath=path;m_settlementProgram=c.value("helper").toString();m_settlementDigest=c.value("helperDigest").toString();
    m_settlementSources={{path,bytes},{rpcPath,rpc}};m_settlementState="configured";emit changed();return true;
}
bool LitecoinNegotiationSession::settlementSourcesMatch() const {
    if (m_settlementPath.isEmpty()) return m_settlementSources.isEmpty();
    if (!BackendExecutable::matches(m_settlementProgram,m_settlementDigest)) return false;
    for (auto it=m_settlementSources.cbegin();it!=m_settlementSources.cend();++it)
        if (LitecoinJournalProtocol::privateConfig(it.key())!=it.value()) return false;
    return true;
}
bool LitecoinNegotiationSession::settlementOperation() const {
    return QStringList{"funding-proof","funding-proof-accept","redeem-review","redeem-submit","redeem-accept"}.contains(m_operation);
}
bool LitecoinNegotiationSession::startRedemption(const QString &approvedFundingTxid) {
    if (m_settlementPath.isEmpty() || !m_recoveryComplete || m_failed || m_busy || !m_channel || !m_channel->ready()
        || m_settlementComplete || !sourcesMatch()) return false;
    if (m_role=="native-funder") {
        if (m_settlementStarted || !approvedFundingTxid.isEmpty()) return false;
        m_settlementStarted=true;m_settlementDeadline.start(180000);run("funding-proof",{{"safetyBlocks",2}});
    } else {
        if (!m_proofReceived || m_releaseApproved || approvedFundingTxid!=m_fundingTxid || !encoded(approvedFundingTxid,32,32)) return false;
        m_releaseApproved=true;m_releaseWasApproved=true;m_settlementDeadline.start(180000);run("redeem-review");
    }
    return !m_failed;
}
bool LitecoinNegotiationSession::restoreCompletedSettlement(const QString &fundingTxid,const QString &redemptionTxid) {
    if (m_settlementPath.isEmpty() || !m_complete || !m_recoveryComplete || m_failed || m_busy || m_settlementStarted
        || !encoded(fundingTxid,32,32) || !encoded(redemptionTxid,32,32) || !sourcesMatch()) return false;
    m_fundingTxid=fundingTxid;m_redemptionTxid=redemptionTxid;
    m_settlementStarted=true;m_settlementComplete=true;m_settlementState="complete";emit changed();return true;
}
bool LitecoinNegotiationSession::redeemFrameMatches(const QByteArray &bytes) const {
    if (bytes.isEmpty() || bytes.size()>16384) return false;
    const auto f=QJsonDocument::fromJson(bytes).object(),b=f.value("binding").toObject();
    const auto quote=b.value("quote").toArray();const auto expected=QByteArray::fromHex(m_quoteCommitment.toLatin1());
    if (quote.size()!=32) return false;
    for (int i=0;i<32;++i) if (!quote.at(i).isDouble() || quote.at(i).toDouble()!=static_cast<unsigned char>(expected.at(i))) return false;
    return f.size()==5 && f.value("schema")=="monzero-ltc-xmz-redeem-v1" && b.size()==7
        && b.value("schema")=="monzero-ltc-xmz-recovery-v1" && f.value("native_txid")==m_fundingTxid
        && encoded(f.value("redeem").toString(),32,32);
}
void LitecoinNegotiationSession::receiveSettlement(const QJsonObject &message) {
    if (m_settlementPath.isEmpty() || !m_recoveryComplete || !sourcesMatch()) {fail();return;}
    const auto kind=message.value("kind").toString();
    if (kind=="settlement-proof-ack") {
        if (m_role!="native-funder" || message.size()!=5 || !m_proofSent
            || message.value("digest").toString().toLatin1()!=m_proofDigest) {fail();return;}
        m_proofAcknowledged=true;m_settlementDeadline.stop();checkSettlementComplete();return;
    }
    if (kind=="settlement-redeem-ack") {
        if (m_role!="litecoin-funder" || message.size()!=5 || !m_redeemSent
            || message.value("digest").toString().toLatin1()!=m_redeemDigest) {fail();return;}
        m_redeemAcknowledged=true;checkSettlementComplete();return;
    }
    QJsonObject incoming;QByteArray receivedDigest;
    if (kind=="settlement-proof") {
        if (m_role!="litecoin-funder" || message.size()!=6 || !encoded(message.value("proofHex").toString(),16384)
            || !encoded(message.value("fundingTxid").toString(),32,32)) {fail();return;}
        incoming={{"proofHex",message.value("proofHex")},{"fundingTxid",message.value("fundingTxid")}};
        auto bytes=QByteArray::fromHex(message.value("proofHex").toString().toLatin1());receivedDigest=digest(bytes);erase(bytes);
        if (m_proofReceived) {
            if (receivedDigest!=m_proofDigest || message.value("fundingTxid")!=m_fundingTxid
                || !m_channel->send(wire("settlement-proof-ack",{{"digest",QString::fromLatin1(m_proofDigest)}}))) fail();
            return;
        }
    } else if (kind=="settlement-redeem") {
        if (m_role!="native-funder" || !m_proofSent || message.size()!=6
            || !encoded(message.value("frameHex").toString(),16384) || !encoded(message.value("signatureHex").toString(),64,64)
            || !redeemFrameMatches(QByteArray::fromHex(message.value("frameHex").toString().toLatin1()))) {fail();return;}
        incoming={{"frameHex",message.value("frameHex")},{"signatureHex",message.value("signatureHex")}};
        receivedDigest=digest(QByteArray::fromHex(message.value("frameHex").toString().toLatin1())
            +QByteArray::fromHex(message.value("signatureHex").toString().toLatin1()));
        if (m_redeemReceived) {
            if (receivedDigest!=m_redeemDigest || !m_channel->send(wire("settlement-redeem-ack",{{"digest",QString::fromLatin1(m_redeemDigest)}}))) fail();
            return;
        }
    } else {fail();return;}
    if ((!m_settlementIncoming.isEmpty() && m_settlementIncoming!=incoming)
        || (!m_settlementPending.isEmpty() && m_settlementPending!=incoming)) {fail();return;}
    if (m_settlementIncoming==incoming) return;
    m_settlementStarted=true;m_settlementPending=incoming;pumpSettlement();
}
void LitecoinNegotiationSession::pumpSettlement() {
    if (m_failed || m_busy || !m_recoveryComplete || !m_settlementStarted) return;
    if (!m_pending.isEmpty()) {pump();return;}
    if (!m_recoveryPending.isEmpty()) {pumpRecovery();return;}
    if (!m_settlementPending.isEmpty()) {
        m_settlementIncoming=m_settlementPending;m_settlementPending={};
        if (m_role=="litecoin-funder") run("funding-proof-accept",{{"proofHex",m_settlementIncoming.value("proofHex")}});
        else run("redeem-accept",m_settlementIncoming);
        return;
    }
    checkSettlementComplete();
}
void LitecoinNegotiationSession::finishSettlement(const QJsonObject &response) {
    if (!m_channel || !m_channel->ready() || !m_snapshot.value("keysReady").toBool()
        || !m_snapshot.value("recoveryIntentRetained").toBool()) {fail();return;}
    if (m_operation=="funding-proof") {
        const auto proof=response.value("proofHex").toString(),txid=response.value("fundingTxid").toString();
        if (response.size()!=5 || m_role!="native-funder" || !encoded(proof,16384) || !encoded(txid,32,32)
            || !m_snapshot.value("nativeFunding").toObject().value("attempted").toBool()) {fail();return;}
        auto bytes=QByteArray::fromHex(proof.toLatin1());m_proofDigest=digest(bytes);erase(bytes);
        m_fundingTxid=txid;m_proofSent=true;m_settlementState="waiting-for-funding-ack";
        if (!m_channel->send(wire("settlement-proof",{{"proofHex",proof},{"fundingTxid",txid}}))) {fail();return;}
    } else if (m_operation=="funding-proof-accept") {
        if (response.size()!=4 || m_role!="litecoin-funder" || m_settlementIncoming.isEmpty()
            || response.value("fundingTxid")!=m_settlementIncoming.value("fundingTxid")) {fail();return;}
        m_fundingTxid=response.value("fundingTxid").toString();
        auto bytes=QByteArray::fromHex(m_settlementIncoming.value("proofHex").toString().toLatin1());m_proofDigest=digest(bytes);erase(bytes);
        m_settlementIncoming={};m_proofReceived=true;m_settlementState="funding-verified";m_settlementDeadline.stop();
        if (!m_channel->send(wire("settlement-proof-ack",{{"digest",QString::fromLatin1(m_proofDigest)}}))) {fail();return;}
    } else if (m_operation=="redeem-review") {
        const auto frame=response.value("frameHex").toString();
        if (response.size()!=4 || m_role!="litecoin-funder" || !m_releaseApproved || !encoded(frame,16384)) {fail();return;}
        m_localFrame=QByteArray::fromHex(frame.toLatin1());
        if (!redeemFrameMatches(m_localFrame)) {fail();return;}
        m_redemptionTxid=QJsonDocument::fromJson(m_localFrame).object().value("redeem").toString();
        m_localSignature=m_identity->identity()->signLitecoinRedeem(m_localFrame);
        if (m_localSignature.size()!=64) {fail();return;}
        run("redeem-submit",{{"frameHex",frame},{"signatureHex",QString::fromLatin1(m_localSignature.toHex())}});return;
    } else if (m_operation=="redeem-submit") {
        if (response.size()!=5 || m_role!="litecoin-funder" || !m_releaseApproved
            || response.value("frameHex").toString().toLatin1()!=m_localFrame.toHex()
            || response.value("signatureHex").toString().toLatin1()!=m_localSignature.toHex()) {fail();return;}
        m_redeemDigest=digest(m_localFrame+m_localSignature);m_redeemSent=true;m_settlementState="waiting-for-redemption-ack";
        if (!m_channel->send(wire("settlement-redeem",{{"frameHex",response.value("frameHex")},{"signatureHex",response.value("signatureHex")}}))) {fail();return;}
        erase(m_localFrame);erase(m_localSignature);
    } else if (m_operation=="redeem-accept") {
        const auto frame=QByteArray::fromHex(m_settlementIncoming.value("frameHex").toString().toLatin1());
        if (response.size()!=4 || m_role!="native-funder" || m_settlementIncoming.isEmpty()
            || !m_snapshot.value("litecoinRedemption").toObject().value("prepared").toBool()
            || response.value("txid")!=QJsonDocument::fromJson(frame).object().value("redeem")) {fail();return;}
        m_redemptionTxid=response.value("txid").toString();
        m_redeemDigest=digest(frame+QByteArray::fromHex(m_settlementIncoming.value("signatureHex").toString().toLatin1()));
        m_settlementIncoming={};m_redeemReceived=true;
        if (!m_channel->send(wire("settlement-redeem-ack",{{"digest",QString::fromLatin1(m_redeemDigest)}}))) {fail();return;}
    } else {fail();return;}
    pumpSettlement();
}
void LitecoinNegotiationSession::checkSettlementComplete() {
    if (m_failed || m_busy) return;
    const bool complete=m_role=="native-funder" ? m_proofAcknowledged && m_redeemReceived : m_proofReceived && m_redeemAcknowledged;
    if (complete) {m_settlementComplete=true;m_settlementState="complete";m_settlementDeadline.stop();m_state="complete";}
    else if (m_role=="native-funder" && m_proofAcknowledged) m_settlementState="waiting-for-redemption";
    emit changed();
}
