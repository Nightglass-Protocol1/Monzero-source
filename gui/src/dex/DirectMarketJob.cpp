#include "DirectMarketJob.h"
#include "BackendExecutable.h"
#include "DexChains.h"
#include <sodium.h>
#include <algorithm>
#include <cmath>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <QProcessEnvironment>
#include <QRandomGenerator>
#include <QRegularExpression>

namespace {
const QString RequestSchema="monzero-xmz-evm-helper-request-v1",ResponseSchema="monzero-xmz-evm-helper-response-v1";
const QRegularExpression Hash("\\A0x[0-9a-f]{64}\\z"),Digits("\\A[0-9]{1,78}\\z"),HexQty("\\A0x[0-9a-f]{1,64}\\z");
bool cleanAbsolute(const QString &path) {return QDir::isAbsolutePath(path) && QDir::cleanPath(path)==path;}
// The chains a direct trade may use: each chain's own coin (no tokens)
bool directChain(qint64 id) {
    return id==1 || id==56 || id==11155111 || id==97 || id==31337
        || id==DexChains::SolMainnet || id==DexChains::SolDevnet || id==DexChains::SolLocal;
}
bool coinValid(const QJsonObject &c) {
    static const QRegularExpression address("\\A0x[0-9a-fA-F]{40}\\z");
    return c.size()==4 && c.value("chainId").isDouble() && directChain(qint64(c.value("chainId").toDouble()))
        && address.match(c.value("contract").toString()).hasMatch() && address.match(c.value("asset").toString()).hasMatch()
        && HexQty.match(c.value("value").toString()).hasMatch();
}
bool listingValid(const QJsonObject &signedListing) {
    const auto l=signedListing.value("listing").toObject();
    static const QRegularExpression address("\\A0x[0-9a-fA-F]{40}\\z");
    static const QRegularExpression pair("\\A(BNB|ETH|SOL|USDT|USDT-BSC)/(BNB|ETH|SOL|USDT|USDT-BSC)\\z");
    return signedListing.size()==2 && l.value("schema")=="monzero-dex-direct-listing/1" && Hash.match(l.value("id").toString()).hasMatch()
        && pair.match(l.value("pair").toString()).hasMatch() && address.match(l.value("maker").toString()).hasMatch()
        && coinValid(l.value("gives").toObject()) && coinValid(l.value("wants").toObject())
        && l.value("expiresAt").isDouble() && l.value("createdAt").isDouble();
}
QString readSmall(const QString &path,int limit=400) {
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.read(limit)).trimmed().left(200) : QString();
}
bool mark(const QString &path,const QByteArray &text) {
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly|QIODevice::Truncate) || f.write(text)!=text.size()) return false;
    f.close();return true;
}
}

DirectMarketJob::DirectMarketJob(QObject *parent):QObject(parent) {
    m_deadline.setSingleShot(true);
    connect(&m_deadline,&QTimer::timeout,this,[this]{finish(false);});
    connect(&m_process,&QProcess::started,this,[this]{
        const bool written=m_busy && m_process.write(m_input)==m_input.size();
        sodium_memzero(m_input.data(),m_input.size());m_input.clear();   // holds the trade keys
        if (!written) {finish(false);return;}
        m_process.closeWriteChannel();
    });
    connect(&m_process,&QProcess::readyReadStandardOutput,this,&DirectMarketJob::drain);
    connect(&m_process,&QProcess::readyReadStandardError,this,&DirectMarketJob::drain);
    connect(&m_process,&QProcess::errorOccurred,this,[this](QProcess::ProcessError){finish(false);});
    connect(&m_process,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,
        [this](int,QProcess::ExitStatus status){
            QPointer<DirectMarketJob> guard(this);drain();
            if (guard) finish(status==QProcess::NormalExit);
        });
}
DirectMarketJob::~DirectMarketJob() {
    disconnect(&m_process,nullptr,this,nullptr);
    if (m_process.state()!=QProcess::NotRunning) {m_process.kill();m_process.waitForFinished(1000);}
    sodium_memzero(m_input.data(),m_input.size());
    sodium_memzero(m_swapSeedHex.data(),m_swapSeedHex.size());sodium_memzero(m_evmKeyHex.data(),m_evmKeyHex.size());
}

