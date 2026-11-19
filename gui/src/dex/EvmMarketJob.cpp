#include "DexChains.h"
#include "EvmMarketJob.h"
#include <sodium.h>
#include <cmath>
#include <algorithm>
#include "BackendExecutable.h"
#include "LitecoinJournalProtocol.h"
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <QProcessEnvironment>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSet>

namespace {
const QString RequestSchema="monzero-xmz-evm-helper-request-v1",ResponseSchema="monzero-xmz-evm-helper-response-v1";
const QRegularExpression Hash("\\A0x[0-9a-f]{64}\\z"),Digits("\\A[0-9]{1,78}\\z"),HexQty("\\A0x[0-9a-f]{1,64}\\z");
bool cleanAbsolute(const QString &path) {return QDir::isAbsolutePath(path) && QDir::cleanPath(path)==path;}
bool localEndpoint(const QJsonValue &value) {
    const auto e=value.toObject();
    static const QRegularExpression local("\\Ahttp://(127\\.0\\.0\\.1|localhost):[0-9]{1,5}\\z");
    for (auto i=e.begin();i!=e.end();++i) if (i.key()!="url" && i.key()!="loginFile") return false;
    return value.isObject() && local.match(e.value("url").toString()).hasMatch()
        && (!e.contains("loginFile") || cleanAbsolute(e.value("loginFile").toString()));
}
bool configValid(const QJsonObject &c) {
    const QSet<QString> required{"schema","helper","helperDigest","tradesDir","evmRpc","chainId","accountIndex","mainWallet","swapWallet","relays"};
    for (const auto &key:required) if (!c.contains(key)) return false;
    for (auto i=c.begin();i!=c.end();++i) if (!required.contains(i.key()) && i.key()!="evmLogChunk" && i.key()!="localContract" && i.key()!="localSolProgram" && i.key()!="localSolUsdtMint") return false;
    static const QRegularExpression rpc("\\A(https://[A-Za-z0-9.-]+(:[0-9]{1,5})?(/[A-Za-z0-9._~/-]*)?|http://127\\.0\\.0\\.1:[0-9]{1,5})\\z");
    static const QRegularExpression relay("\\A(https://[A-Za-z0-9.-]+(:[0-9]{1,5})?(/[A-Za-z0-9._~/-]*)?|http://(127\\.0\\.0\\.1|localhost):[0-9]{1,5})\\z");
    const auto relays=c.value("relays").toArray();
    if (relays.isEmpty() || relays.size()>8) return false;
    for (const auto &r:relays) if (!relay.match(r.toString()).hasMatch()) return false;
    const auto index=c.value("accountIndex").toDouble(-1);
    const auto chain=c.value("chainId").toDouble(-1);
    if (chain!=double(qint64(chain)) || !DexChains::known(qint64(chain))) return false;
    // A local Solana test validator's swap program (that chain only), as the local EVM contract
    if (c.contains("localSolProgram") && !(DexChains::helperChain(qint64(chain))==DexChains::SolLocal && DexChains::solanaAddress(c.value("localSolProgram").toString()))) return false;
    if (c.contains("localSolUsdtMint") && !(qint64(chain)==DexChains::SolUsdtLocal && DexChains::solanaAddress(c.value("localSolUsdtMint").toString()))) return false;
    return c.value("schema")=="monzero-private-xmz-evm-market-v1" && cleanAbsolute(c.value("helper").toString())
        && cleanAbsolute(c.value("tradesDir").toString()) && rpc.match(c.value("evmRpc").toString()).hasMatch()
        && index>=0 && index<2147483648.0 && index==double(qint64(index))
        && (!c.contains("localContract") || QRegularExpression("\\A0x[0-9a-fA-F]{40}\\z").match(c.value("localContract").toString()).hasMatch())
        && localEndpoint(c.value("mainWallet")) && localEndpoint(c.value("swapWallet"));
}
// Decimal digits of a hex quantity ("0x..."), for amounts up to 2^128
QString hexToDecimal(const QString &hex) {
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
// A decimal amount of atomic units as coins with `places` decimals, trailing zeros removed
QString units(const QString &digits,int places) {
    QString d=digits;while (d.size()<=places) d.prepend('0');
    QString whole=d.left(d.size()-places),frac=d.mid(d.size()-places);
    while (frac.endsWith('0')) frac.chop(1);
    return frac.isEmpty() ? whole : whole+"."+frac;
}
bool listingValid(const QJsonObject &signedListing) {
    const auto l=signedListing.value("listing").toObject();
    static const QRegularExpression address("\\A0x[0-9a-fA-F]{40}\\z");
    return signedListing.size()==2 && Hash.match(l.value("id").toString()).hasMatch()
        && (l.value("pair")=="ETH/XMZ" || l.value("pair")=="BNB/XMZ" || l.value("pair")=="SOL/XMZ" || l.value("pair")=="USDT/XMZ" || l.value("pair")=="USDT-BSC/XMZ")
        && (l.value("side")=="sell-xmz" || l.value("side")=="buy-xmz")
        && HexQty.match(l.value("value").toString()).hasMatch() && Digits.match(l.value("xmz").toString()).hasMatch()
        && address.match(l.value("maker").toString()).hasMatch() && l.value("expiresAt").isDouble() && l.value("chainId").isDouble();
}
}

EvmMarketJob::EvmMarketJob(QObject *parent):QObject(parent) {
    m_deadline.setSingleShot(true);
    connect(&m_deadline,&QTimer::timeout,this,[this]{finish(false);});
    connect(&m_process,&QProcess::started,this,[this]{
        const bool written=m_busy && m_process.write(m_input)==m_input.size();
        m_input.fill('\0');m_input.clear();   // may hold the wallet seed
        if (!written) {finish(false);return;}
        m_process.closeWriteChannel();
    });
    connect(&m_process,&QProcess::readyReadStandardOutput,this,&EvmMarketJob::drain);
    connect(&m_process,&QProcess::readyReadStandardError,this,&EvmMarketJob::drain);
    connect(&m_process,&QProcess::errorOccurred,this,[this](QProcess::ProcessError){finish(false);});
    connect(&m_process,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,
        [this](int,QProcess::ExitStatus status){
            QPointer<EvmMarketJob> guard(this);drain();
            if (guard) finish(status==QProcess::NormalExit);
        });
}
EvmMarketJob::~EvmMarketJob() {
    disconnect(&m_process,nullptr,this,nullptr);
    if (m_process.state()!=QProcess::NotRunning) {m_process.kill();m_process.waitForFinished(1000);}
    m_input.fill('\0');
}

bool EvmMarketJob::configure(const QString &path) {
    if (!m_path.isEmpty() || m_busy) return false;
    const auto bytes=LitecoinJournalProtocol::privateConfig(path);
    const auto doc=QJsonDocument::fromJson(bytes);
    if (!doc.isObject() || !configValid(doc.object())) return false;
    const auto c=doc.object();
    if (!BackendExecutable::matches(c.value("helper").toString(),c.value("helperDigest").toString())) return false;
    m_path=path;m_source=bytes;m_config=c;m_program=c.value("helper").toString();m_digest=c.value("helperDigest").toString();
    m_tradesDir=c.value("tradesDir").toString();
    QDir().mkpath(m_tradesDir);QFile::setPermissions(m_tradesDir,QFileDevice::ReadOwner|QFileDevice::WriteOwner|QFileDevice::ExeOwner);
    scanFolders();
    m_state="ready";emit changed();return true;
}

// Our offers and takes survive a restart: they are folders under tradesDir
void EvmMarketJob::scanFolders() {
    m_folders.clear();
    const auto names=QDir(m_tradesDir).entryList(QDir::Dirs|QDir::NoDotAndDotDot,QDir::Name);
    for (const auto &name:names) {
        const auto path=m_tradesDir+"/"+name;
        Folder f;f.path=path;
        const bool maker=QFile::exists(path+"/listing.json"),taker=QFile::exists(path+"/take.json");
        if (!maker && !taker) continue;
        f.kind=maker ? "offer" : "take";
        QFile file(path+(maker ? "/listing.json" : "/take.json"));
        if (file.size()>65536 || !file.open(QIODevice::ReadOnly)) continue;
        const auto o=QJsonDocument::fromJson(file.read(65536)).object();
        f.listing=o.value("listing").toObject();
        if (!listingValid(f.listing)) continue;
        f.listingId=f.listing.value("listing").toObject().value("id").toString();
        f.state=QFile::exists(path+"/journal.json") ? "agreed" : (maker && !o.value("takenBy").isNull() ? "agreed" : "open");
        if (QFile::exists(path+"/cancelled") || QFile::exists(path+"/abandoned")) f.state="cancelled";
        // A take the maker refused: it ended there, nothing was locked
        if (f.state=="open" && !maker && QFile::exists(path+"/refused")) {
            f.state="refused";
            QFile why(path+"/refused");
            if (why.open(QIODevice::ReadOnly)) f.detail=QString::fromUtf8(why.read(400)).left(200);
        }
        f.renew=maker && QFile::exists(path+"/renew");
        // Renewed (kept open): the new offer has its own folder; this one is retired
        if (QFile::exists(path+"/renewed")) f.state="renewed";
        // Taken off the board by our own helper: our side could no longer pay for it
        if (f.state=="open" && QFile::exists(path+"/withdrawn")) {
            f.state="withdrawn";
            QFile why(path+"/withdrawn");
            if (why.open(QIODevice::ReadOnly)) f.detail=QString::fromUtf8(why.read(400)).left(200);
        }
        m_folders.append(f);
    }
}

QStringList EvmMarketJob::agreedFolders() const {
    QStringList out;
    // A trade the wallet saw finished is marked; reopening it would only start its swap wallet again
    for (const auto &f:m_folders) if (f.state=="agreed" && !QFile::exists(f.path+"/finished")) out.append(f.path);
    return out;
}

QJsonObject EvmMarketJob::base(const QString &command,const QString &folder) const {
    QJsonObject r{{"schema",RequestSchema},{"command",command},{"tradeDir",folder.isEmpty() ? m_tradesDir : folder},
        {"relays",m_config.value("relays")}};
    if (m_config.contains("localContract")) r.insert("localContract",m_config.value("localContract"));
    if (m_config.contains("localSolProgram")) r.insert("localSolProgram",m_config.value("localSolProgram"));
    if (m_config.contains("localSolUsdtMint")) r.insert("localSolUsdtMint",m_config.value("localSolUsdtMint"));
    return r;
}

bool EvmMarketJob::tickConfig(QByteArray &walletSeed,QJsonObject *out) const {
    if (!m_swapSeedHex.isEmpty()) {
        walletSeed.fill('\0');walletSeed.clear();
        QJsonObject t{{"evmRpc",m_config.value("evmRpc")},{"swapKeys",QJsonObject{{"swapSeedHex",QString::fromLatin1(m_swapSeedHex)},
            {"evmKeyHex",QString::fromLatin1(m_evmKeyHex)}}},{"mainWallet",m_config.value("mainWallet")},{"swapWallet",m_config.value("swapWallet")}};
        if (m_config.contains("evmLogChunk")) t.insert("evmLogChunk",m_config.value("evmLogChunk"));
        *out=t;return true;
    }
    const bool valid=walletSeed.size()>=16 && walletSeed.size()<=64;
    QByteArray seedHex=walletSeed.toHex();walletSeed.fill('\0');walletSeed.clear();
    if (!valid) {seedHex.fill('\0');return false;}
    QJsonObject t{{"evmRpc",m_config.value("evmRpc")},{"walletSeedHex",QString::fromLatin1(seedHex)},
        {"accountIndex",m_config.value("accountIndex")},{"mainWallet",m_config.value("mainWallet")},{"swapWallet",m_config.value("swapWallet")}};
    seedHex.fill('\0');
    if (m_config.contains("evmLogChunk")) t.insert("evmLogChunk",m_config.value("evmLogChunk"));
    *out=t;return true;
}

QJsonObject EvmMarketJob::tradeConfig(const QString &tradeDir) const {
    QJsonObject c{{"schema","monzero-private-xmz-evm-trade-v1"},{"helper",m_program},{"helperDigest",m_digest},{"tradeDir",tradeDir},
        {"evmRpc",m_config.value("evmRpc")},{"accountIndex",m_config.value("accountIndex")},
        {"mainWallet",m_config.value("mainWallet")},{"swapWallet",m_config.value("swapWallet")}};
    if (m_config.contains("evmLogChunk")) c.insert("evmLogChunk",m_config.value("evmLogChunk"));
    if (m_config.contains("localSolProgram")) c.insert("localSolProgram",m_config.value("localSolProgram"));
    if (m_config.contains("localSolUsdtMint")) c.insert("localSolUsdtMint",m_config.value("localSolUsdtMint"));
    // The trade's relays: a USDT trade's network fees may be paid by them (in USDT)
    if (DexChains::token(qint64(m_config.value("chainId").toDouble()))) c.insert("relays",m_config.value("relays"));
    return c;
}

bool EvmMarketJob::requestAccounts(QByteArray &walletSeed) {
    if (m_busy || m_path.isEmpty()) {walletSeed.fill('\0');walletSeed.clear();return false;}
    QJsonObject t;if (!tickConfig(walletSeed,&t)) return false;
    auto r=base("accounts",QString());r.insert("tick",t);
    return run(r,QString());
}

bool EvmMarketJob::refreshListings(const QString &pair) {
    // This market's pair only (USDT and SOL share the Solana chain but are separate markets)
    if (pair!=DexChains::pair(qint64(m_config.value("chainId").toDouble()))) return false;
    // Partial offers too: this wallet can take part of one
    auto r=base("listings",{});r.insert("pair",pair);r.insert("partial",true);
    return run(r,{});
}

bool EvmMarketJob::post(QByteArray &walletSeed,qint64 chainId,const QString &side,const QString &valueWei,const QString &xmzAtoms,const QString &minXmzAtoms,qint64 lifetimeSecs,bool renew) {
    if (m_busy || m_path.isEmpty() || chainId!=qint64(m_config.value("chainId").toDouble()) || (side!="sell-xmz" && side!="buy-xmz") || !Digits.match(valueWei).hasMatch()
        || !Digits.match(xmzAtoms).hasMatch() || lifetimeSecs<=0 || lifetimeSecs>31*86400 || (!minXmzAtoms.isEmpty() && !Digits.match(minXmzAtoms).hasMatch())) {walletSeed.fill('\0');walletSeed.clear();return false;}
    QJsonObject t;if (!tickConfig(walletSeed,&t)) return false;
    const auto folder=m_tradesDir+"/offer-"+QString::number(QRandomGenerator::system()->generate64(),16);
    auto r=base("post",folder);r.insert("tick",t);
    // The helper trades the market on its chain; a token market names its pair
    QJsonObject offer{{"chainId",DexChains::helperChain(chainId)},{"side",side},{"valueWei",valueWei},{"xmzAtoms",xmzAtoms},{"lifetimeSecs",lifetimeSecs}};
    if (DexChains::token(chainId)) offer.insert("pair",DexChains::pair(chainId));
    if (!minXmzAtoms.isEmpty()) offer.insert("minXmzAtoms",minXmzAtoms);
    if (renew) offer.insert("renew",true);
    r.insert("offer",offer);
    return run(r,folder);
}

bool EvmMarketJob::setRenew(const QString &folder,bool on) {
    for (auto &f:m_folders) if (f.path==folder && f.kind=="offer" && f.state=="open") {
        QFile mark(folder+"/renew");
        const bool ok=on ? (mark.open(QIODevice::WriteOnly) && (mark.close(),true)) : (!mark.exists() || mark.remove());
        if (ok) {f.renew=on;emit changed();}
        return ok;
    }
    return false;
}

bool EvmMarketJob::take(QByteArray &walletSeed,const QString &listingId,const QString &xmzAtoms) {
    const auto signedListing=m_signed.value(listingId);
    if (m_busy || m_path.isEmpty() || signedListing.isEmpty() || (!xmzAtoms.isEmpty() && !Digits.match(xmzAtoms).hasMatch())) {walletSeed.fill('\0');walletSeed.clear();return false;}
    for (const auto &f:m_folders) if (f.listingId==listingId) {walletSeed.fill('\0');walletSeed.clear();return false;}
    QJsonObject t;if (!tickConfig(walletSeed,&t)) return false;
    const auto folder=m_tradesDir+"/take-"+listingId.mid(2,16);
    auto r=base("take",folder);r.insert("tick",t);r.insert("listing",signedListing);
    if (!xmzAtoms.isEmpty()) r.insert("xmzAtoms",xmzAtoms);
    return run(r,folder);
}

bool EvmMarketJob::cancel(QByteArray &walletSeed,const QString &folder) {
    // A take still waiting for the maker's answer: nothing of it is locked, so we just stop
    // waiting (no helper step runs meanwhile: the job is idle). Its folder keeps a mark.
    for (auto &f:m_folders) if (f.path==folder && f.kind=="take" && f.state=="open") {
        walletSeed.fill('\0');walletSeed.clear();
        if (m_busy || QFile::exists(folder+"/journal.json")) return false;
        QFile marker(folder+"/abandoned");
        if (!marker.open(QIODevice::WriteOnly) || marker.write("stopped waiting for the maker\n")<0) return false;
        marker.close();f.state="cancelled";emit changed();return true;
    }
    bool ours=false;for (const auto &f:m_folders) ours=ours || (f.path==folder && f.kind=="offer" && f.state=="open");
    if (m_busy || !ours) {walletSeed.fill('\0');walletSeed.clear();return false;}
    QJsonObject t;if (!tickConfig(walletSeed,&t)) return false;
    auto r=base("cancel",folder);r.insert("tick",t);
    return run(r,folder);
}

bool EvmMarketJob::withdraw(QByteArray &walletSeed,const QString &address) {
    if (m_busy || m_path.isEmpty() || !DexChains::address(qint64(m_config.value("chainId").toDouble()),address)) {walletSeed.fill('\0');walletSeed.clear();return false;}
    QJsonObject t;if (!tickConfig(walletSeed,&t)) return false;
    auto r=base("withdraw",QString());r.insert("tick",t);r.insert("payoutAddress",address);
    r.insert("chainId",double(DexChains::helperChain(qint64(m_config.value("chainId").toDouble()))));
    m_withdrawal.clear();
    return run(r,QString());
}

bool EvmMarketJob::report(const QString &folder) {
    if (m_busy || m_path.isEmpty() || !cleanAbsolute(folder) || !folder.startsWith(m_tradesDir+"/")) return false;
    return run(base("report",folder),folder);
}

bool EvmMarketJob::step(QByteArray &walletSeed) {
    QList<int> open;
    for (int i=0;i<m_folders.size();++i) if (m_folders[i].state=="open") open.append(i);
    if (m_busy || open.isEmpty()) {walletSeed.fill('\0');walletSeed.clear();return false;}
    const auto &f=m_folders[open[m_next++%open.size()]];
    QJsonObject t;if (!tickConfig(walletSeed,&t)) return false;
    auto r=base(f.kind=="offer" ? "serve" : "take",f.path);r.insert("tick",t);
    return run(r,f.path);
}

bool EvmMarketJob::run(const QJsonObject &request,const QString &folder) {
    if (m_busy || m_path.isEmpty() || m_process.state()!=QProcess::NotRunning) return false;
    if (LitecoinJournalProtocol::privateConfig(m_path)!=m_source || !BackendExecutable::matches(m_program,m_digest)) {
        m_state="source-changed";emit changed();return false;
    }
    m_command=request.value("command").toString();m_folder=folder;
    m_input=QJsonDocument(request).toJson(QJsonDocument::Compact);m_output.clear();m_errors=0;
    m_busy=true;m_state="running";m_error.clear();
    QProcessEnvironment environment;environment.insert("LANG","C");
    m_process.setProcessEnvironment(environment);m_process.setProcessChannelMode(QProcess::SeparateChannels);
    m_deadline.start(120000);
    m_process.start(m_program,QStringList{});emit changed();return true;
}

void EvmMarketJob::drain() {
    if (!m_busy) return;
    constexpr qint64 limit=8*1024*1024;
    m_process.setReadChannel(QProcess::StandardOutput);m_output+=m_process.read(limit+1-m_output.size());
    m_process.setReadChannel(QProcess::StandardError);m_errors+=m_process.read(4097-m_errors).size();
    if (m_output.size()>limit || m_errors>4096) finish(false);
}

void EvmMarketJob::finish(bool success) {
    if (!m_busy) return;
    m_busy=false;m_deadline.stop();m_input.fill('\0');m_input.clear();
    if (m_process.state()!=QProcess::NotRunning) m_process.kill();
    const auto doc=QJsonDocument::fromJson(m_output);m_output.clear();
    const auto r=doc.object();
    const bool parsed=success && doc.isObject() && r.value("schema")==ResponseSchema && r.value("ok").isBool();
    if (!parsed || !r.value("ok").toBool()) {
        m_state=parsed ? "refused" : "failed";
        m_error=parsed ? r.value("error").toString().left(300) : QStringLiteral("no valid answer from the helper");
        emit changed();return;
    }
    m_state="ok";
    if (m_command=="accounts") {
        // This wallet's trade accounts for these keys (the Solana one is where SOL is paid in)
        const auto sol=r.value("sol").toString();
        if (DexChains::solanaAddress(sol)) m_solAccount=sol;
        const auto usdt=r.value("solUsdt").toString();
        if (DexChains::solanaAddress(usdt)) m_solUsdtAccount=usdt;
    } else if (m_command=="listings") {
        m_signed.clear();m_listings.clear();
        const int places=DexChains::decimals(qint64(m_config.value("chainId").toDouble()));
        for (const auto &v:r.value("listings").toArray()) {
            const auto entry=v.toObject(),s=entry.value("signed").toObject();
            if (!listingValid(s)) continue;
            const auto l=s.value("listing").toObject();
            const auto wei=hexToDecimal(l.value("value").toString());
            if (wei.isEmpty()) continue;
            const auto id=l.value("id").toString();
            m_signed.insert(id,s);
            QVariantMap shown{{"id",id},{"pair",l.value("pair")},{"side",l.value("side")},{"chainId",l.value("chainId").toDouble()},
                {"maker",l.value("maker")},{"valueWei",wei},{"xmzAtoms",l.value("xmz")},{"coin",weiToCoin(wei,places)},
                {"xmz",units(l.value("xmz").toString(),11)},{"price",priceText(wei,l.value("xmz").toString(),places)},
                {"expiresAt",l.value("expiresAt").toDouble()},{"relays",entry.value("relays").toArray().size()}};
            // A partial offer: any amount from its minimum may be taken
            if (Digits.match(l.value("minXmz").toString()).hasMatch()) {shown.insert("minXmzAtoms",l.value("minXmz").toString());shown.insert("minXmz",units(l.value("minXmz").toString(),11));}
            m_listings.append(shown);
        }
    } else if (m_command=="post") {
        scanFolders();
    } else if (m_command=="serve" || m_command=="take") {
        // A first take creates its folder: track it, so later steps collect the maker's answer
        if (m_command=="take" && std::none_of(m_folders.cbegin(),m_folders.cend(),[this](const Folder &f){return f.path==m_folder;})) scanFolders();
        // Part of a partial offer of ours was taken: the rest is a new offer in its own folder
        const auto rest=r.value("remainder").toObject().value("folder").toString();
        if (!rest.isEmpty() && std::none_of(m_folders.cbegin(),m_folders.cend(),[&rest](const Folder &f){return f.path==rest;})) scanFolders();
        // A kept-open offer was posted again: serve the new folder from now on
        const auto renewed=r.value("renewed").toObject().value("folder").toString();
        if (!renewed.isEmpty()) scanFolders();
        const bool done=r.value("taken").toBool() || r.value("agreed").toBool();
        for (auto &f:m_folders) if (f.path==m_folder) {
            if (done) f.state="agreed";
            else if (!renewed.isEmpty()) f.state="renewed";
            else if (r.value("offerWithdrawn").toBool()) f.state="withdrawn";
            else if (r.value("expired").toBool()) f.state="expired";
            else if (r.value("refusedFinal").toBool()) f.state="refused";
            else if (r.value("stopped").toBool()) f.state="cancelled";
            f.detail=r.value("refused").isString() ? r.value("refused").toString().left(200)
                : r.value("offerWithdrawn").toBool() ? r.value("reason").toString().left(200) : QString();
        }
        if (done) {const auto folder=m_folder;emit changed();emit agreed(folder);return;}
    } else if (m_command=="withdraw") {
        m_withdrawal=r.value("withdrawn").isObject() ? r.value("withdrawn").toObject().toVariantMap()
            : QVariantMap{{"sent",false},{"reason",r.value("reason").toString().left(200)}};
    } else if (m_command=="cancel") {
        QFile marker(m_folder+"/cancelled");marker.open(QIODevice::WriteOnly);marker.close();
        scanFolders();
    }
    emit changed();
}

QVariantMap EvmMarketJob::status() const {
    QVariantList folders;
    const int places=DexChains::decimals(qint64(m_config.value("chainId").toDouble()));
    for (const auto &f:m_folders) {
        const auto l=f.listing.value("listing").toObject();
        const auto wei=hexToDecimal(l.value("value").toString());
        folders.append(QVariantMap{{"folder",f.path},{"kind",f.kind},{"state",f.state},{"detail",f.detail},{"id",f.listingId},{"renew",f.renew},
            {"pair",l.value("pair")},{"side",l.value("side")},{"coin",weiToCoin(wei,places)},{"xmz",units(l.value("xmz").toString(),11)},
            {"price",priceText(wei,l.value("xmz").toString(),places)},{"expiresAt",l.value("expiresAt").toDouble()}});
    }
    return {{"configured",!m_path.isEmpty()},{"busy",m_busy},{"state",m_state},{"error",m_error},{"command",m_command},
        {"listings",m_listings},{"mine",folders},{"withdrawal",m_withdrawal},{"relays",m_config.value("relays").toArray().toVariantList()},
        {"chainId",m_config.value("chainId").toDouble()},{"coin",DexChains::coin(qint64(m_config.value("chainId").toDouble()))},{"decimals",DexChains::decimals(qint64(m_config.value("chainId").toDouble()))},{"solAccount",m_solAccount}};
}

QString EvmMarketJob::weiToCoin(const QString &valueWei,int decimals) {
    return Digits.match(valueWei).hasMatch() ? units(valueWei,decimals) : QString();
}

QString EvmMarketJob::priceText(const QString &valueWei,const QString &xmzAtoms,int decimals) {
    // coin per XMZ = (wei / 1e18) / (atoms / 1e11); for display only
    const double wei=valueWei.toDouble(),atoms=xmzAtoms.toDouble();
    if (!(wei>0) || !(atoms>0)) return {};
    // Plain decimals with six significant digits (never "5e-05"), trailing zeros trimmed
    const double price=wei/std::pow(10.0,decimals)/(atoms/1e11);
    const int places=qBound(0,5-int(std::floor(std::log10(price))),18);
    QString text=QString::number(price,'f',places);
    if (text.contains('.')) {while (text.endsWith('0')) text.chop(1);if (text.endsWith('.')) text.chop(1);}
    return text;
}

void EvmMarketJob::setSwapKeys(const QByteArray &swapSeedHex,const QByteArray &evmKeyHex) {
    sodium_memzero(m_swapSeedHex.data(),m_swapSeedHex.size());sodium_memzero(m_evmKeyHex.data(),m_evmKeyHex.size());
    m_swapSeedHex=swapSeedHex;m_evmKeyHex=evmKeyHex;
}
