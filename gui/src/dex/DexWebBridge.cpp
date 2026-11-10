#include "DexChains.h"
#include "DexWebBridge.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QFile>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSettings>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrlQuery>

namespace {
const int MaxRequest=64*1024;
QByteArray randomHex(int bytes) {
    QByteArray raw(bytes,'\0');
    for (auto &c:raw) c=char(QRandomGenerator::system()->bounded(256));
    return raw.toHex();
}
QByteArray hashed(const QByteArray &token) {return QCryptographicHash::hash(token,QCryptographicHash::Sha256);}
bool sameBytes(const QByteArray &a,const QByteArray &b) {
    if (a.size()!=b.size()) return false;
    char diff=0;for (int i=0;i<a.size();++i) diff|=a[i]^b[i];
    return diff==0;
}
QVariantMap pick(const QVariantMap &from,std::initializer_list<const char *> keys) {
    QVariantMap out;for (auto *k:keys) if (from.contains(k)) out.insert(k,from.value(k));
    return out;
}
}

DexWebBridge::DexWebBridge(QObject *engine,const QStringList &allowedOrigins,bool allowLocalPages,QObject *parent)
    :QObject(parent),m_engine(engine),m_origins(allowedOrigins),m_localPages(allowLocalPages) {}

int DexWebBridge::port() const {return m_server && m_server->isListening() ? m_server->serverPort() : 0;}
QVariantMap DexWebBridge::connection() const {
    return m_tokenHash.isEmpty() ? QVariantMap{} : QVariantMap{{"origin",m_origin},{"account",m_account},{"solAccount",m_solAccount}};
}
QString DexWebBridge::payoutAccount(qint64 chainId) const {
    return DexChains::solana(chainId) ? m_solAccount : m_account;
}

bool DexWebBridge::listen(const QString &settingsFile) {
    if (!m_engine || m_server) return false;
    m_settingsFile=settingsFile;
    {
        QSettings s(m_settingsFile,QSettings::IniFormat);
        const auto hash=QByteArray::fromHex(s.value("page/tokenHash").toByteArray());
        const auto origin=s.value("page/origin").toString(),account=s.value("page/account").toString(),sol=s.value("page/solAccount").toString();
        const bool evmOk=DexChains::evmAddress(account),solOk=DexChains::solanaAddress(sol);
        if (hash.size()==32 && originAllowed(origin.toLatin1()) && (evmOk || solOk) && (account.isEmpty() || evmOk) && (sol.isEmpty() || solOk)) {
            m_tokenHash=hash;m_origin=origin;m_account=account;m_solAccount=sol;
        }
    }
    m_server=new QTcpServer(this);
    bool listening=false;
    for (int i=0;i<Ports && !listening;++i) listening=m_server->listen(QHostAddress::LocalHost,quint16(FirstPort+i));
    if (!listening) {delete m_server;m_server=nullptr;return false;}
    connect(m_server,&QTcpServer::newConnection,this,[this]{while (auto *s=m_server->nextPendingConnection()) serve(s);});
    connect(m_engine,SIGNAL(evmTradeChanged()),this,SLOT(watchTrade()));
    connect(m_engine,SIGNAL(evmMarketChanged()),this,SLOT(watchTrade()));
    emit changed();
    return true;
}

void DexWebBridge::save() const {
    QSettings s(m_settingsFile,QSettings::IniFormat);
    s.setValue("page/tokenHash",QString::fromLatin1(m_tokenHash.toHex()));
    s.setValue("page/origin",m_origin);s.setValue("page/account",m_account);s.setValue("page/solAccount",m_solAccount);
    s.sync();
    QFile::setPermissions(m_settingsFile,QFile::ReadOwner | QFile::WriteOwner);
}