bool DirectMarketJob::configure(const QJsonObject &c) {
    if (!m_tradesDir.isEmpty()) return false;
    static const QRegularExpression rpc("\\A(https://[A-Za-z0-9.-]+(:[0-9]{1,5})?(/[A-Za-z0-9._~/-]*)?|http://127\\.0\\.0\\.1:[0-9]{1,5})\\z");
    static const QRegularExpression relay("\\A(https://[A-Za-z0-9.-]+(:[0-9]{1,5})?(/[A-Za-z0-9._~/-]*)?|http://(127\\.0\\.0\\.1|localhost):[0-9]{1,5})\\z");
    const auto rpcs=c.value("rpcs").toObject();
    const auto relays=c.value("relays").toArray();
    if (!cleanAbsolute(c.value("helper").toString()) || !cleanAbsolute(c.value("tradesDir").toString())
        || rpcs.size()<2 || relays.isEmpty() || relays.size()>8 || !c.value("mainnet").isBool()) return false;
    for (auto i=rpcs.begin();i!=rpcs.end();++i) {
        bool ok=false;const qint64 id=i.key().toLongLong(&ok);
        if (!ok || !directChain(id) || !rpc.match(i.value().toString()).hasMatch()) return false;
    }
    for (const auto &r:relays) if (!relay.match(r.toString()).hasMatch()) return false;
    if (c.contains("localSolProgram") && !DexChains::solanaAddress(c.value("localSolProgram").toString())) return false;
    // USDT markets (Solana's, BNB Chain's): each on a chain with a node
    for (const auto &t:c.value("tokens").toArray()) {
        const qint64 m=qint64(t.toDouble());
        if (!DexChains::token(m) || !rpcs.contains(QString::number(DexChains::helperChain(m)))) return false;
    }
    if (!BackendExecutable::matches(c.value("helper").toString(),c.value("helperDigest").toString())) return false;
    m_config=c;m_program=c.value("helper").toString();m_digest=c.value("helperDigest").toString();m_tradesDir=c.value("tradesDir").toString();
    QDir().mkpath(m_tradesDir);QFile::setPermissions(m_tradesDir,QFileDevice::ReadOwner|QFileDevice::WriteOwner|QFileDevice::ExeOwner);
    scanFolders();
    m_state="ready";emit changed();return true;
}

void DirectMarketJob::setSwapKeys(const QByteArray &swapSeedHex,const QByteArray &evmKeyHex) {
    sodium_memzero(m_swapSeedHex.data(),m_swapSeedHex.size());sodium_memzero(m_evmKeyHex.data(),m_evmKeyHex.size());
    m_swapSeedHex=swapSeedHex;m_evmKeyHex=evmKeyHex;
}

QList<qint64> DirectMarketJob::chains() const {
    // The coins this market trades, by market id: each chain's own coin, and its USDT where enabled
    QList<qint64> out;
    for (const auto &k:m_config.value("rpcs").toObject().keys()) out.append(k.toLongLong());
    for (const auto &t:m_config.value("tokens").toArray()) out.append(qint64(t.toDouble()));
    std::sort(out.begin(),out.end());
    return out;
}

QString DirectMarketJob::tickerOf(qint64 id) {
    if (DexChains::usdt(id)) return QStringLiteral("USDT");
    if (DexChains::usdtBsc(id)) return QStringLiteral("USDT-BSC");
    return DexChains::solana(id) ? QStringLiteral("SOL") : (id==56 || id==97) ? QStringLiteral("BNB") : QStringLiteral("ETH");
}
int DirectMarketJob::decimalsOf(qint64 id) {return DexChains::decimals(id);}
QString DirectMarketJob::pairOf(qint64 a,qint64 b) {
    const auto x=tickerOf(a),y=tickerOf(b);
    return x<=y ? x+"/"+y : y+"/"+x;
}

