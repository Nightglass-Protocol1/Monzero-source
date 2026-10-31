#include "DexDeployment.h"
#include "LitecoinNegotiationSession.h"
#include "LitecoinJournalJob.h"
#include "DexUtils.h"
#include "LitecoinJournalProtocol.h"
#include "OfferIdentityStore.h"
#include "PrivateQuoteChannel.h"
#include <QJsonDocument>
#include <QJsonArray>
#include <QProcessEnvironment>
#include <QUuid>
#include <QThread>

namespace {
void wipe(QByteArray &bytes) { sodium_memzero(bytes.data(),bytes.size());bytes.clear(); }
bool hex(const QString &value,int maximum,int exact=0) {
    if (value.isEmpty() || value.size()>maximum*2 || value.size()%2 || (exact && value.size()!=exact*2)) return false;
    for (const auto c:value) if (!QStringLiteral("0123456789abcdef").contains(c)) return false;
    return true;
}
bool psbtEncoding(const QByteArray &value) {
    if (value.isEmpty() || value.size()>100000) return false;
    const auto decoded=QByteArray::fromBase64(value);
    return decoded.startsWith(QByteArray::fromHex("70736274ff")) && decoded.toBase64()==value;
}
QByteArray deliveryDigest(const QByteArray &frame,const QByteArray &signature) {
    return QCryptographicHash::hash(frame+signature,QCryptographicHash::Sha256).toHex();
}
}
LitecoinNegotiationSession::LitecoinNegotiationSession(QObject *parent):QObject(parent) {
    m_settlementDeadline.setSingleShot(true);
    connect(&m_settlementDeadline,&QTimer::timeout,this,&LitecoinNegotiationSession::fail);
    m_keepalive.setInterval(60000);
    connect(&m_keepalive,&QTimer::timeout,this,[this] {
        if (!m_failed && m_started && m_channel && m_channel->ready()) m_channel->send(wire("keepalive",{}));
    });
    m_processDeadline.setSingleShot(true);m_ackDeadline.setSingleShot(true);m_recoveryDeadline.setSingleShot(true);
    connect(&m_recoveryDeadline,&QTimer::timeout,this,&LitecoinNegotiationSession::fail);
    connect(&m_processDeadline,&QTimer::timeout,this,&LitecoinNegotiationSession::fail);
    connect(&m_ackDeadline,&QTimer::timeout,this,&LitecoinNegotiationSession::fail);
    connect(&m_process,&QProcess::started,this,[this] {
        if (m_process.write(m_input)!=m_input.size()) {fail();return;}
        m_process.closeWriteChannel();wipe(m_input);
    });
    connect(&m_process,&QProcess::readyReadStandardOutput,this,&LitecoinNegotiationSession::drain);
    connect(&m_process,&QProcess::readyReadStandardError,this,&LitecoinNegotiationSession::drain);
    connect(&m_process,&QProcess::errorOccurred,this,[this](QProcess::ProcessError){fail();});
    connect(&m_process,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[this](int code,QProcess::ExitStatus exit) {
        QPointer<LitecoinNegotiationSession> guard(this);const auto request=m_request;
        drain();if (guard && request==m_request) finish(code==0 && exit==QProcess::NormalExit && m_errors==0);
    });
}
LitecoinNegotiationSession::~LitecoinNegotiationSession() {
    disconnect(&m_process,nullptr,this,nullptr);
    if (m_process.state()!=QProcess::NotRunning) {m_process.kill();m_process.waitForFinished(1000);}
    wipe(m_input);wipe(m_output);wipe(m_localFrame);wipe(m_localSignature);
}
bool LitecoinNegotiationSession::configure(const QString &path,const QString &identityFile,const QByteArray &passphrase) {
    if (!m_path.isEmpty() || m_started) return false;
    LitecoinJournalJob validator;
    if (!validator.configure(path) || !validator.status().value("startConfigured").toBool()) return false;
    const auto config=LitecoinJournalProtocol::privateConfig(path);const auto c=QJsonDocument::fromJson(config).object();
    const auto quote=LitecoinJournalProtocol::privateConfig(c.value("startQuoteFile").toString());
    const auto envelope=QJsonDocument::fromJson(quote).object();const auto payload=envelope.value("payload").toString().toUtf8();
    const auto terms=QJsonDocument::fromJson(payload).object();
    auto identity=std::unique_ptr<OfferIdentityStore>(new OfferIdentityStore(identityFile,passphrase,OfferIdentityStore::Mode::OpenExisting));
    const auto role=c.value("role").toString();
    if (config.isEmpty() || quote.isEmpty() || c.value("schema")!="monzero-private-ltc-journal-v5"
        || terms.value("schema")!="monzero-native-ltc-xmz-quote-v2" || !identity->ready()
        || identity->identity()->publicKey().toHex()!=terms.value(role=="litecoin-funder"?"ltcFunder":"xmzFunder").toString().toLatin1()) return false;
    m_path=path;m_config=config;m_quote=quote;m_quotePath=c.value("startQuoteFile").toString();
    m_program=c.value("helper").toString();m_digest=c.value("helperDigest").toString();
    m_trade=c.value("tradeId").toString();m_role=role;m_terms=terms;m_identity=std::move(identity);
    QByteArray binding=terms.value("schema").toString().toUtf8();binding.append('\0');binding+=payload;
    m_quoteCommitment=QString::fromLatin1(QCryptographicHash::hash(binding,QCryptographicHash::Sha256).toHex());
    m_state="configured";emit changed();return true;
}
bool LitecoinNegotiationSession::attach(PrivateQuoteChannel &channel) {
    if (m_path.isEmpty() || m_channel || m_started || channel.thread()!=thread()
        || channel.profile()!=PrivateQuoteChannel::Profile::LitecoinNegotiation || channel.failed()) return false;
    m_channel=&channel;
    connect(&channel,&PrivateQuoteChannel::authenticated,this,[this]{if (m_started) run("negotiation-status");});
    connect(&channel,&PrivateQuoteChannel::received,this,&LitecoinNegotiationSession::receive);
    m_keepalive.start();
    // A lost or refused connection is recorded apart from protocol failures, so
    // the owner may rebuild the session the way an app restart would.
    const auto lost=[this]{
        if (!m_failed) m_transportLost=true;
        if (!m_complete || (m_recoveryStarted && !m_recoveryComplete) || (m_settlementStarted && !m_settlementComplete)) fail();else emit changed();
    };
    connect(&channel,&PrivateQuoteChannel::closed,this,lost);
    connect(&channel,&PrivateQuoteChannel::rejected,this,[this]{if (!m_failed) m_transportLost=true;fail();});
    connect(&channel,&QObject::destroyed,this,lost);
    return true;
}
void LitecoinNegotiationSession::abort() { fail(); }
bool LitecoinNegotiationSession::waitForConnection() {
    if (m_started || m_failed || m_channel || !sourcesMatch()) return false;
    m_state="waiting-for-connection";emit changed();return true;
}
bool LitecoinNegotiationSession::start() {
    if (m_started || !m_channel || !sourcesMatch() || m_channel->failed()) return false;
    m_started=true;m_state="connecting";
    if (m_channel->ready()) run("negotiation-status");else emit changed();
    return true;
}
QJsonObject LitecoinNegotiationSession::lightFundingContext() const {
    if(m_role!="litecoin-funder" || !sourcesMatch())return {};
    const auto config=QJsonDocument::fromJson(m_config).object();
    if(!DexDeployment::litecoinNetworkAllowed(config.value("policy").toObject().value("ltc_network").toString()))return {};
    QJsonObject result{{"schema","monzero-private-ltc-light-dex-v1"}};
    for(const auto key:{"tradeId","metadataDirectory","snapshotDirectory","pins","policy"})result.insert(key,config.value(key));
    return result;
}
QVariantMap LitecoinNegotiationSession::status() const {
    const bool settlementReady=!m_settlementPath.isEmpty() && m_recoveryComplete && !m_failed && !m_busy && m_channel && m_channel->ready();
    return {{"settlementConfigured",!m_settlementPath.isEmpty()},{"settlementState",m_settlementState},
        {"settlementStarted",m_settlementStarted},{"settlementComplete",m_settlementComplete},
        {"nativeFundingTxid",m_fundingTxid},{"redemptionTxid",m_redemptionTxid},
        {"litecoinLockedAmount",DexUtils::formatAtomic(m_terms.value("ltcAtoms").toString().toULongLong(),8)},
        {"litecoinRedemptionFee",DexUtils::formatAtomic(m_terms.value("ltcRedeemFeeAtoms").toString().toULongLong(),8)},
        {"litecoinRedemptionDestination",m_terms.value("ltcPayout").toString()},
        {"canShareFunding",settlementReady && m_role=="native-funder" && !m_settlementStarted},
        {"canReleaseRedemption",settlementReady && m_role=="litecoin-funder" && m_proofReceived && !m_releaseApproved},
        {"recoveryStarted",m_recoveryStarted},{"recoveryState",m_recoveryState},{"recoveryComplete",m_recoveryComplete},
        {"configured",!m_path.isEmpty()},{"attached",!m_channel.isNull()},{"state",m_state},{"busy",m_busy},
        {"keysReady",m_snapshot.value("keysReady").toBool()},{"peerAcknowledged",m_acknowledged},
        {"failedDuring",m_failedDuring},{"proofSent",m_proofSent},{"releaseApproved",m_releaseApproved},{"recoveryAgreed",m_recoveryAgreed},{"releaseWasApproved",m_releaseWasApproved},{"complete",m_complete},{"transportAuthenticated",m_channel && m_channel->ready() && !m_failed},{"transportLost",m_transportLost},
        {"tradeId",m_trade},{"role",m_role},{"quoteCommitment",m_quoteCommitment},{"executable",false},{"chainObserved",false}};
}
bool LitecoinNegotiationSession::resumeRecovery() {
    const auto context=lightFundingContext();if(context.isEmpty())return false;
    const auto path=context.value("metadataDirectory").toString()+"/"+m_trade+".ltc-recovery-intent";
    const auto bytes=LitecoinJournalProtocol::privateRecord(path,131072);
    const auto document=QJsonDocument::fromJson(bytes);if(!document.isObject())return false;
    const auto intent=document.object();
    if(intent.size()!=5 || intent.value("schema")!="monzero-ltc-recovery-intent-v1"
        || !intent.value("checkpoint").isArray() || !intent.value("policy").isObject()
        || !intent.value("admitted_at").isDouble() || !intent.value("psbt").isString())return false;
    // The existing recovery-review helper validates these untrusted saved bytes
    // against the exact committed checkpoint before requesting an identity signature.
    // No wallet reservation, fresh agreement or broadcast is requested here.
    return startRecovery(intent.value("psbt").toString().toLatin1());
}
bool LitecoinNegotiationSession::startRecovery(const QByteArray &psbt) {
    if (!m_complete || m_failed || m_busy || m_recoveryStarted || m_role!="litecoin-funder"
        || !m_channel || !m_channel->ready() || !sourcesMatch() || !psbtEncoding(psbt)) return false;
    m_recoveryPsbt=psbt;m_recoveryStarted=true;m_recoveryState="preparing";m_recoveryDeadline.start(180000);
    pumpRecovery();return !m_failed;
}
bool LitecoinNegotiationSession::recoveryFrameMatches(const QByteArray &bytes,bool local) const {
    if (bytes.isEmpty() || bytes.size()>16384) return false;
    const auto f=QJsonDocument::fromJson(bytes).object();const auto binding=f.value("binding").toObject();
    const auto quote=binding.value("quote").toArray();const auto expected=QByteArray::fromHex(m_quoteCommitment.toLatin1());
    if (quote.size()!=32) return false;
    for (int i=0;i<32;++i) if (!quote.at(i).isDouble() || quote.at(i).toDouble()!=static_cast<unsigned char>(expected.at(i))) return false;
    const int role=m_role=="litecoin-funder"?0:1;
    return f.size()==5 && binding.size()==7 && binding.value("schema")=="monzero-ltc-xmz-recovery-v1"
        && f.value("role").isDouble() && f.value("role").toDouble()==(local?role:1-role);
}
void LitecoinNegotiationSession::receiveRecovery(const QJsonObject &message) {
    if (message.value("kind")=="recovery-ack") {
        if (message.size()!=5 || !m_recoverySent || message.value("digest").toString().toLatin1()!=m_recoveryDigest) {fail();return;}
        m_recoveryAcknowledged=true;checkRecoveryComplete();return;
    }
    const auto psbt=message.value("psbt").toString().toLatin1();
    if (message.value("kind")!="recovery-frame" || message.size()!=7 || !psbtEncoding(psbt)
        || !hex(message.value("frameHex").toString(),16384) || !hex(message.value("signatureHex").toString(),64,64)
        || !recoveryFrameMatches(QByteArray::fromHex(message.value("frameHex").toString().toLatin1()),false)
        || (m_recoveryStarted && psbt!=m_recoveryPsbt) || (!m_recoveryStarted && m_role!="native-funder")) {fail();return;}
    const QJsonObject incoming{{"frameHex",message.value("frameHex")},{"signatureHex",message.value("signatureHex")}};
    if ((!m_recoveryPeer.isEmpty() && m_recoveryPeer!=incoming)
        || (!m_recoveryPending.isEmpty() && m_recoveryPending!=incoming)
        || (!m_recoveryIncoming.isEmpty() && m_recoveryIncoming!=incoming)) {fail();return;}
    if (!m_recoveryStarted) {m_recoveryStarted=true;m_recoveryPsbt=psbt;m_recoveryState="preparing";m_recoveryDeadline.start(180000);}
    if (m_recoveryIncoming==incoming) return;
    m_recoveryPending=incoming;pumpRecovery();
}
void LitecoinNegotiationSession::pumpRecovery() {
    if (!m_complete || !m_recoveryStarted || m_busy || m_failed) return;
    if (!m_pending.isEmpty()) {pump();return;}
    if (!m_recoverySent) {run("recovery-review",{{"psbt",QString::fromLatin1(m_recoveryPsbt)}});return;}
    if (!m_recoveryPending.isEmpty()) {
        m_recoveryIncoming=m_recoveryPending;m_recoveryPending={};run("recovery-accept",m_recoveryIncoming);return;
    }
    m_recoveryState="waiting-for-peer";checkRecoveryComplete();
}
void LitecoinNegotiationSession::checkRecoveryComplete() {
    if (m_failed) return;
    if (m_recoveryReceived && m_recoveryAcknowledged && !m_busy && m_recoveryPending.isEmpty()) {
        m_recoveryDeadline.stop();m_recoveryState="complete";m_state="complete";
        if (!m_recoveryComplete) {m_recoveryComplete=true;m_recoveryAgreed=true;QPointer<LitecoinNegotiationSession> guard(this);
            emit changed();if (guard) emit recoveryCompleted();if (guard) pumpSettlement();return;}
    }
    QPointer<LitecoinNegotiationSession> guard(this);emit changed();if (guard) pumpSettlement();
}
bool LitecoinNegotiationSession::sourcesMatch() const {
    return !m_path.isEmpty() && LitecoinJournalProtocol::privateConfig(m_path)==m_config
        && LitecoinJournalProtocol::privateConfig(m_quotePath)==m_quote && BackendExecutable::matches(m_program,m_digest) && settlementSourcesMatch();
}
bool LitecoinNegotiationSession::boundSnapshot(const QJsonObject &s) const {
    if (!LitecoinJournalProtocol::snapshotValid(s,m_trade,m_role) || s.value("quoteCommitment")!=m_quoteCommitment) return false;
    for (const auto key:{"ltcNetwork","xmzGenesis","ltcAtoms","xmzAtoms","ltcPayout","ltcRefund","xmzPayout","xmzRefund"})
        if (s.value(key)!=m_terms.value(key)) return false;
    return true;
}
bool LitecoinNegotiationSession::frameMatches(const QByteArray &bytes,bool local) const {
    if (bytes.isEmpty() || bytes.size()>200000) return false;
    const auto f=QJsonDocument::fromJson(bytes).object();
    const int role=m_role=="litecoin-funder"?0:1;
    return f.size()==6 && f.value("schema")=="monzero-ltc-xmz-key-exchange-v1"
        && f.value("swapId")==m_trade && f.value("quoteCommitment")==m_quoteCommitment
        && f.value("sequence").isDouble() && f.value("sequence").toDouble()==(local?role:1-role)
        && f.value("previous").isString() && hex(f.value("previous").toString(),32,32) && f.value("share").isObject();
}
void LitecoinNegotiationSession::run(const QString &operation,const QJsonObject &fields) {
    if (m_failed || m_busy || m_process.state()!=QProcess::NotRunning || !sourcesMatch()) {fail();return;}
    m_operation=operation;m_request=QUuid::createUuid().toString(QUuid::WithoutBraces);
    auto request=fields;request.insert("schema","monzero-ltc-"+operation+"-request-v1");request.insert("requestId",m_request);
    m_input=QJsonDocument(request).toJson(QJsonDocument::Compact);wipe(m_output);m_errors=0;m_busy=true;
    m_state=operation;if (operation.startsWith("recovery-")) m_recoveryState=operation;
    if (settlementOperation()) m_settlementState=operation;
    m_processDeadline.start(180000);
    // A seller using light servers verifies them against the platform trust store.
    QProcessEnvironment environment;environment.insert("LANG","C");const auto system=QProcessEnvironment::systemEnvironment();
    for (const auto name:{"SSL_CERT_FILE","SSL_CERT_DIR"}) if (system.contains(name)) environment.insert(name,system.value(name));
    m_process.setProcessEnvironment(environment);
    m_process.setProcessChannelMode(QProcess::SeparateChannels);
    const bool settlement=settlementOperation() && operation!="redeem-accept";
    m_process.start(settlement?m_settlementProgram:m_program,{"--"+operation+"-stdio-v1","--config-file",settlement?m_settlementPath:m_path});emit changed();
}
void LitecoinNegotiationSession::drain() {
    if (!m_busy || m_failed) return;
    m_process.setReadChannel(QProcess::StandardOutput);m_output+=m_process.read(410001-m_output.size());
    m_process.setReadChannel(QProcess::StandardError);m_errors+=m_process.read(1025-m_errors).size();
    if (m_output.size()>410000 || m_errors>1024) fail();
}
QByteArray LitecoinNegotiationSession::wire(const QString &kind,const QJsonObject &fields) const {
    auto message=fields;message.insert("schema","monzero-ltc-private-negotiation-v1");message.insert("kind",kind);
    message.insert("tradeId",m_trade);message.insert("quoteCommitment",m_quoteCommitment);
    return QJsonDocument(message).toJson(QJsonDocument::Compact);
}
void LitecoinNegotiationSession::finish(bool success) {
    if (!m_busy || m_failed) return;
    m_busy=false;m_processDeadline.stop();
    const auto doc=QJsonDocument::fromJson(m_output);const auto response=doc.object();
    const auto snapshot=response.value("snapshot").toObject();
    if (!success || !sourcesMatch() || !doc.isObject() || doc.toJson(QJsonDocument::Compact)!=m_output
        || response.value("schema")!="monzero-ltc-"+m_operation+"-response-v1" || response.value("requestId")!=m_request
        || !boundSnapshot(snapshot)) {fail();return;}
    if (m_operation.startsWith("recovery-")) {
        auto expected=m_snapshot;expected.insert("recoveryIntentRetained",true);
        if (!snapshot.value("keysReady").toBool() || snapshot!=expected) {fail();return;}
    }
    wipe(m_output);m_snapshot=snapshot;
    if (settlementOperation()) {finishSettlement(response);return;}
    if (m_operation=="recovery-review") {
        const auto encoded=response.value("frameHex").toString();
        if (response.size()!=4 || !hex(encoded,16384) || !snapshot.value("keysReady").toBool()
            || !snapshot.value("recoveryIntentRetained").toBool()) {fail();return;}
        m_localFrame=QByteArray::fromHex(encoded.toLatin1());
        if (!recoveryFrameMatches(m_localFrame,true)) {fail();return;}
        m_localSignature=m_identity->identity()->signLitecoinRecovery(m_localFrame);
        if (m_localSignature.size()!=64) {fail();return;}
        run("recovery-submit",{{"frameHex",encoded},{"signatureHex",QString::fromLatin1(m_localSignature.toHex())}});return;
    }
    if (m_operation=="recovery-submit") {
        if (response.size()!=5 || response.value("frameHex").toString().toLatin1()!=m_localFrame.toHex()
            || response.value("signatureHex").toString().toLatin1()!=m_localSignature.toHex()
            || !m_channel || !m_channel->ready()) {fail();return;}
        m_recoveryDigest=deliveryDigest(m_localFrame,m_localSignature);m_recoverySent=true;
        if (!m_channel->send(wire("recovery-frame",{{"psbt",QString::fromLatin1(m_recoveryPsbt)},
            {"frameHex",response.value("frameHex")},{"signatureHex",response.value("signatureHex")}}))) {fail();return;}
        wipe(m_localFrame);wipe(m_localSignature);pumpRecovery();return;
    }
    if (m_operation=="recovery-accept") {
        if (response.size()!=3 || m_recoveryIncoming.isEmpty() || !m_channel || !m_channel->ready()) {fail();return;}
        const auto digest=deliveryDigest(QByteArray::fromHex(m_recoveryIncoming.value("frameHex").toString().toLatin1()),
            QByteArray::fromHex(m_recoveryIncoming.value("signatureHex").toString().toLatin1()));
        m_recoveryPeer=m_recoveryIncoming;m_recoveryIncoming={};m_recoveryReceived=true;
        if (!m_channel->send(wire("recovery-ack",{{"digest",QString::fromLatin1(digest)}}))) {fail();return;}
        pumpRecovery();return;
    }
    if (m_operation=="negotiation-status") {
        const auto n=response.value("negotiation").toObject();const auto next=n.value("next").toString();
        if (response.size()!=4 || n.size()!=2 || !n.value("outgoingRetained").isBool()
            || !QStringList{"local","peer","complete"}.contains(next)
            || (next=="complete")!=snapshot.value("keysReady").toBool()
            || (next=="complete" && !n.value("outgoingRetained").toBool())) {fail();return;}
        m_next=next;m_retained=n.value("outgoingRetained").toBool();pump();return;
    }
    if (m_operation=="outgoing-review") {
        const auto encoded=response.value("frameHex").toString();
        if (response.size()!=4 || !hex(encoded,200000)) {fail();return;}
        m_localFrame=QByteArray::fromHex(encoded.toLatin1());
        if (!frameMatches(m_localFrame,true)) {fail();return;}
        m_localSignature=m_identity->identity()->signLitecoinNegotiation(m_localFrame);
        if (m_localSignature.size()!=64) {fail();return;}
        run("outgoing-submit",{{"frameHex",encoded},{"signatureHex",QString::fromLatin1(m_localSignature.toHex())}});return;
    }
    if (m_operation=="outgoing-submit") {
        if (response.size()!=5 || response.value("frameHex").toString().toLatin1()!=m_localFrame.toHex()
            || response.value("signatureHex").toString().toLatin1()!=m_localSignature.toHex()
            || !m_channel || !m_channel->ready()) {fail();return;}
        m_localDigest=deliveryDigest(m_localFrame,m_localSignature);m_sent=true;m_retained=true;
        if (!m_channel->send(wire("frame",{{"frameHex",response.value("frameHex")},{"signatureHex",response.value("signatureHex")}}))) {fail();return;}
        wipe(m_localFrame);wipe(m_localSignature);m_ackDeadline.start(180000);pump();return;
    }
    if (m_operation=="peer-accept") {
        if (response.size()!=4 || !hex(response.value("checkpoint").toString(),32,32) || m_incoming.isEmpty()
            || !m_channel || !m_channel->ready()) {fail();return;}
        const auto digest=deliveryDigest(QByteArray::fromHex(m_incoming.value("frameHex").toString().toLatin1()),
            QByteArray::fromHex(m_incoming.value("signatureHex").toString().toLatin1()));
        m_incoming={};
        if (!m_channel->send(wire("ack",{{"digest",QString::fromLatin1(digest)}}))) {fail();return;}
        run("negotiation-status");return;
    }
    fail();
}
void LitecoinNegotiationSession::receive(const QByteArray &bytes) {
    if (m_failed || !m_started || !m_channel || !m_channel->ready() || bytes.size()>410000) {fail();return;}
    const auto doc=QJsonDocument::fromJson(bytes);const auto message=doc.object();
    if (!doc.isObject() || doc.toJson(QJsonDocument::Compact)!=bytes || message.value("schema")!="monzero-ltc-private-negotiation-v1"
        || message.value("tradeId")!=m_trade || message.value("quoteCommitment")!=m_quoteCommitment) {fail();return;}
    if (message.value("kind")=="keepalive") {if (message.size()!=4) fail();return;}
    if (message.value("kind").toString().startsWith("settlement-")) {receiveSettlement(message);return;}
    if (message.value("kind").toString().startsWith("recovery-")) {receiveRecovery(message);return;}
    if (message.value("kind")=="ack") {
        if (message.size()!=5 || !m_sent || message.value("digest").toString().toLatin1()!=m_localDigest) {fail();return;}
        m_acknowledged=true;m_ackDeadline.stop();checkComplete();return;
    }
    if (message.value("kind")!="frame" || message.size()!=6 || !hex(message.value("frameHex").toString(),200000)
        || !hex(message.value("signatureHex").toString(),64,64)
        || !frameMatches(QByteArray::fromHex(message.value("frameHex").toString().toLatin1()),false)) {fail();return;}
    const QJsonObject incoming{{"frameHex",message.value("frameHex")},{"signatureHex",message.value("signatureHex")}};
    if ((!m_pending.isEmpty() && m_pending!=incoming) || (!m_incoming.isEmpty() && m_incoming!=incoming)) {fail();return;}
    if (m_incoming==incoming) return;
    m_pending=incoming;pump();
}
void LitecoinNegotiationSession::pump() {
    if (m_busy || m_failed) return;
    if (!m_pending.isEmpty()) {m_incoming=m_pending;m_pending={};run("peer-accept",m_incoming);return;}
    if (!m_sent && (m_retained || m_next=="local")) {run("outgoing-review");return;}
    m_state=m_snapshot.value("keysReady").toBool()?"waiting-for-acknowledgement":"waiting-for-peer";
    checkComplete();
}
void LitecoinNegotiationSession::checkComplete() {
    if (m_failed) return;
    if (m_snapshot.value("keysReady").toBool() && m_acknowledged && !m_busy && m_pending.isEmpty()) {
        if (!m_complete) {m_complete=true;m_state="complete";QPointer<LitecoinNegotiationSession> guard(this);
            emit changed();if (guard) emit completed();if (guard) pumpRecovery();return;}
        m_state="complete";
    }
    if (m_complete && m_recoveryStarted) {pumpRecovery();return;}
    emit changed();
}
void LitecoinNegotiationSession::fail() {
    if (m_failed) return;
    m_failedDuring=m_busy?m_operation:m_state;
    m_settlementComplete=false;m_releaseApproved=false;m_settlementDeadline.stop();
    if (m_settlementStarted) m_settlementState="unknown";
    m_settlementPending={};m_settlementIncoming={};
    m_recoveryComplete=false;if (m_recoveryStarted) m_recoveryState="unknown";m_recoveryDeadline.stop();
    m_failed=true;m_busy=false;m_complete=false;m_state="unknown";m_processDeadline.stop();m_ackDeadline.stop();m_keepalive.stop();
    if (m_process.state()!=QProcess::NotRunning) m_process.kill();
    wipe(m_input);wipe(m_output);wipe(m_localFrame);wipe(m_localSignature);m_pending={};m_incoming={};
    QPointer<LitecoinNegotiationSession> guard(this);
    if (m_channel) m_channel->close();if (guard) emit changed();
}