bool DexWebBridge::originAllowed(const QByteArray &origin) const {
    if (origin.isEmpty()) return m_localPages;   // not a browser page: test scripts on prototype builds only
    const auto text=QString::fromLatin1(origin);
    if (m_origins.contains(text)) return true;
    static const QRegularExpression local("\\Ahttp://(localhost|127\\.0\\.0\\.1)(:[0-9]{1,5})?\\z");
    return m_localPages && local.match(text).hasMatch();
}

bool DexWebBridge::acceptPairing(const QString &id) {
    if (m_pairRequest.isEmpty() || m_pairRequest.value("id")!=id || QDateTime::currentSecsSinceEpoch()>m_pairExpires) return false;
    const auto token=randomHex(32);
    m_tokenHash=hashed(token);m_origin=m_pairRequest.value("origin").toString();m_account=m_pairRequest.value("account").toString();
    m_solAccount=m_pairRequest.value("solAccount").toString();
    m_pairResult="accepted";m_pairResultId=id;m_pairResultToken=QString::fromLatin1(token);
    m_pairRequest.clear();save();
    // No trading keys yet, or locked: ask for the password right away, in the same sitting
    m_keysWanted=m_engine && !m_engine->property("dexWalletStatus").toMap().value("keysReady").toBool();
    emit changed();
    return true;
}
void DexWebBridge::dismissKeys() {if (m_keysWanted) {m_keysWanted=false;emit changed();}}
void DexWebBridge::refusePairing(const QString &id) {
    if (m_pairRequest.value("id")!=id) return;
    m_pairResult="refused";m_pairResultId=id;m_pairResultToken.clear();
    m_pairRequest.clear();emit changed();
}
void DexWebBridge::disconnectPage() {
    m_tokenHash.clear();m_origin.clear();m_account.clear();m_solAccount.clear();m_keysWanted=false;save();emit changed();
}

bool DexWebBridge::call(const char *method,const QVariantList &args) const {
    if (!m_engine) return false;
    bool result=false;
    QGenericArgument a[5];
    QList<QString> strings;QList<int> ints;strings.reserve(5);ints.reserve(5);
    for (int i=0;i<args.size() && i<5;++i) {
        if (args[i].type()==QVariant::Int) {ints.append(args[i].toInt());a[i]=Q_ARG(int,ints.last());}
        else {strings.append(args[i].toString());a[i]=Q_ARG(QString,strings.last());}
    }
    return QMetaObject::invokeMethod(m_engine,method,Qt::DirectConnection,Q_RETURN_ARG(bool,result),a[0],a[1],a[2],a[3],a[4]) && result;
}

QVariantMap DexWebBridge::statusFor() const {
    const auto wallet=m_engine->property("dexWalletStatus").toMap();
    const auto trade=m_engine->property("evmTradeStatus").toMap();
    const auto markets=m_engine->property("evmMarketStatus").toMap();
    QVariantMap marketOut;
    for (auto it=markets.cbegin();it!=markets.cend();++it)
        marketOut.insert(it.key(),pick(it.value().toMap(),{"coin","chainId","decimals","configured","busy","state","mine","message","error","withdrawal"}));
    auto tradeOut=pick(trade,{"configured","busy","state","tradeId","commitment","awaitingApproval","awaitingFunding","startRefusal","error","payout","account","solAccount"});
    tradeOut.insert("snapshot",pick(trade.value("snapshot").toMap(),{"stage","role","done","offer","evmTxs","xmzSweeps","approved","progress","deadlines"}));
    return {{"wallet",pick(wallet,{"state","balance","unlocked","address","keysReady","keysExist","keysBusy","tradeAccount","solTradeAccount","error"})},
        {"markets",marketOut},{"trade",tradeOut},{"account",m_account},{"solAccount",m_solAccount}};
}