// Our offers, takes and trades are folders under tradesDir
void DirectMarketJob::scanFolders() {
    QList<Folder> kept;
    const auto names=QDir(m_tradesDir).entryList(QDir::Dirs|QDir::NoDotAndDotDot,QDir::Name);
    for (const auto &name:names) {
        const auto path=m_tradesDir+"/"+name;
        const bool maker=QFile::exists(path+"/direct-listing.json"),taker=QFile::exists(path+"/direct-take.json");
        if (!maker && !taker) continue;
        Folder f;f.path=path;f.kind=maker ? "offer" : "take";
        QFile file(path+(maker ? "/direct-listing.json" : "/direct-take.json"));
        if (file.size()>65536 || !file.open(QIODevice::ReadOnly)) continue;
        const auto o=QJsonDocument::fromJson(file.read(65536)).object();
        f.listing=o.value("listing").toObject();
        if (!listingValid(f.listing)) continue;
        f.listingId=f.listing.value("listing").toObject().value("id").toString();
        const qint64 now=QDateTime::currentSecsSinceEpoch();
        const bool expired=qint64(f.listing.value("listing").toObject().value("expiresAt").toDouble())<=now;
        if (QFile::exists(path+"/direct-journal.json")) f.state="agreed";
        else if (maker && o.value("closed").toBool()) f.state="cancelled";
        else if (QFile::exists(path+"/cancelled") || QFile::exists(path+"/abandoned")) f.state="cancelled";
        else if (!maker && QFile::exists(path+"/refused")) {f.state="refused";f.detail=readSmall(path+"/refused");}
        else if (expired && (maker || !QFile::exists(path+"/direct-journal.json"))) f.state=maker ? "expired" : "open";
        else f.state="open";
        // What the helper said before (kept across scans in this session)
        for (const auto &old:m_folders) if (old.path==path) {f.trade=old.trade;if (f.detail.isEmpty() && f.state==old.state) f.detail=old.detail;}
        if (f.state=="agreed" && f.trade.value("done").toBool()) f.state="done";
        kept.append(f);
    }
    m_folders=kept;
}

DirectMarketJob::Folder *DirectMarketJob::find(const QString &path) {
    for (auto &f:m_folders) if (f.path==path) return &f;
    return nullptr;
}

QJsonObject DirectMarketJob::keys() const {
    return QJsonObject{{"swapSeedHex",QString::fromLatin1(m_swapSeedHex)},{"evmKeyHex",QString::fromLatin1(m_evmKeyHex)}};
}

QJsonObject DirectMarketJob::base(const QString &command,const QString &folder) const {
    QJsonObject r{{"schema",RequestSchema},{"command",command},{"tradeDir",folder.isEmpty() ? m_tradesDir : folder},
        {"relays",m_config.value("relays")},{"deadlineSecs",100}};
    QJsonObject direct{{"rpcs",m_config.value("rpcs")}};
    if (!m_swapSeedHex.isEmpty()) direct.insert("swapKeys",keys());
    r.insert("direct",direct);
    if (m_config.value("mainnet").toBool()) r.insert("directMainnet",true);
    if (m_config.contains("localSolProgram")) r.insert("localSolProgram",m_config.value("localSolProgram"));
    return r;
}

bool DirectMarketJob::refreshListings() {
    if (m_tradesDir.isEmpty()) return false;
    for (const auto &q:m_queue) if (q.request.value("command")=="directListings") return true;
    if (m_busy && m_command=="directListings") return true;
    return enqueue(base("directListings",{}),{});
}

