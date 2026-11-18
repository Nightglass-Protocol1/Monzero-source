#include "DexExchangeFeed.h"
#include "DexChains.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QUrl>
#include <QUrlQuery>

namespace {
// Relay answers are small; anything larger is not what we asked for
const qint64 MaxReply=2*1024*1024;
const int TimeoutMs=15000;
}

DexExchangeFeed::DexExchangeFeed(SourceFor sources,QObject *parent) : QObject(parent),m_sources(std::move(sources)),m_net(new QNetworkAccessManager(this)) {}

bool DexExchangeFeed::knownCoin(const QString &coin) {return coin=="ETH" || coin=="BNB" || coin=="SOL" || coin=="USDT";}
bool DexExchangeFeed::knownInterval(int interval) {return interval==3600 || interval==4*3600 || interval==24*3600;}
bool DexExchangeFeed::allowedBase(const QString &text) {
    const QUrl url(text);
    if (!url.isValid() || url.host().isEmpty() || !url.userInfo().isEmpty() || url.hasQuery() || url.hasFragment()) return false;
    if (url.scheme()=="https") return true;
    return url.scheme()=="http" && (url.host()=="127.0.0.1" || url.host()=="localhost");
}

void DexExchangeFeed::store(const QString &key,const QVariant &value) {
    m_data.insert(key,value);
    emit changed();
}

// GET base+path from the first base that answers with JSON; the parsed answer goes to `key`
void DexExchangeFeed::get(const QStringList &bases,const QString &path,const QString &key,int index) {
    if (index>=bases.size()) {m_pending.remove(key);return;}
    if (!allowedBase(bases[index])) {get(bases,path,key,index+1);return;}
    QString base=bases[index];
    while (base.endsWith('/')) base.chop(1);
    QNetworkRequest request(QUrl(base+path));
    request.setTransferTimeout(TimeoutMs);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::NoLessSafeRedirectPolicy);
    auto *reply=m_net->get(request);
    connect(reply,&QNetworkReply::downloadProgress,reply,[reply](qint64 received,qint64){if (received>MaxReply) reply->abort();});
    connect(reply,&QNetworkReply::finished,this,[this,reply,bases,path,key,index]{
        reply->deleteLater();
        const auto body=reply->error()==QNetworkReply::NoError ? reply->readAll() : QByteArray();
        QJsonParseError error{};
        const auto doc=QJsonDocument::fromJson(body,&error);
        if (body.isEmpty() || body.size()>MaxReply || error.error!=QJsonParseError::NoError || !doc.isObject()) {get(bases,path,key,index+1);return;}
        const auto o=doc.object();
        m_pending.remove(key);
        if (key.startsWith("candles:")) store(key,o.value("candles").toArray().toVariantList());
        else if (key.startsWith("prints:")) store(key,o.value("prints").toArray().toVariantList());
        else if (key=="usd") {
            QVariantMap usd;
            for (const auto &c:o.value("coins").toArray()) {
                const auto coin=c.toObject();
                const double price=coin.value("usd").toDouble();
                if (price>0) usd.insert(coin.value("symbol").toString(),price);
            }
            store(key,usd);
        }
    });
}

bool DexExchangeFeed::candles(const QString &coin,int interval) {
    if (!knownCoin(coin) || !knownInterval(interval)) return false;
    const auto key=QStringLiteral("candles:%1:%2").arg(coin).arg(interval);
    if (m_pending.value(key)) return true;
    const auto relays=m_sources(coin).relays;
    if (relays.isEmpty()) return false;
    m_pending.insert(key,true);
    QUrlQuery q;q.addQueryItem("pair",coin+"/XMZ");q.addQueryItem("interval",QString::number(interval));
    get(relays,"/v1/candles?"+q.toString(QUrl::FullyEncoded),key);
    return true;
}

bool DexExchangeFeed::prints(const QString &coin) {
    if (!knownCoin(coin)) return false;
    const auto key="prints:"+coin;
    if (m_pending.value(key)) return true;
    const auto relays=m_sources(coin).relays;
    if (relays.isEmpty()) return false;
    m_pending.insert(key,true);
    QUrlQuery q;q.addQueryItem("pair",coin+"/XMZ");
    get(relays,"/v1/prints?"+q.toString(QUrl::FullyEncoded),key);
    return true;
}