void DexWebBridge::watchTrade() {
    if (!m_engine || m_tokenHash.isEmpty()) return;
    if (m_keysWanted && m_engine->property("dexWalletStatus").toMap().value("keysReady").toBool()) {m_keysWanted=false;emit changed();}
    const auto trade=m_engine->property("evmTradeStatus").toMap();
    const auto snapshot=trade.value("snapshot").toMap();
    const auto id=trade.value("tradeId").toString();
    if (id.isEmpty()) return;
    if (trade.value("awaitingApproval").toBool() && m_announcedTrade!=id) {
        m_announcedTrade=id;
        emit attention(tr("A trade is waiting for your approval"),tr("Check the amounts in your Monzero wallet and approve the trade there."));
    }
    // When the trade is over, what the trade account holds goes back to the account the user
    // accepted for this page. Not while another offer of ours is still open: it needs the funds.
    if (!snapshot.value("done").toBool() || trade.value("busy").toBool() || m_paidTrade==id || !trade.value("payout").toMap().isEmpty()) return;
    const auto markets=m_engine->property("evmMarketStatus").toMap();
    for (const auto &market:markets) for (const auto &mine:market.toMap().value("mine").toList())
        if (mine.toMap().value("state")=="open") return;
    // To the user's account on the trade's chain; a page without one for that chain is not paid
    // to by itself (the user sends it back from the page after connecting that wallet)
    const auto to=payoutAccount(qint64(snapshot.value("offer").toMap().value("chainId").toDouble()));
    if (!to.isEmpty() && call("payoutEvmTrade",{to})) m_paidTrade=id;
}

