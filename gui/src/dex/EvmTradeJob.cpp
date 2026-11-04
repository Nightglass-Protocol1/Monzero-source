#include "EvmTradeJob.h"
#include "BackendExecutable.h"
#include "BchAdmissionProtocol.h"
#include "LitecoinJournalProtocol.h"
#include <QDir>
#include <QJsonDocument>
#include <QPointer>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSet>

namespace {
const QString RequestSchema="monzero-xmz-evm-helper-request-v1",ResponseSchema="monzero-xmz-evm-helper-response-v1";
bool cleanAbsolute(const QString &path) {return QDir::isAbsolutePath(path) && QDir::cleanPath(path)==path;}
bool endpoint(const QJsonValue &value) {
    const auto e=value.toObject();
    static const QRegularExpression local("\\Ahttp://(127\\.0\\.0\\.1|localhost):[0-9]{1,5}\\z");
    for (auto i=e.begin();i!=e.end();++i) if (i.key()!="url" && i.key()!="loginFile") return false;
    return value.isObject() && local.match(e.value("url").toString()).hasMatch()
        && (!e.contains("loginFile") || cleanAbsolute(e.value("loginFile").toString()));
}
bool configValid(const QJsonObject &c) {
    const QSet<QString> required{"schema","helper","helperDigest","tradeDir","evmRpc","accountIndex","mainWallet","swapWallet"};
    for (const auto &key:required) if (!c.contains(key)) return false;
    for (auto i=c.begin();i!=c.end();++i) if (!required.contains(i.key()) && i.key()!="evmLogChunk") return false;
    static const QRegularExpression rpc("\\A(https://[A-Za-z0-9.-]+(:[0-9]{1,5})?(/[A-Za-z0-9._~/-]*)?|http://127\\.0\\.0\\.1:[0-9]{1,5})\\z");
    const auto index=c.value("accountIndex").toDouble(-1);
    return c.value("schema")=="monzero-private-xmz-evm-trade-v1" && cleanAbsolute(c.value("helper").toString())
        && cleanAbsolute(c.value("tradeDir").toString()) && rpc.match(c.value("evmRpc").toString()).hasMatch()
        && index>=0 && index<2147483648.0 && index==double(qint64(index))
        && (!c.contains("evmLogChunk") || (c.value("evmLogChunk").toDouble()>=1 && c.value("evmLogChunk").toDouble()<=100000))
        && endpoint(c.value("mainWallet")) && endpoint(c.value("swapWallet"));
}
// What the app may show: the journal view, nothing that could carry a secret.
bool snapshotValid(const QJsonObject &r) {
    static const QRegularExpression hash("\\A0x[0-9a-f]{64}\\z"),stage("\\A[A-Za-z]{1,32}(\\([A-Za-z]{1,16}\\))?\\z");
    static const QRegularExpression address("\\A0x[0-9a-fA-F]{40}\\z"),wei("\\A[0-9]{1,78}\\z");
    static const QSet<QString> allowed{"schema","ok","role","stage","done","approved","commitment","offer","deadlines",
        "evmTxs","xmzLock","xmzSweeps","awaitingApproval","account","awaitingFunding","payout"};
    for (auto i=r.begin();i!=r.end();++i) if (!allowed.contains(i.key())) return false;
    const auto offer=r.value("offer").toObject();
    if (r.contains("account") && !address.match(r.value("account").toString()).hasMatch()) return false;
    if (r.contains("awaitingFunding") && !r.value("awaitingFunding").isNull()) {
        const auto f=r.value("awaitingFunding").toObject();
        if (f.size()!=3) return false;
        for (const auto &k:{"neededWei","haveWei","missingWei"}) if (!wei.match(f.value(k).toString()).hasMatch()) return false;
    }
    if (r.contains("payout")) {
        const auto p=r.value("payout").toObject();
        if (!address.match(p.value("to").toString()).hasMatch()) return false;
        const bool sent=p.contains("tx");
        if (sent ? !(hash.match(p.value("tx").toString()).hasMatch() && wei.match(p.value("amountWei").toString()).hasMatch() && p.size()==3)
                 : !(p.value("sent")==false && p.value("reason").isString() && p.size()==3)) return false;
    }
    return (r.value("role")=="owner" || r.value("role")=="claimer") && stage.match(r.value("stage").toString()).hasMatch()
        && r.value("done").isBool() && r.value("approved").isBool() && hash.match(r.value("commitment").toString()).hasMatch()
        && r.value("offer").isObject() && hash.match(offer.value("tradeId").toString()).hasMatch()
        && r.value("evmTxs").isObject() && (!r.contains("awaitingApproval") || r.value("awaitingApproval").isBool());
}
}