bool DirectMarketJob::post(qint64 givesChain,const QString &givesValue,qint64 wantsChain,const QString &wantsValue,qint64 lifetimeSecs) {
    const auto list=chains();
    if (m_tradesDir.isEmpty() || !keysSet() || !list.contains(givesChain) || !list.contains(wantsChain) || tickerOf(givesChain)==tickerOf(wantsChain)
        || !Digits.match(givesValue).hasMatch() || !Digits.match(wantsValue).hasMatch() || lifetimeSecs<600 || lifetimeSecs>31*86400) return false;
    const auto folder=m_tradesDir+"/offer-"+QString::number(QRandomGenerator::system()->generate64(),16);
    auto r=base("directPost",folder);
    // The helper names a coin by its chain, and a token (USDT) besides
    QJsonObject offer{{"givesChainId",double(DexChains::helperChain(givesChain))},{"givesValue",givesValue},{"wantsChainId",double(DexChains::helperChain(wantsChain))},
        {"wantsValue",wantsValue},{"lifetimeSecs",double(lifetimeSecs)}};
    if (DexChains::token(givesChain)) offer.insert("givesToken","usdt");
    if (DexChains::token(wantsChain)) offer.insert("wantsToken","usdt");
    r.insert("directOffer",offer);
    return enqueue(r,folder,true);
}

bool DirectMarketJob::take(const QString &listingId) {
    const auto signedListing=m_signed.value(listingId);
    if (m_tradesDir.isEmpty() || !keysSet() || signedListing.isEmpty()) return false;
    for (const auto &f:m_folders) if (f.listingId==listingId) return false;
    // Both of its chains must be ones this wallet has a node for
    const auto l=signedListing.value("listing").toObject();
    const auto list=chains();
    const auto market=[](const QJsonObject &c){return DexChains::marketOf(qint64(c.value("chainId").toDouble()),c.value("asset").toString());};
    if (!list.contains(market(l.value("gives").toObject())) || !list.contains(market(l.value("wants").toObject()))) return false;
    const auto folder=m_tradesDir+"/take-"+listingId.mid(2,16);
    auto r=base("directTake",folder);r.insert("directListing",signedListing);
    return enqueue(r,folder,true);
}

bool DirectMarketJob::cancel(const QString &folder) {
    auto *f=find(folder);
    if (!f || f->state!="open" || !keysSet()) return false;
    if (f->kind=="take") {
        // Nothing of a take is locked until its keys are agreed: we simply stop waiting
        if ((m_busy && m_folder==folder) || QFile::exists(folder+"/direct-journal.json")) return false;
        for (int i=m_queue.size()-1;i>=0;--i) if (m_queue[i].folder==folder) m_queue.removeAt(i);
        if (!mark(folder+"/abandoned","stopped waiting for the maker\n")) return false;
        f->state="cancelled";emit changed();return true;
    }
    return enqueue(base("directCancel",folder),folder,true);
}

bool DirectMarketJob::approve(const QString &folder,const QString &commitment) {
    auto *f=find(folder);
    if (!f || f->state!="agreed" || !Hash.match(commitment).hasMatch() || f->trade.value("commitment")!=commitment) return false;
    auto r=base("directApprove",folder);r.insert("commitment",commitment);
    return enqueue(r,folder,true);
}

bool DirectMarketJob::payout(const QString &folder,const QString &address) {
    auto *f=find(folder);
    if (!f || f->state!="done" || !keysSet()) return false;
    // The chain it came in on: what we got after a swap, our own coin back after a refund
    const auto stage=f->trade.value("stage").toString();
    const auto leg=stage.contains("Refunded") ? f->trade.value("gives").toMap() : f->trade.value("gets").toMap();
    const qint64 chain=qint64(leg.value("chainId").toDouble());
    if (!stage.contains("Swapped") && !stage.contains("Refunded")) return false;
    if (!DexChains::address(chain,address.trimmed())) return false;
    auto r=base("directPayout",folder);r.insert("chainId",double(chain));r.insert("payoutAddress",address.trimmed());
    return enqueue(r,folder,true);
}