QByteArray DexWebBridge::respond(const Request &r,int *status) {
    const auto json=[](const QVariantMap &m){return QJsonDocument(QJsonObject::fromVariantMap(m)).toJson(QJsonDocument::Compact);};
    const auto fail=[&](int code,const QString &why){*status=code;return json({{"ok",false},{"error",why}});};
    const auto body=QJsonDocument::fromJson(r.body).object();
    const bool authorised=!m_tokenHash.isEmpty() && !r.token.isEmpty() && sameBytes(hashed(r.token),m_tokenHash)
        && QString::fromLatin1(r.origin)==m_origin;
    *status=200;
    if (r.method=="GET" && r.path=="/v1/hello")
        return json({{"ok",true},{"app","monzero-wallet"},{"bridge",1},{"connected",authorised},{"account",authorised ? m_account : QString()},{"solAccount",authorised ? m_solAccount : QString()}});
    if (r.method=="POST" && r.path=="/v1/pair") {
        // The user's own wallets: MetaMask (ETH, BNB) and/or a Solana wallet; at least one
        const auto account=body.value("account").toString(),solAccount=body.value("solAccount").toString();
        if ((account.isEmpty() && solAccount.isEmpty()) || (!account.isEmpty() && !DexChains::evmAddress(account))
            || (!solAccount.isEmpty() && !DexChains::solanaAddress(solAccount))) return fail(400,"account");
        const auto now=QDateTime::currentSecsSinceEpoch();
        if (!m_pairRequest.isEmpty() && now<=m_pairExpires) return fail(429,"another connection request is waiting in the wallet");
        const auto id=QString::fromLatin1(randomHex(16));
        const auto code=QString::number(QRandomGenerator::system()->bounded(1000000)).rightJustified(6,'0');
        m_pairRequest={{"id",id},{"origin",QString::fromLatin1(r.origin)},{"account",account},{"solAccount",solAccount},{"code",code}};
        m_pairExpires=now+180;m_pairResult.clear();m_pairResultId.clear();m_pairResultToken.clear();
        emit changed();
        emit attention(tr("The DEX web page asks to connect"),tr("Open your Monzero wallet and compare the code: %1").arg(code));
        return json({{"ok",true},{"id",id},{"code",code}});
    }
    if (r.method=="GET" && r.path=="/v1/pair") {
        const auto id=QUrlQuery(QString::fromLatin1(r.query)).queryItemValue("id");
        if (!m_pairRequest.isEmpty() && m_pairRequest.value("id")==id) {
            if (QDateTime::currentSecsSinceEpoch()<=m_pairExpires) return json({{"ok",true},{"state","pending"}});
            m_pairRequest.clear();emit changed();
            return json({{"ok",true},{"state","expired"}});
        }
        if (!id.isEmpty() && id==m_pairResultId) {
            QVariantMap out{{"ok",true},{"state",m_pairResult}};
            if (m_pairResult=="accepted" && !m_pairResultToken.isEmpty()) {out.insert("token",m_pairResultToken);m_pairResultToken.clear();}   // handed over once
            return json(out);
        }
        return json({{"ok",true},{"state","expired"}});
    }
    if (!authorised) return fail(401,"not connected");
    if (r.method=="GET" && r.path=="/v1/status") {auto s=statusFor();s.insert("ok",true);return json(s);}
    if (r.method!="POST") return fail(404,"unknown");
    const auto message=[this](const QString &coin){
        const auto market=m_engine->property("evmMarketStatus").toMap().value(coin).toMap();
        return market.value("message").toString();
    };
    const auto coinOf=[](int chainId){return DexChains::coin(chainId);};
    if (r.path=="/v1/refresh") {
        const auto pair=body.value("pair").toString();
        if (pair!="ETH/XMZ" && pair!="BNB/XMZ" && pair!="SOL/XMZ") return fail(400,"pair");
        return json({{"ok",call("refreshEvmOffers",{pair})}});
    }
    if (r.path=="/v1/offer") {
        const int chainId=body.value("chainId").toInt(),hours=body.value("hours").toInt(6);
        const auto side=body.value("side").toString();
        if (side!="buy-xmz" && side!="sell-xmz") return fail(400,"side");
        const bool ok=call("postEvmOffer",{chainId,side,body.value("coin").toString(),body.value("xmz").toString(),hours});
        return json({{"ok",ok},{"message",message(coinOf(chainId))}});
    }
    if (r.path=="/v1/take") {
        const auto id=body.value("id").toString();
        if (!QRegularExpression("\\A0x[0-9a-fA-F]{64}\\z").match(id).hasMatch()) return fail(400,"id");
        const bool ok=call("takeEvmOffer",{id});
        return json({{"ok",ok},{"message",ok ? QString() : message("ETH")}});   // the engine's message is one for all coins
    }
    if (r.path=="/v1/cancel") return json({{"ok",call("cancelEvmOffer",{body.value("folder").toString()})}});
    if (r.path=="/v1/return") {
        // Only ever to the account the user accepted for this page
        const int chainId=body.value("chainId").toInt();
        const auto to=payoutAccount(chainId);
        if (to.isEmpty()) return json({{"ok",false},{"message",tr("Connect your own wallet for this coin first: the funds go back to it.")}});
        const bool ok=call("returnEvmFunds",{chainId,to});
        return json({{"ok",ok},{"message",message(coinOf(chainId))}});
    }
    // Things only the user can do, in the wallet: the page may only ask for the screen to be shown
    // (at most once every few seconds, so a page cannot flood the wallet with prompts)
    if (r.path=="/v1/unlock" || r.path=="/v1/xmz-add" || r.path=="/v1/xmz-return") {
        const auto now=QDateTime::currentMSecsSinceEpoch();
        if (now-m_lastAsk<3000) return fail(429,"wait a moment");
        m_lastAsk=now;
        const auto wallet=m_engine->property("dexWalletStatus").toMap();
        if (r.path=="/v1/unlock") {
            if (wallet.value("keysReady").toBool()) return json({{"ok",true},{"ready",true}});
            m_keysWanted=true;emit changed();
            emit attention(tr("Your trading password is needed"),tr("Open your Monzero wallet to unlock your trading keys."));
            return json({{"ok",true}});
        }
        if (wallet.value("state")!="ready") return json({{"ok",false},{"message",tr("The trading wallet is still starting.")}});
        if (r.path=="/v1/xmz-add") {
            const auto xmz=body.value("xmz").toString();
            if (!xmz.isEmpty() && !QRegularExpression("\\A[0-9]{1,9}(\\.[0-9]{1,8})?\\z").match(xmz).hasMatch()) return fail(400,"xmz");
            emit xmzAddRequested(xmz);
            emit attention(tr("Add XMZ for trading"),tr("Confirm the transfer in your Monzero wallet."));
        } else {
            emit xmzReturnRequested();
            emit attention(tr("Move XMZ back to your main balance"),tr("Confirm it in your Monzero wallet."));
        }
        return json({{"ok",true}});
    }
    // Giving up a trade that was never approved moves nothing: the page may ask for it
    if (r.path=="/v1/abandon") return json({{"ok",call("abandonEvmTrade",{})}});
    if (r.path=="/v1/disconnect") {disconnectPage();return json({{"ok",true}});}
    return fail(404,"unknown");
}