bool DexExchangeFeed::usdPrices() {
    if (m_pending.value("usd")) return true;
    m_pending.insert("usd",true);
    get({QStringLiteral("https://monzero.org")},QStringLiteral("/markets/api.php"),QStringLiteral("usd"));
    return true;
}

// The trade account's balance: eth_getBalance on ETH/BNB, getBalance on Solana, at the RPC node
// the market's helper uses; the account is this wallet's own (from the engine), never the page's
bool DexExchangeFeed::balance(const QString &coin) {
    if (!knownCoin(coin)) return false;
    const auto source=m_sources(coin);
    const auto key="balance:"+coin;
    if (m_pending.value(key) || !allowedBase(source.rpc) || source.account.isEmpty()) return false;
    if (source.solana ? !DexChains::solanaAddress(source.account) : !DexChains::evmAddress(source.account)) return false;
    m_pending.insert(key,true);
    // USDT on Solana: the token accounts of the trade account for Tether's mint
    const bool token=coin=="USDT";
    const QJsonObject call=token
        ? QJsonObject{{"jsonrpc","2.0"},{"id",1},{"method","getTokenAccountsByOwner"},{"params",QJsonArray{source.account,
              QJsonObject{{"mint","Es9vMFrzaCERmJfrF4H2FYD4KCoNkY11McCe8BenwNYB"}},QJsonObject{{"encoding","jsonParsed"},{"commitment","confirmed"}}}}}
        : source.solana
        ? QJsonObject{{"jsonrpc","2.0"},{"id",1},{"method","getBalance"},{"params",QJsonArray{source.account,QJsonObject{{"commitment","confirmed"}}}}}
        : QJsonObject{{"jsonrpc","2.0"},{"id",1},{"method","eth_getBalance"},{"params",QJsonArray{source.account,"latest"}}};
    QNetworkRequest request{QUrl(source.rpc)};
    request.setHeader(QNetworkRequest::ContentTypeHeader,"application/json");
    request.setTransferTimeout(TimeoutMs);
    auto *reply=m_net->post(request,QJsonDocument(call).toJson(QJsonDocument::Compact));
    connect(reply,&QNetworkReply::finished,this,[this,reply,key,solana=source.solana,token]{
        reply->deleteLater();
        m_pending.remove(key);
        const auto body=reply->error()==QNetworkReply::NoError && reply->bytesAvailable()<=MaxReply ? reply->readAll() : QByteArray();
        const auto result=QJsonDocument::fromJson(body).object().value("result");
        QString amount;
        if (token) {
            // Base units (6 decimals) summed over the account's USDT token accounts; none yet: 0
            quint64 units=0;bool read=result.isObject();
            for (const auto &a:result.toObject().value("value").toArray()) {
                const auto text=a.toObject().value("account").toObject().value("data").toObject().value("parsed").toObject()
                    .value("info").toObject().value("tokenAmount").toObject().value("amount").toString();
                bool ok=false;const auto n=text.toULongLong(&ok);
                if (ok) units+=n; else read=false;
            }
            if (read) amount=QString::number(units);
        } else if (solana) {
            const auto v=result.toObject().value("value");
            if (v.isDouble() && v.toDouble()>=0) amount=QString::number(qint64(v.toDouble()));
        } else {
            static const QRegularExpression hex("\\A0x[0-9a-fA-F]{1,64}\\z");
            const auto text=result.toString();
            if (hex.match(text).hasMatch()) {
                // Hex wei -> decimal text without overflow (balances can exceed 64 bits)
                QString decimal="0";
                for (const auto ch:text.mid(2)) {
                    int carry=QString(ch).toInt(nullptr,16);
                    QString next;
                    for (int i=decimal.size()-1;i>=0;--i) {
                        const int d=(decimal[i].digitValue())*16+carry;
                        next.prepend(QChar('0'+d%10));carry=d/10;
                    }
                    while (carry) {next.prepend(QChar('0'+carry%10));carry/=10;}
                    while (next.size()>1 && next.startsWith('0')) next.remove(0,1);
                    decimal=next;
                }
                amount=decimal;
            }
        }
        if (!amount.isEmpty()) store(key,amount);
    });
    return true;
}