bool DirectMarketJob::step() {
    if (m_tradesDir.isEmpty() || !keysSet()) return false;
    bool any=false;
    for (const auto &f:m_folders) {
        QString command;
        if (f.state=="open") command=f.kind=="offer" ? "directServe" : "directTake";
        else if (f.state=="agreed") command="directTick";
        if (command.isEmpty()) continue;
        bool queued=m_busy && m_folder==f.path;
        for (const auto &q:m_queue) queued=queued || q.folder==f.path;
        if (queued) continue;
        any=enqueue(base(command,f.path),f.path) || any;
    }
    return any;
}

bool DirectMarketJob::enqueue(const QJsonObject &request,const QString &folder,bool urgent) {
    if (m_queue.size()>64) return false;
    if (urgent) m_queue.prepend({request,folder}); else m_queue.append({request,folder});
    if (!m_busy) next(); else emit changed();
    return true;
}

void DirectMarketJob::next() {
    if (m_busy || m_queue.isEmpty() || m_process.state()!=QProcess::NotRunning) return;
    if (!BackendExecutable::matches(m_program,m_digest)) {m_queue.clear();m_state="source-changed";emit changed();return;}
    const auto run=m_queue.takeFirst();
    m_command=run.request.value("command").toString();m_folder=run.folder;
    m_input=QJsonDocument(run.request).toJson(QJsonDocument::Compact);m_output.clear();m_errors=0;
    m_busy=true;m_state="running";
    QProcessEnvironment environment;environment.insert("LANG","C");
    m_process.setProcessEnvironment(environment);m_process.setProcessChannelMode(QProcess::SeparateChannels);
    m_deadline.start(120000);
    m_process.start(m_program,QStringList{});emit changed();
}

void DirectMarketJob::drain() {
    if (!m_busy) return;
    constexpr qint64 limit=8*1024*1024;
    m_process.setReadChannel(QProcess::StandardOutput);m_output+=m_process.read(limit+1-m_output.size());
    m_process.setReadChannel(QProcess::StandardError);m_errors+=m_process.read(4097-m_errors).size();
    if (m_output.size()>limit || m_errors>4096) finish(false);
}

void DirectMarketJob::finish(bool success) {
    if (!m_busy) return;
    m_busy=false;m_deadline.stop();sodium_memzero(m_input.data(),m_input.size());m_input.clear();
    if (m_process.state()!=QProcess::NotRunning) m_process.kill();
    const auto doc=QJsonDocument::fromJson(m_output);m_output.clear();
    const auto r=doc.object();
    const bool parsed=success && doc.isObject() && r.value("schema")==ResponseSchema && r.value("ok").isBool();
    if (!parsed || !r.value("ok").toBool()) {
        m_state=parsed ? "refused" : "failed";
        m_error=parsed ? r.value("error").toString().left(300) : QStringLiteral("no valid answer from the helper");
        // A folder's own step that failed: shown on it (a running trade simply tries again)
        if (auto *f=find(m_folder)) f->detail=m_error;
        // A post or take that never made its folder: nothing to show it on
        if (m_command=="directPost" || m_command=="directTake") scanFolders();
    } else {
        m_state="ok";m_error.clear();
        handle(r);
    }
    emit changed();
    // The next queued run (a QProcess may not be restarted from inside its own finished signal)
    QTimer::singleShot(0,this,&DirectMarketJob::next);
}