EvmTradeJob::EvmTradeJob(QObject *parent):QObject(parent) {
    m_deadline.setSingleShot(true);
    connect(&m_deadline,&QTimer::timeout,this,[this]{finish(false);});
    connect(&m_process,&QProcess::started,this,[this]{
        const bool written=m_busy && m_process.write(m_input)==m_input.size();
        m_input.fill('\0');m_input.clear();   // may hold the wallet seed
        if (!written) {finish(false);return;}
        m_process.closeWriteChannel();
    });
    connect(&m_process,&QProcess::readyReadStandardOutput,this,&EvmTradeJob::drain);
    connect(&m_process,&QProcess::readyReadStandardError,this,&EvmTradeJob::drain);
    connect(&m_process,&QProcess::errorOccurred,this,[this](QProcess::ProcessError){finish(false);});
    connect(&m_process,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,
        [this](int,QProcess::ExitStatus status){
            QPointer<EvmTradeJob> guard(this);drain();
            if (guard) finish(status==QProcess::NormalExit);   // a refusal exits 1 with ok:false
        });
}
EvmTradeJob::~EvmTradeJob() {
    disconnect(&m_process,nullptr,this,nullptr);
    if (m_process.state()!=QProcess::NotRunning) {m_process.kill();m_process.waitForFinished(1000);}
    m_input.fill('\0');
}
bool EvmTradeJob::configure(const QString &path) {
    if (!m_path.isEmpty() || m_busy) return false;
    const auto bytes=LitecoinJournalProtocol::privateConfig(path);QJsonObject config;
    if (!BchAdmissionProtocol::canonicalObject(bytes,&config) || !configValid(config)
        || !BackendExecutable::matches(config.value("helper").toString(),config.value("helperDigest").toString())) return false;
    m_path=path;m_source=bytes;m_config=config;m_program=config.value("helper").toString();
    m_digest=config.value("helperDigest").toString();m_tradeDir=config.value("tradeDir").toString();
    m_state="ready";emit changed();return true;
}
bool EvmTradeJob::sourceMatches() const {
    return !m_path.isEmpty() && LitecoinJournalProtocol::privateConfig(m_path)==m_source && BackendExecutable::matches(m_program,m_digest);
}
QVariantMap EvmTradeJob::status() const {
    return {{"configured",!m_path.isEmpty()},{"busy",m_busy},{"state",m_state},{"error",m_error},{"tradeId",m_trade},
        {"commitment",m_commitment},{"awaitingApproval",m_awaitingApproval},{"snapshot",m_snapshot.toVariantMap()},
        {"approvalPending",!m_pendingApproval.isEmpty()},{"stepError",m_stepError},
        {"canApprove",!m_commitment.isEmpty() && !m_snapshot.value("approved").toBool() && m_pendingApproval.isEmpty()},
        {"account",m_snapshot.value("account").toString()},{"awaitingFunding",m_funding},{"payout",m_payout}};
}
bool EvmTradeJob::refresh() {
    return run({{"schema",RequestSchema},{"command","status"},{"tradeDir",m_tradeDir}});
}
bool EvmTradeJob::approve(const QString &commitment) {
    // Only what the user was shown for this trade
    if (m_commitment.isEmpty() || commitment!=m_commitment) return false;
    // A step is running (they take a while): send the approval as soon as it ends
    if (m_busy) {m_pendingApproval=commitment;emit changed();return true;}
    return run({{"schema",RequestSchema},{"command","approve"},{"tradeDir",m_tradeDir},{"commitment",commitment}});
}
bool EvmTradeJob::tick(QByteArray &walletSeed) {
    QJsonObject t;
    if (!tickConfig(walletSeed,&t)) return false;
    return run({{"schema",RequestSchema},{"command","tick"},{"tradeDir",m_tradeDir},{"tick",t}});
}
bool EvmTradeJob::payout(QByteArray &walletSeed,const QString &address) {
    // Only a finished trade, to an address the user entered (the helper checks its checksum)
    static const QRegularExpression format("\\A0x[0-9a-fA-F]{40}\\z");
    if (!m_snapshot.value("done").toBool() || !format.match(address).hasMatch()) {walletSeed.fill('\0');walletSeed.clear();return false;}
    QJsonObject t;
    if (!tickConfig(walletSeed,&t)) return false;
    return run({{"schema",RequestSchema},{"command","payout"},{"tradeDir",m_tradeDir},{"tick",t},{"payoutAddress",address}});
}
bool EvmTradeJob::tickConfig(QByteArray &walletSeed,QJsonObject *out) const {
    const bool valid=walletSeed.size()>=16 && walletSeed.size()<=64;
    QByteArray seedHex=walletSeed.toHex();walletSeed.fill('\0');walletSeed.clear();
    if (!valid) {seedHex.fill('\0');return false;}
    QJsonObject t{{"evmRpc",m_config.value("evmRpc")},{"walletSeedHex",QString::fromLatin1(seedHex)},
        {"accountIndex",m_config.value("accountIndex")},{"mainWallet",m_config.value("mainWallet")},{"swapWallet",m_config.value("swapWallet")}};
    seedHex.fill('\0');
    if (m_config.contains("evmLogChunk")) t.insert("evmLogChunk",m_config.value("evmLogChunk"));
    *out=t;return true;
}
bool EvmTradeJob::run(const QJsonObject &request) {
    if (m_busy || m_path.isEmpty() || m_process.state()!=QProcess::NotRunning) return false;
    if (!sourceMatches()) {m_state="source-changed";emit changed();return false;}
    m_command=request.value("command").toString();
    m_input=QJsonDocument(request).toJson(QJsonDocument::Compact);m_output.clear();m_errors=0;
    m_busy=true;m_state="running";m_error.clear();
    QProcessEnvironment environment;environment.insert("LANG","C");
    m_process.setProcessEnvironment(environment);m_process.setProcessChannelMode(QProcess::SeparateChannels);
    m_deadline.start(m_command=="tick" ? 300000 : 30000);
    m_process.start(m_program,QStringList{});emit changed();return true;
}
void EvmTradeJob::drain() {
    if (!m_busy) return;
    m_process.setReadChannel(QProcess::StandardOutput);m_output+=m_process.read(65537-m_output.size());
    m_process.setReadChannel(QProcess::StandardError);m_errors+=m_process.read(4097-m_errors).size();
    if (m_output.size()>65536 || m_errors>4096) finish(false);
}
void EvmTradeJob::finish(bool success) {
    if (!m_busy) return;
    m_busy=false;m_deadline.stop();m_input.fill('\0');m_input.clear();
    if (m_process.state()!=QProcess::NotRunning) m_process.kill();
    if (m_output.endsWith('\n')) m_output.chop(1);
    QJsonObject r;
    const bool parsed=success && BchAdmissionProtocol::canonicalObject(m_output,&r) && r.value("schema")==ResponseSchema && r.value("ok").isBool();
    m_output.clear();
    // A queued approval is dropped when the step fails: the user approves again after reading the error
    if (!parsed || !r.value("ok").toBool()) {
        m_pendingApproval.clear();
        m_state=parsed ? "refused" : "failed";
        m_error=parsed ? r.value("error").toString().left(300) : QStringLiteral("no valid answer from the helper");
        // A failed step leaves the trade where it was: show its current stage (status needs no
        // network) next to the error, which stays until a step succeeds
        if (m_command=="tick") {m_stepError=m_error;QTimer::singleShot(0,this,[this]{refresh();});}
        emit changed();return;
    }
    const auto trade=r.value("offer").toObject().value("tradeId").toString(),commitment=r.value("commitment").toString();
    if (!snapshotValid(r) || (!m_trade.isEmpty() && (trade!=m_trade || commitment!=m_commitment))) {
        m_pendingApproval.clear();m_state="mismatch";m_error="the helper answered for another trade";emit changed();return;
    }
    m_trade=trade;m_commitment=commitment;m_snapshot=r;
    m_awaitingApproval=m_command=="tick" && r.value("awaitingApproval").toBool();
    // What still has to reach the trade's account before it can lock (tick answers only)
    if (m_command=="tick") m_funding=r.value("awaitingFunding").isObject() ? r.value("awaitingFunding").toObject().toVariantMap() : QVariantMap{};
    if (m_command=="payout") m_payout=r.value("payout").toObject().toVariantMap();
    if (m_command=="tick") m_stepError.clear();
    m_state="ok";emit changed();
    if (!m_pendingApproval.isEmpty()) {
        const auto pending=m_pendingApproval;m_pendingApproval.clear();
        if (m_snapshot.value("approved").toBool()) emit changed();
        else if (!approve(pending)) {m_state="refused";m_error="the approval could not be sent";emit changed();}
    }
}
