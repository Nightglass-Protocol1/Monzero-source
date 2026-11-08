#include "BtcMarketJob.h"
#include "BtcTradeSetup.h"
#include "LitecoinJournalProtocol.h"
#include "OfferIdentity.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QUrl>
#include <sodium.h>

using namespace BtcTradeSetup;

namespace {
constexpr qint64 ResendSeconds=300;
bool relayUrl(const QString &url) {
    static const QRegularExpression ok("\\A(https://[A-Za-z0-9.-]+(:[0-9]{1,5})?(/[A-Za-z0-9._~/-]*)?|http://(127\\.0\\.0\\.1|localhost):[0-9]{1,5})\\z");
    return ok.match(url).hasMatch();
}
QJsonObject readJson(const QString &path) {
    QFile f(path);
    if (f.size()>1024*1024 || !f.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(f.readAll()).object();
}
bool writeFile(const QString &path,const QByteArray &data) {
    QFile f(path);
    if (QFile::exists(path)) QFile::remove(path);
    return f.open(QIODevice::WriteOnly|QIODevice::NewOnly) && f.setPermissions(QFileDevice::ReadOwner|QFileDevice::WriteOwner)
        && f.write(data)==data.size() && f.flush();
}
qint64 now() {return QDateTime::currentSecsSinceEpoch();}
QString amountText(const QString &atoms,int places) {
    QString d=atoms;while (d.size()<=places) d.prepend('0');
    QString whole=d.left(d.size()-places),frac=d.mid(d.size()-places);
    while (frac.endsWith('0')) frac.chop(1);
    return frac.isEmpty() ? whole : whole+"."+frac;
}
}

BtcMarketJob::BtcMarketJob(QObject *parent):QObject(parent),m_network(new QNetworkAccessManager(this)) {}

bool BtcMarketJob::configure(const QString &path) {
    if (!m_path.isEmpty()) return false;
    const auto doc=QJsonDocument::fromJson(LitecoinJournalProtocol::privateConfig(path,65536));
    const auto c=doc.object();
    const QSet<QString> keys{"schema","network","relays","tradesDir","journalHelper","local","params","policy"};
    const auto names=c.keys();
    if (!doc.isObject() || QSet<QString>(names.cbegin(),names.cend())!=keys || c.value("schema")!="monzero-private-btc-market-v1"
        || pairOf(c.value("network").toString()).isEmpty() || !QDir::isAbsolutePath(c.value("tradesDir").toString())
        || !QFile::exists(c.value("journalHelper").toString()) || !c.value("local").isObject() || !c.value("params").isObject()
        || !c.value("policy").isObject()) return false;
    QStringList relays;
    for (const auto &r:c.value("relays").toArray()) {if (!relayUrl(r.toString())) return false;relays<<r.toString();}
    if (relays.isEmpty() || relays.size()>8) return false;
    const auto params=c.value("params").toObject();
    if (params.value("ltcNetwork")!=c.value("network") || c.value("policy").toObject().value("ltc_network")!=c.value("network")) return false;
    m_path=path;m_config=c;m_relays=relays;
    QDir().mkpath(c.value("tradesDir").toString());
    QFile::setPermissions(c.value("tradesDir").toString(),QFileDevice::ReadOwner|QFileDevice::WriteOwner|QFileDevice::ExeOwner);
    scanFolders();m_state="ready";emit changed();return true;
}

void BtcMarketJob::scanFolders() {
    m_folders.clear();
    const QString root=m_config.value("tradesDir").toString();
    for (const auto &name:QDir(root).entryList(QDir::Dirs|QDir::NoDotAndDotDot,QDir::Name)) {
        if (!name.startsWith("btc-")) continue;
        Folder f;f.path=root+"/"+name;
        const bool maker=QFile::exists(f.path+"/offer.json");
        if (!maker && !QFile::exists(f.path+"/take.json")) continue;
        f.kind=maker ? "offer" : "take";
        const auto record=readJson(f.path+(maker ? "/offer.json" : "/take.json"));
        f.listing=maker ? record.value("signed").toObject().value("listing").toObject() : record.value("listing").toObject();
        f.listingId=f.listing.value("id").toString();
        f.state=QFile::exists(f.path+"/app/app.json") ? "agreed" : QFile::exists(f.path+"/cancelled") ? "cancelled"
            : qint64(f.listing.value("expiresAt").toDouble())<=now() ? "expired" : "open";
        m_folders.append(f);
    }
}

void BtcMarketJob::request(const QString &path,const QJsonObject &body,std::function<void(const Replies&)> done,bool post) {
    auto replies=std::make_shared<Replies>();
    auto left=std::make_shared<int>(m_relays.size());
    ++m_outstanding;emit changed();
    QPointer<BtcMarketJob> self(this);
    for (const auto &relay:m_relays) {
        QNetworkRequest req(QUrl(relay+path));
        req.setHeader(QNetworkRequest::ContentTypeHeader,"application/json");
        req.setTransferTimeout(30000);
        auto *reply=post ? m_network->post(req,QJsonDocument(body).toJson(QJsonDocument::Compact)) : m_network->get(req);
        connect(reply,&QNetworkReply::finished,this,[self,reply,relay,replies,left,done]{
            reply->deleteLater();
            if (!self) return;
            if (reply->error()==QNetworkReply::NoError) replies->append({relay,reply->read(64*1024*1024)});
            if (--*left>0) return;
            --self->m_outstanding;
            done(*replies);
            emit self->changed();
        });
    }
}

bool BtcMarketJob::refreshListings() {
    if (m_path.isEmpty()) return false;
    request("/v1/utxo/listings",{},[this](const Replies &replies){
        m_signed.clear();m_listings.clear();
        for (const auto &r:replies) for (const auto &v:QJsonDocument::fromJson(r.second).object().value("listings").toArray()) {
            const auto signedListing=v.toObject();
            const auto l=checkListing(signedListing,now());
            if (l.isEmpty() || l.value("network")!=network() || m_signed.contains(l.value("id").toString())) continue;
            m_signed.insert(l.value("id").toString(),signedListing);
            const auto sats=l.value("coin").toString(),atoms=l.value("xmz").toString();
            m_listings.append(QVariantMap{{"id",l.value("id")},{"side",l.value("side")},{"btc",amountText(sats,8)},
                {"xmz",amountText(atoms,11)},{"btcSats",sats},{"xmzAtoms",atoms},
                {"price",QString::number(sats.toDouble()/1e8/(atoms.toDouble()/1e11),'g',6)},
                {"expiresAt",l.value("expiresAt").toDouble()},{"pair",pairOf(network())}});
        }
        m_state=replies.isEmpty() ? "failed" : "ok";m_error=replies.isEmpty() ? tr("No relay answered.") : QString();
    },false);
    return true;
}

QVariantMap BtcMarketJob::listingAmounts(const QString &id) const {
    const auto l=m_signed.value(id).value("listing").toObject();
    return {{"btcSats",l.value("coin")},{"xmzAtoms",l.value("xmz")},{"side",l.value("side")}};
}

bool BtcMarketJob::newIdentity(const QString &dir,const QString &role,QJsonObject *card) {
    if (!QDir().mkpath(dir) || !QFile::setPermissions(dir,QFileDevice::ReadOwner|QFileDevice::WriteOwner|QFileDevice::ExeOwner)) return false;
    OfferIdentity identity;
    QByteArray passphrase(32,'\0');randombytes_buf(passphrase.data(),32);
    auto passHex=passphrase.toHex();sodium_memzero(passphrase.data(),32);
    const auto blob=identity.encrypt(passHex);
    QByteArray mailbox(32,'\0');randombytes_buf(mailbox.data(),32);
    auto mailboxHex=mailbox.toHex();
    const auto mailboxPub=mailboxPublic(mailbox);sodium_memzero(mailbox.data(),32);
    const bool ok=!blob.isEmpty() && writeFile(dir+"/identity.bin",blob) && writeFile(dir+"/identity.passphrase",passHex)
        && writeFile(dir+"/mailbox.key",mailboxHex);
    sodium_memzero(passHex.data(),passHex.size());sodium_memzero(mailboxHex.data(),mailboxHex.size());
    *card=QJsonObject{{"schema",CardSchema},{"role",role},{"funderKey",QString::fromLatin1(identity.publicKey().toHex())},
        {"mailbox",QString::fromLatin1(mailboxPub.toHex())}};
    return ok && writeFile(dir+"/card.json",canonical(*card));
}

QByteArray BtcMarketJob::signTerms(const QString &dir,const QByteArray &payload,const QString &role,QString *error) const {
    auto pass=LitecoinJournalProtocol::privateConfig(dir+"/identity.passphrase");
    QFile f(dir+"/identity.bin");f.open(QIODevice::ReadOnly);
    auto identity=OfferIdentity::decrypt(f.readAll(),pass);
    sodium_memzero(pass.data(),pass.size());
    if (!identity) {if (error) *error=tr("The trade identity could not be opened.");return {};}
    return identity->sign(payload,signingDomain(role).toLatin1());
}

void BtcMarketJob::sendLetter(const QString &toHex,const QJsonObject &letter) {
    const auto sealed=seal(QByteArray::fromHex(toHex.toLatin1()),canonical(letter));
    if (sealed.isEmpty()) return;
    request("/v1/mail",{{"to","0x"+toHex},{"sealed","0x"+QString::fromLatin1(sealed.toHex())}},[](const Replies&){},true);
}

bool BtcMarketJob::post(const QString &side,const QString &btcSats,const QString &xmzAtoms,int lifetimeHours) {
    static const QRegularExpression digits("\\A[1-9][0-9]{0,18}\\z");
    if (m_path.isEmpty() || (side!="sell-xmz" && side!="buy-xmz") || !digits.match(btcSats).hasMatch() || !digits.match(xmzAtoms).hasMatch()
        || lifetimeHours<1 || lifetimeHours>24) return false;
    const QString role=side=="sell-xmz" ? "xmz" : "ltc";
    const auto params=m_config.value("params").toObject();
    for (const auto &f:fields()) {
        static const QSet<QString> variable{"schema","quoteId","swapId","ltcFunder","xmzFunder","appFeeXmzAtoms","ltcAtoms","xmzAtoms","expiresAt"};
        if (!variable.contains(f) && !ownAddresses(otherRole(role)).contains(f) && !params.contains(f)) {fail(tr("The offer settings lack %1.").arg(f));return false;}
    }
    const auto dir=m_config.value("tradesDir").toString()+"/btc-offer-"+QString::number(QRandomGenerator::system()->generate64(),16);
    QJsonObject card;
    if (!newIdentity(dir,role,&card)) {fail(tr("The trade identity could not be created."));return false;}
    QByteArray id(32,'\0');randombytes_buf(id.data(),32);
    const QJsonObject listing{{"schema",ListingSchema},{"id","0x"+QString::fromLatin1(id.toHex())},{"pair",pairOf(network())},{"network",network()},
        {"side",side},{"coin",btcSats},{"xmz",xmzAtoms},{"makerKey",card.value("funderKey")},{"mailbox",card.value("mailbox")},
        {"createdAt",double(now())},{"expiresAt",double(now()+qint64(lifetimeHours)*3600)}};
    auto pass=LitecoinJournalProtocol::privateConfig(dir+"/identity.passphrase");
    QFile f(dir+"/identity.bin");f.open(QIODevice::ReadOnly);
    auto identity=OfferIdentity::decrypt(f.readAll(),pass);sodium_memzero(pass.data(),pass.size());
    if (!identity) {fail(tr("The trade identity could not be opened."));return false;}
    const QJsonObject signedListing{{"listing",listing},{"signature",QString::fromLatin1(identity->signSwapOffer(canonical(listing)).toHex())}};
    if (checkListing(signedListing).isEmpty() || !writeFile(dir+"/offer.json",canonical({{"signed",signedListing}}))) {fail(tr("The offer could not be signed."));return false;}
    request("/v1/utxo/listings",signedListing,[this](const Replies &replies){
        if (replies.isEmpty()) fail(tr("No relay took the offer; it stays saved and can be posted again."));
        else {m_state="ok";m_error.clear();}
    },true);
    scanFolders();emit changed();return true;
}

bool BtcMarketJob::take(const QString &listingId) {
    const auto signedListing=m_signed.value(listingId);
    const auto l=checkListing(signedListing,now());
    if (m_path.isEmpty() || l.isEmpty()) return false;
    for (const auto &f:m_folders) if (f.listingId==listingId) return false;
    const QString role=otherRole(l.value("side")=="sell-xmz" ? "xmz" : "ltc");
    QJsonObject addresses;
    for (const auto &k:ownAddresses(role)) {
        const auto v=m_config.value("params").toObject().value(k).toString();
        if (v.isEmpty()) {fail(tr("Set %1 in the BTC trade settings first.").arg(k));return false;}
        addresses.insert(k,v);
    }
    const auto dir=m_config.value("tradesDir").toString()+"/btc-take-"+listingId.mid(2,16);
    QJsonObject card;
    if (!newIdentity(dir,role,&card) || !writeFile(dir+"/take.json",canonical({{"listing",l},{"addresses",addresses}}))) {
        fail(tr("The trade identity could not be created."));return false;
    }
    m_takeSentAt.insert(dir,now());
    sendLetter(l.value("mailbox").toString(),{{"kind","btc-take"},{"listing",listingId},{"card",card},{"addresses",addresses},{"replyTo",card.value("mailbox")}});
    scanFolders();emit changed();return true;
}

bool BtcMarketJob::cancel(const QString &folder) {
    for (const auto &f:m_folders) {
        if (f.path!=folder || f.kind!="offer" || f.state!="open" || QFile::exists(folder+"/sent-terms.json")) continue;
        const auto card=readJson(folder+"/card.json");
        const QJsonObject c{{"schema",CancelSchema},{"id",f.listingId},{"makerKey",card.value("funderKey")},{"at",double(now())}};
        auto pass=LitecoinJournalProtocol::privateConfig(folder+"/identity.passphrase");
        QFile file(folder+"/identity.bin");file.open(QIODevice::ReadOnly);
        auto identity=OfferIdentity::decrypt(file.readAll(),pass);sodium_memzero(pass.data(),pass.size());
        if (!identity) return false;
        request("/v1/utxo/cancel",{{"cancel",c},{"signature",QString::fromLatin1(identity->signSwapCancel(canonical(c)).toHex())}},[](const Replies&){},true);
        writeFile(folder+"/cancelled",QByteArray("cancelled\n"));
        scanFolders();emit changed();return true;
    }
    return false;
}

bool BtcMarketJob::step() {
    if (m_path.isEmpty() || m_outstanding>0) return false;
    bool any=false;
    for (const auto &f:m_folders) {
        if (f.state!="open") continue;
        any=true;
        const auto path=f.path;
        auto secretHex=LitecoinJournalProtocol::privateConfig(path+"/mailbox.key");
        auto secret=QByteArray::fromHex(secretHex.trimmed());sodium_memzero(secretHex.data(),secretHex.size());
        const auto own=mailboxPublic(secret);sodium_memzero(secret.data(),secret.size());
        request("/v1/mail?to=0x"+QString::fromLatin1(own.toHex()),{},[this,path](const Replies &replies){
            auto hexKey=LitecoinJournalProtocol::privateConfig(path+"/mailbox.key");
            auto key=QByteArray::fromHex(hexKey.trimmed());sodium_memzero(hexKey.data(),hexKey.size());
            QSet<QByteArray> seen;QList<QJsonObject> letters;
            for (const auto &r:replies) for (const auto &v:QJsonDocument::fromJson(r.second).object().value("mail").toArray()) {
                auto h=v.toObject().value("sealed").toString();if (h.startsWith("0x")) h=h.mid(2);
                const auto sealed=QByteArray::fromHex(h.toLatin1());
                if (seen.contains(sealed)) continue;
                seen.insert(sealed);
                const auto plain=open(key,sealed);
                if (!plain.isEmpty()) letters<<QJsonDocument::fromJson(plain).object();
            }
            sodium_memzero(key.data(),key.size());
            for (auto &f:m_folders) {
                if (f.path!=path || f.state!="open") continue;
                for (const auto &letter:letters) {
                    if (letter.value("listing")!=f.listingId) continue;
                    if (f.kind=="offer") handleMaker(f,letter); else handleTaker(f,letter);
                    if (f.state!="open") break;
                }
                // A take whose answer has not come: ask again now and then (our letter may be lost)
                if (f.kind=="take" && f.state=="open" && now()-m_takeSentAt.value(path,0)>=ResendSeconds) {
                    const auto record=readJson(path+"/take.json");const auto card=readJson(path+"/card.json");
                    m_takeSentAt.insert(path,now());
                    sendLetter(f.listing.value("mailbox").toString(),{{"kind","btc-take"},{"listing",f.listingId},{"card",card},
                        {"addresses",record.value("addresses")},{"replyTo",card.value("mailbox")}});
                }
            }
        },false);
    }
    return any;
}

void BtcMarketJob::handleMaker(Folder &f,const QJsonObject &letter) {
    const auto ownCard=readJson(f.path+"/card.json");
    const QString role=ownCard.value("role").toString(),takerRole=otherRole(role);
    const auto policy=m_config.value("policy").toObject();
    const bool sent=QFile::exists(f.path+"/sent-terms.json");
    if (letter.value("kind")=="btc-take") {
        const auto card=letter.value("card").toObject(),addresses=letter.value("addresses").toObject();
        const auto reply=letter.value("replyTo").toString();
        if (sent) {
            // Asked again by the same taker (our answer was lost): the same signed terms, to it only
            const auto record=readJson(f.path+"/sent-terms.json");
            if (card.value("funderKey")==record.value("takerCard").toObject().value("funderKey") && reply==record.value("takerCard").toObject().value("mailbox").toString())
                sendLetter(reply,{{"kind","btc-terms"},{"listing",f.listingId},{"terms",record.value("terms")},{"signature",record.value("signature")},
                    {"makerCard",ownCard},{"policy",policy}});
            return;
        }
        QStringList given=addresses.keys(),wanted=ownAddresses(takerRole);given.sort();wanted.sort();
        if (card.value("schema")!=CardSchema || card.value("role")!=takerRole || card.value("funderKey")==ownCard.value("funderKey")
            || given!=wanted || card.value("mailbox")!=reply || reply.size()!=64) return;
        QString error;
        auto params=m_config.value("params").toObject();
        for (const auto &k:ownAddresses(takerRole)) params.remove(k);   // the taker's own come from the take
        const auto payload=buildTerms(role,f.listing,params,ownCard,card,addresses,now(),&error);
        const auto signature=payload.isEmpty() ? QByteArray() : signTerms(f.path,payload,role,&error);
        if (signature.isEmpty()) {f.detail=error;return;}
        writeFile(f.path+"/sent-terms.json",canonical({{"terms",QString::fromUtf8(payload)},{"signature",QString::fromLatin1(signature.toHex())},{"takerCard",card}}));
        sendLetter(reply,{{"kind","btc-terms"},{"listing",f.listingId},{"terms",QString::fromUtf8(payload)},
            {"signature",QString::fromLatin1(signature.toHex())},{"makerCard",ownCard},{"policy",policy}});
        f.detail=tr("Taken; waiting for the taker's signature.");
    } else if (letter.value("kind")=="btc-signature" && sent) {
        const auto record=readJson(f.path+"/sent-terms.json");
        const auto payload=record.value("terms").toString().toUtf8();
        const auto takerCard=record.value("takerCard").toObject();
        const auto signature=QByteArray::fromHex(letter.value("signature").toString().toLatin1());
        if (!verifyTerms(payload,takerRole,QByteArray::fromHex(takerCard.value("funderKey").toString().toLatin1()),signature)) return;
        finishTrade(f,payload,record.value("signature").toString(),letter.value("signature").toString(),takerCard,policy);
    }
}

void BtcMarketJob::handleTaker(Folder &f,const QJsonObject &letter) {
    if (letter.value("kind")!="btc-terms") return;
    const auto ownCard=readJson(f.path+"/card.json");
    const QString role=ownCard.value("role").toString(),makerRole=otherRole(role);
    const auto makerCard=letter.value("makerCard").toObject();
    if (makerCard.value("funderKey")!=f.listing.value("makerKey") || makerCard.value("mailbox")!=f.listing.value("mailbox")) return;
    const auto payload=letter.value("terms").toString().toUtf8();
    const auto policy=letter.value("policy").toObject();
    const auto refusal=termsRefusal(payload,f.listing,role,ownCard,readJson(f.path+"/take.json").value("addresses").toObject(),policy);
    if (!refusal.isEmpty()) {f.detail=refusal;return;}
    if (policy.value("ltc_network")!=network()) {f.detail=tr("The maker's policy is for another network.");return;}
    const auto makerSignature=QByteArray::fromHex(letter.value("signature").toString().toLatin1());
    if (!verifyTerms(payload,makerRole,QByteArray::fromHex(f.listing.value("makerKey").toString().toLatin1()),makerSignature)) {
        f.detail=tr("The maker's signature does not verify.");return;
    }
    QString error;
    const auto own=signTerms(f.path,payload,role,&error);
    if (own.isEmpty()) {f.detail=error;return;}
    sendLetter(f.listing.value("mailbox").toString(),{{"kind","btc-signature"},{"listing",f.listingId},{"signature",QString::fromLatin1(own.toHex())}});
    finishTrade(f,payload,QString::fromLatin1(own.toHex()),letter.value("signature").toString(),makerCard,policy);
}

void BtcMarketJob::finishTrade(Folder &f,const QByteArray &payload,const QString &ownSignature,const QString &peerSignature,
        const QJsonObject &peerCard,const QJsonObject &policy) {
    const auto role=readJson(f.path+"/card.json").value("role").toString();
    const QString ltcSig=role=="ltc" ? ownSignature : peerSignature,xmzSig=role=="ltc" ? peerSignature : ownSignature;
    const auto quotePath=f.path+"/start-quote.json";
    QString error;
    const auto terms=parseTerms(payload);
    if (terms.isEmpty() || !writeFile(quotePath,envelope(payload,ltcSig,xmzSig)) || !writeFile(f.path+"/peer-card.json",canonical(peerCard))
        || !writeFile(f.path+"/policy.json",canonical(policy))) {f.detail=tr("The signed quote could not be saved.");return;}
    const auto app=writeAppFolder(f.path+"/app",role,terms,quotePath,f.path,peerCard,policy,m_config.value("local").toObject(),
        m_config.value("journalHelper").toString(),m_relays,&error);
    if (app.isEmpty()) {f.detail=error;return;}
    f.state="agreed";f.detail=tr("Agreed: review and start the trade.");
    emit changed();emit agreed(app);
}

void BtcMarketJob::fail(const QString &why) {m_state="failed";m_error=why;emit changed();}

QVariantMap BtcMarketJob::status() const {
    QVariantList mine;
    for (const auto &f:m_folders)
        mine.append(QVariantMap{{"folder",f.path},{"kind",f.kind},{"state",f.state},{"detail",f.detail},{"id",f.listingId},
            {"side",f.listing.value("side")},{"btc",amountText(f.listing.value("coin").toString(),8)},
            {"xmz",amountText(f.listing.value("xmz").toString(),11)},{"app",f.state=="agreed" ? f.path+"/app/app.json" : QString()}});
    return {{"configured",!m_path.isEmpty()},{"busy",m_outstanding>0},{"state",m_state},{"error",m_error},{"network",network()},
        {"coin",pairOf(network()).left(3)},
        {"listings",m_listings},{"mine",mine},{"relays",QJsonArray::fromStringList(m_relays).toVariantList()}};
}