void DirectMarketJob::handle(const QJsonObject &r) {
    if (m_command=="directListings") {
        m_signed.clear();m_listings.clear();m_relayErrors.clear();
        for (const auto &v:r.value("listings").toArray()) {
            const auto entry=v.toObject(),s=entry.value("signed").toObject();
            if (!listingValid(s)) continue;
            const auto shownListing=shown(s,entry.value("relays").toArray().size());
            if (shownListing.isEmpty()) continue;
            m_signed.insert(shownListing.value("id").toString(),s);
            m_listings.append(shownListing);
        }
        for (const auto &e:r.value("relayErrors").toArray()) m_relayErrors.append(e.toString().left(200));
        return;
    }
    if (m_command=="directPost" || m_command=="directCancel") {
        if (m_command=="directCancel" && r.value("cancelled").toBool()) mark(m_folder+"/cancelled","withdrawn by the user\n");
        scanFolders();
        // Our new offer is on the book now
        if (m_command=="directPost") {for (const auto &q:m_queue) if (q.request.value("command")=="directListings") return;
            m_queue.append({base("directListings",{}),{}});}
        return;
    }
    if (m_command=="directPayout") {
        m_lastPayout=r.value("payout").toObject().toVariantMap();
        m_lastPayout.insert("folder",m_folder);
        if (auto *f=find(m_folder)) f->trade.insert("payout",r.value("payout").toObject().toVariantMap());
        return;
    }
    const bool first=!find(m_folder);
    if (first) scanFolders();
    auto *f=find(m_folder);
    if (!f) return;
    f->detail.clear();
    if (m_command=="directServe") {
        if (r.value("taken").toBool()) {f->state="agreed";emit agreed(f->path);}
        else if (r.value("expired").toBool()) f->state="expired";
        return;
    }
    if (m_command=="directTake") {
        if (r.value("agreed").toBool()) {f->state="agreed";f->trade=r.toVariantMap();emit agreed(f->path);}
        else if (r.value("refusedFinal").toBool()) {f->state="refused";f->detail=r.value("refused").toString().left(200);}
        else if (r.value("expired").toBool()) f->state="expired";
        else if (r.value("refused").isString()) f->detail=r.value("refused").toString().left(200);
        return;
    }
    // directStatus / directApprove / directTick: the trade as the helper sees it now
    auto trade=r.toVariantMap();trade.remove("schema");trade.remove("ok");
    if (!trade.contains("payout") || trade.value("payout").isNull()) trade.insert("payout",f->trade.value("payout"));
    f->trade=trade;
    f->state=trade.value("done").toBool() ? "done" : "agreed";
}

QVariantMap DirectMarketJob::shown(const QJsonObject &s,int relays) const {
    const auto l=s.value("listing").toObject(),gives=l.value("gives").toObject(),wants=l.value("wants").toObject();
    // Coins by market id: the chain's own coin, or its USDT
    const qint64 gc=DexChains::marketOf(qint64(gives.value("chainId").toDouble()),gives.value("asset").toString());
    const qint64 wc=DexChains::marketOf(qint64(wants.value("chainId").toDouble()),wants.value("asset").toString());
    const auto gv=hexToDecimal(gives.value("value").toString()),wv=hexToDecimal(wants.value("value").toString());
    if (gv.isEmpty() || wv.isEmpty() || tickerOf(gc)==tickerOf(wc)) return {};
    const auto pair=pairOf(gc,wc);
    // The book's view: an offer that gives the pair's base asks for the quote (a sell of the base)
    const bool sellsBase=tickerOf(gc)==pair.section('/',0,0);
    const auto baseUnits=sellsBase ? gv : wv,quoteUnits=sellsBase ? wv : gv;
    const qint64 bc=sellsBase ? gc : wc,qc=sellsBase ? wc : gc;
    return {{"id",l.value("id")},{"pair",pair},{"maker",l.value("maker")},{"side",sellsBase ? "sell" : "buy"},
        {"gives",QVariantMap{{"chainId",double(gc)},{"coin",tickerOf(gc)},{"units",gv},{"amount",unitsText(gv,decimalsOf(gc))}}},
        {"wants",QVariantMap{{"chainId",double(wc)},{"coin",tickerOf(wc)},{"units",wv},{"amount",unitsText(wv,decimalsOf(wc))}}},
        {"base",unitsText(baseUnits,decimalsOf(bc))},{"quote",unitsText(quoteUnits,decimalsOf(qc))},
        {"price",priceText(baseUnits,decimalsOf(bc),quoteUnits,decimalsOf(qc))},
        {"expiresAt",l.value("expiresAt").toDouble()},{"relays",relays}};
}