void DexWebBridge::serve(QTcpSocket *socket) {
    connect(socket,&QTcpSocket::disconnected,socket,&QObject::deleteLater);
    connect(socket,&QTcpSocket::readyRead,this,[this,socket]{
        auto buffer=socket->property("request").toByteArray()+socket->readAll();
        if (buffer.size()>MaxRequest) {socket->abort();return;}
        const int head=buffer.indexOf("\r\n\r\n");
        if (head<0) {socket->setProperty("request",buffer);return;}
        const auto lines=buffer.left(head).split('\n');
        const auto first=lines.value(0).trimmed().split(' ');
        Request r;r.method=first.value(0);
        const auto target=first.value(1);const int q=target.indexOf('?');
        r.path=q<0 ? target : target.left(q);r.query=q<0 ? QByteArray() : target.mid(q+1);
        qint64 length=0;bool preflightPrivate=false;
        for (int i=1;i<lines.size();++i) {
            const auto line=lines[i].trimmed();const int colon=line.indexOf(':');
            if (colon<0) continue;
            const auto name=line.left(colon).toLower(),value=line.mid(colon+1).trimmed();
            if (name=="origin") r.origin=value;
            else if (name=="host") r.host=value;
            else if (name=="content-length") length=value.toLongLong();
            else if (name=="authorization" && value.startsWith("Bearer ")) r.token=value.mid(7).trimmed();
            else if (name=="access-control-request-private-network") preflightPrivate=true;
        }
        if (length<0 || length>MaxRequest) {socket->abort();return;}
        if (buffer.size()<head+4+length) {socket->setProperty("request",buffer);return;}
        r.body=buffer.mid(head+4,int(length));
        // Only this loopback address by name, and only the DEX page's own origin
        const auto port=QByteArray::number(socket->localPort());
        const bool hostOk=r.host=="127.0.0.1:"+port || r.host=="localhost:"+port;
        int status=403;QByteArray out="{\"ok\":false,\"error\":\"origin\"}";
        const bool allowed=hostOk && originAllowed(r.origin);
        if (allowed) {
            if (r.method=="OPTIONS") {status=204;out.clear();}
            else out=respond(r,&status);
        }
        QByteArray reply="HTTP/1.1 "+QByteArray::number(status)+(status==200 ? " OK" : status==204 ? " No Content" : " Error")+"\r\n";
        if (allowed && !r.origin.isEmpty()) {
            reply+="Access-Control-Allow-Origin: "+r.origin+"\r\nVary: Origin\r\n"
                "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\nAccess-Control-Allow-Headers: content-type, authorization\r\n"
                "Access-Control-Max-Age: 600\r\n";
            if (preflightPrivate) reply+="Access-Control-Allow-Private-Network: true\r\n";
        }
        reply+="Content-Type: application/json\r\nCache-Control: no-store\r\nConnection: close\r\nContent-Length: "+QByteArray::number(out.size())+"\r\n\r\n"+out;
        socket->write(reply);socket->flush();socket->disconnectFromHost();
    });
}