QVariantMap DirectMarketJob::shown(const Folder &f) const {
    auto l=shown(f.listing,0);
    // A take: we give what the maker wants, and get what it gives
    if (f.kind=="take") {const auto g=l.value("gives");l.insert("gives",l.value("wants"));l.insert("wants",g);}
    l.insert("folder",f.path);l.insert("kind",f.kind);l.insert("state",f.state);l.insert("detail",f.detail);l.insert("trade",f.trade);
    return l;
}

QVariantMap DirectMarketJob::listing(const QString &id) const {
    for (const auto &v:m_listings) if (v.toMap().value("id")==id) return v.toMap();
    return {};
}

QVariantMap DirectMarketJob::folder(const QString &path) const {
    for (const auto &f:m_folders) if (f.path==path) return shown(f);
    return {};
}

QVariantMap DirectMarketJob::status() const {
    QVariantList mine;
    for (const auto &f:m_folders) mine.append(shown(f));
    QVariantList chainList;
    for (const auto id:chains()) chainList.append(QVariantMap{{"chainId",double(id)},{"coin",tickerOf(id)},{"decimals",decimalsOf(id)}});
    QStringList pairs;
    const auto list=chains();
    for (const auto a:list) for (const auto b:list) if (tickerOf(a)<tickerOf(b)) pairs.append(pairOf(a,b));
    pairs.removeDuplicates();pairs.sort();
    return {{"configured",!m_tradesDir.isEmpty()},{"keys",keysSet()},{"busy",m_busy},{"queued",m_queue.size()},{"state",m_state},{"error",m_error},
        {"command",m_command},{"listings",m_listings},{"relayErrors",m_relayErrors},{"mine",mine},{"payout",m_lastPayout},
        {"chains",chainList},{"pairs",pairs},{"mainnet",m_config.value("mainnet").toBool()}};
}

QString DirectMarketJob::hexToDecimal(const QString &hex) {
    if (!HexQty.match(hex).hasMatch() || hex.size()>2+32) return {};
    quint64 hi=0,lo=0;
    for (const auto ch:hex.mid(2)) {
        const quint64 d=QString(ch).toUInt(nullptr,16);
        hi=(hi<<4)|(lo>>60);lo=(lo<<4)|d;
    }
    QString out;
    while (hi || lo) {   // divide the 128-bit value by 10
        const quint64 hq=hi/10,hr=hi%10;
        const unsigned __int128 rest=(static_cast<unsigned __int128>(hr)<<64)|lo;
        lo=quint64(rest/10);hi=hq;out.prepend(QChar('0'+int(rest%10)));
    }
    return out.isEmpty() ? QStringLiteral("0") : out;
}

QString DirectMarketJob::unitsText(const QString &digits,int places) {
    if (!Digits.match(digits).hasMatch()) return {};
    QString d=digits;while (d.size()<=places) d.prepend('0');
    QString whole=d.left(d.size()-places),frac=d.mid(d.size()-places);
    while (frac.endsWith('0')) frac.chop(1);
    return frac.isEmpty() ? whole : whole+"."+frac;
}

QString DirectMarketJob::priceText(const QString &baseUnits,int baseDecimals,const QString &quoteUnits,int quoteDecimals) {
    const double base=baseUnits.toDouble()/std::pow(10.0,baseDecimals),quote=quoteUnits.toDouble()/std::pow(10.0,quoteDecimals);
    if (!(base>0) || !(quote>0)) return {};
    const double price=quote/base;
    const int places=qBound(0,5-int(std::floor(std::log10(price))),18);
    QString text=QString::number(price,'f',places);
    if (text.contains('.')) {while (text.endsWith('0')) text.chop(1);if (text.endsWith('.')) text.chop(1);}
    return text;
}
