#include "BtcTradeSetup.h"
#include "OfferIdentity.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QFileInfo>
#include <QUuid>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSet>
#include <sodium.h>
#ifdef Q_OS_UNIX
#include <unistd.h>
#endif

namespace BtcTradeSetup {
namespace {
const QByteArray MailSchema("monzero-dex-mail/1");
bool hex(const QString &value,int bytes) {
    static const QRegularExpression pattern("\\A[0-9a-f]+\\z");
    return value.size()==2*bytes && pattern.match(value).hasMatch();
}
QString hex64(const QJsonValue &v) {
    auto s=v.toString();if (s.startsWith("0x")) s=s.mid(2);
    return hex(s,32) ? s : QString();
}
bool digits(const QJsonValue &v) {
    static const QRegularExpression pattern("\\A[1-9][0-9]{0,19}\\z");
    return v.isString() && pattern.match(v.toString()).hasMatch();
}
QByteArray mailKey(const QByteArray &shared,const QByteArray &sender,const QByteArray &recipient) {
    return QCryptographicHash::hash(MailSchema+shared+sender+recipient,QCryptographicHash::Sha256);
}
bool writePrivate(const QString &path,const QByteArray &data,QFileDevice::Permissions mode) {
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly|QIODevice::NewOnly) || !f.setPermissions(mode) || f.write(data)!=data.size() || !f.flush()) return false;
#ifdef Q_OS_UNIX
    if (::fsync(f.handle())!=0) return false;
#endif
    return true;
}
}

QStringList fields() {
    return {"schema","quoteId","swapId","ltcFunder","xmzFunder","ltcGenesis","xmzGenesis","ltcNetwork","xmzNetwork",
        "ltcAtoms","xmzAtoms","ltcPayout","ltcRefund","xmzPayout","xmzRefund","ltcFundingFeeMaxAtoms","xmzNativeFeeMaxAtoms",
        "ltcCancelFeeAtoms","ltcRefundFeeAtoms","ltcRedeemFeeAtoms","ltcPunishFeeAtoms","ltcEarlyRefundFeeAtoms",
        "ltcConfirmations","xmzConfirmations","cancelBlocks","punishBlocks","expiresAt","appFeeXmzAtoms",
        "ltcMinimumOutputAtoms","ltcMinimumRelayFeeAtomsPerKb","xmzPayoutAtoms","xmzClaimFeeMaxAtoms","xmzRefundFeeMaxAtoms"};
}
QStringList ownAddresses(const QString &role) {
    return role=="ltc" ? QStringList{"ltcRefund","xmzPayout"} : QStringList{"ltcPayout","xmzRefund"};
}
QString otherRole(const QString &role) {return role=="ltc" ? QStringLiteral("xmz") : QStringLiteral("ltc");}
QString roleName(const QString &role) {return role=="ltc" ? QStringLiteral("litecoin-funder") : QStringLiteral("native-funder");}
QString signingDomain(const QString &role) {return QString(TermsSchema)+(role=="ltc" ? "-ltc-funder" : "-xmz-funder");}

QByteArray canonical(const QJsonObject &object) {return QJsonDocument(object).toJson(QJsonDocument::Compact);}

QByteArray mailboxPublic(const QByteArray &secret) {
    if (secret.size()!=32 || sodium_init()<0) return {};
    QByteArray out(32,'\0');
    crypto_scalarmult_base(reinterpret_cast<unsigned char*>(out.data()),reinterpret_cast<const unsigned char*>(secret.constData()));
    return out;
}

QByteArray seal(const QByteArray &to,const QByteArray &plain) {
    if (to.size()!=32 || sodium_init()<0) return {};
    QByteArray secret(32,'\0'),shared(32,'\0');
    randombytes_buf(secret.data(),32);
    const auto sender=mailboxPublic(secret);
    const bool ok=crypto_scalarmult(reinterpret_cast<unsigned char*>(shared.data()),reinterpret_cast<const unsigned char*>(secret.constData()),
        reinterpret_cast<const unsigned char*>(to.constData()))==0;
    sodium_memzero(secret.data(),32);
    if (!ok) return {};
    auto key=mailKey(shared,sender,to);sodium_memzero(shared.data(),32);
    QByteArray body(plain.size()+crypto_aead_chacha20poly1305_IETF_ABYTES,'\0');
    unsigned long long length=0;
    const QByteArray nonce(12,'\0');
    crypto_aead_chacha20poly1305_ietf_encrypt(reinterpret_cast<unsigned char*>(body.data()),&length,
        reinterpret_cast<const unsigned char*>(plain.constData()),quint64(plain.size()),nullptr,0,nullptr,
        reinterpret_cast<const unsigned char*>(nonce.constData()),reinterpret_cast<const unsigned char*>(key.constData()));
    sodium_memzero(key.data(),key.size());
    body.resize(int(length));
    return sender+body;
}

QByteArray open(const QByteArray &secret,const QByteArray &sealed) {
    if (secret.size()!=32 || sealed.size()<48 || sodium_init()<0) return {};
    const auto sender=sealed.left(32),own=mailboxPublic(secret);
    QByteArray shared(32,'\0');
    if (crypto_scalarmult(reinterpret_cast<unsigned char*>(shared.data()),reinterpret_cast<const unsigned char*>(secret.constData()),
            reinterpret_cast<const unsigned char*>(sender.constData()))!=0) return {};
    auto key=mailKey(shared,sender,own);sodium_memzero(shared.data(),32);
    QByteArray plain(sealed.size()-32,'\0');
    unsigned long long length=0;
    const QByteArray nonce(12,'\0');
    const bool ok=crypto_aead_chacha20poly1305_ietf_decrypt(reinterpret_cast<unsigned char*>(plain.data()),&length,nullptr,
        reinterpret_cast<const unsigned char*>(sealed.constData()+32),quint64(sealed.size()-32),nullptr,0,
        reinterpret_cast<const unsigned char*>(nonce.constData()),reinterpret_cast<const unsigned char*>(key.constData()))==0;
    sodium_memzero(key.data(),key.size());
    if (!ok) return {};
    plain.resize(int(length));
    return plain;
}

QJsonObject checkListing(const QJsonObject &signedListing,qint64 now) {
    const auto l=signedListing.value("listing").toObject();
    const QSet<QString> expected{"schema","id","pair","network","side","btc","xmz","makerKey","mailbox","createdAt","expiresAt"};
    const auto names=l.keys();
    if (signedListing.size()!=2 || QSet<QString>(names.cbegin(),names.cend())!=expected) return {};
    static const QStringList networks{"btc-mainnet","btc-signet","btc-testnet4","btc-regtest"};
    if (l.value("schema")!=ListingSchema || l.value("pair")!="BTC/XMZ" || !networks.contains(l.value("network").toString())
        || (l.value("side")!="sell-xmz" && l.value("side")!="buy-xmz") || !digits(l.value("btc")) || !digits(l.value("xmz"))
        || hex64(l.value("id")).isEmpty() || !hex(l.value("makerKey").toString(),32) || !hex(l.value("mailbox").toString(),32)
        || !l.value("createdAt").isDouble() || !l.value("expiresAt").isDouble()) return {};
    const auto expires=qint64(l.value("expiresAt").toDouble());
    if (now && expires<=now) return {};
    const auto signature=QByteArray::fromHex(signedListing.value("signature").toString().toLatin1());
    if (!OfferIdentity::verifyBtcOffer(canonical(l),QByteArray::fromHex(l.value("makerKey").toString().toLatin1()),signature)) return {};
    return l;
}

QByteArray buildTerms(const QString &makerRole,const QJsonObject &listing,const QJsonObject &params,
        const QJsonObject &makerCard,const QJsonObject &takerCard,const QJsonObject &takerAddresses,qint64 now,QString *error) {
    QJsonObject cards{{makerRole,makerCard},{otherRole(makerRole),takerCard}};
    QJsonObject terms{{"schema",TermsSchema},{"quoteId",QUuid::createUuid().toString(QUuid::WithoutBraces)},
        {"swapId",QUuid::createUuid().toString(QUuid::WithoutBraces)},{"appFeeXmzAtoms","0"},
        {"ltcAtoms",listing.value("btc")},{"xmzAtoms",listing.value("xmz")},{"expiresAt",QString::number(now+QuoteSeconds)},
        {"ltcFunder",cards.value("ltc").toObject().value("funderKey")},{"xmzFunder",cards.value("xmz").toObject().value("funderKey")}};
    for (const auto &f:fields()) {
        if (terms.contains(f)) continue;
        const auto v=takerAddresses.contains(f) ? takerAddresses.value(f) : params.value(f);
        if (v.isUndefined() || v.isNull() || v.toVariant().toString().isEmpty()) {if (error) *error="no value for "+f;return {};}
        terms.insert(f,v.isString() ? v.toString() : v.toVariant().toString());
    }
    return canonical(terms);
}

QJsonObject parseTerms(const QByteArray &payload) {
    const auto doc=QJsonDocument::fromJson(payload);
    const auto t=doc.object();
    if (!doc.isObject() || canonical(t)!=payload || t.size()!=fields().size() || t.value("schema")!=TermsSchema) return {};
    for (const auto &f:fields()) if (!t.value(f).isString()) return {};
    return t;
}

QString termsRefusal(const QByteArray &payload,const QJsonObject &listing,const QString &ownRole,
        const QJsonObject &ownCard,const QJsonObject &ownAddresses,const QJsonObject &policy) {
    const auto t=parseTerms(payload);
    if (t.isEmpty()) return QStringLiteral("the terms are not a canonical quote");
    const auto makerRole=otherRole(ownRole);
    QJsonObject expected{{"ltcAtoms",listing.value("btc")},{"xmzAtoms",listing.value("xmz")},{"ltcNetwork",listing.value("network")},
        {ownRole+"Funder",ownCard.value("funderKey")},{makerRole+"Funder",listing.value("makerKey")},{"appFeeXmzAtoms","0"}};
    for (auto it=ownAddresses.begin();it!=ownAddresses.end();++it) expected.insert(it.key(),it.value());
    QStringList wrong;
    for (auto it=expected.begin();it!=expected.end();++it) if (t.value(it.key())!=it.value()) wrong<<it.key();
    // The policy the apps will run must say what the terms say
    const QList<QPair<QString,QString>> same{{"ltc_network","ltcNetwork"},{"ltc_confirmations","ltcConfirmations"},
        {"xmz_confirmations","xmzConfirmations"},{"cancel_blocks","cancelBlocks"},{"punish_blocks","punishBlocks"},{"xmz_genesis","xmzGenesis"}};
    for (const auto &p:same) if (policy.value(p.first).toVariant().toString()!=t.value(p.second).toString()) wrong<<("policy "+p.first);
    const auto expires=t.value("expiresAt").toString().toLongLong();
    if (expires<=QDateTime::currentSecsSinceEpoch()) wrong<<QStringLiteral("expiresAt");
    return wrong.isEmpty() ? QString() : QStringLiteral("the terms differ from the offer: ")+wrong.join(", ");
}

bool verifyTerms(const QByteArray &payload,const QString &role,const QByteArray &publicKey,const QByteArray &signature) {
    return OfferIdentity::verify(payload,signingDomain(role).toLatin1(),publicKey,signature);
}

QByteArray envelope(const QByteArray &payload,const QString &ltcSignatureHex,const QString &xmzSignatureHex) {
    return canonical({{"schema",EnvelopeSchema},{"payload",QString::fromUtf8(payload)},
        {"ltcSignature",ltcSignatureHex},{"xmzSignature",xmzSignatureHex}});
}

QString writeAppFolder(const QString &appDir,const QString &role,const QJsonObject &terms,const QString &quotePath,
        const QString &identityDir,const QJsonObject &peerCard,const QJsonObject &policy,const QJsonObject &local,
        const QString &journalHelper,const QStringList &relays,QString *error) {
    const auto fail=[error](const QString &why){if (error) *error=why;return QString();};
    if ((role!="ltc" && role!="xmz") || QFileInfo::exists(appDir) || !QDir::isAbsolutePath(appDir)) return fail("bad app folder or role");
    QFile helperFile(journalHelper);
    if (!helperFile.open(QIODevice::ReadOnly)) return fail("cannot read the journal helper");
    const auto helper=helperFile.readAll();
    if (helper.isEmpty()) return fail("the journal helper is empty");
    const auto ltc=local.value("litecoin").toObject(),xmz=local.value("xmz").toObject();
    const bool light=ltc.contains("light");
    const QStringList need=role=="xmz" ? QStringList{"daemonPort","claimWalletPort","claimLoginFile","sourceWalletPort","sourceLoginFile","sourceAddress","walletPasswordFile"}
                                       : QStringList{"daemonPort","claimWalletPort","claimLoginFile","observerWalletPort","observerLoginFile","walletPasswordFile"};
    for (const auto &k:need) if (!xmz.contains(k)) return fail("local xmz settings lack "+k);
    if (!light) for (const auto &k:{"rpcPort","wallet","anchor","cookieFile"}) if (!ltc.contains(k)) return fail(QStringLiteral("local litecoin settings lack ")+k);
    const auto peerMailbox=peerCard.value("mailbox").toString();
    if (!hex(peerMailbox,32) || relays.isEmpty()) return fail("the other side's card has no mailbox key");

    const QString staging=appDir+".staging-"+QString::number(QRandomGenerator::system()->generate64(),16);
    QDir().mkpath(staging);
    QFile::setPermissions(staging,QFileDevice::ReadOwner|QFileDevice::WriteOwner|QFileDevice::ExeOwner);
    const auto owner=QFileDevice::ReadOwner|QFileDevice::WriteOwner;
    bool ok=true;
    QStringList directories{"meta","secrets"};if (role=="ltc") directories<<"reservations";
    for (const auto &d:directories) {
        ok=ok && QDir(staging).mkdir(d) && QFile::setPermissions(staging+"/"+d,owner|QFileDevice::ExeOwner);
    }
    const auto add=[&](const QString &name,const QByteArray &data,QFileDevice::Permissions mode){
        ok=ok && writePrivate(staging+"/"+name,data,mode);return appDir+"/"+name;
    };
    const auto config=[&](const QString &name,const QJsonObject &value){return add(name,canonical(value),owner);};
    QJsonArray pins;
    for (const auto &k:{"ltcFunder","xmzFunder"}) {
        QJsonArray bytes;for (const auto b:QByteArray::fromHex(terms.value(k).toString().toLatin1())) bytes.append(int(quint8(b)));
        pins.append(bytes);
    }
    QJsonObject journal{{"schema","monzero-private-ltc-journal-v5"},
        {"helper",add("journal-helper",helper,owner|QFileDevice::ExeOwner)},
        {"helperDigest",QString::fromLatin1(QCryptographicHash::hash(helper,QCryptographicHash::Sha256).toHex())},
        {"metadataDirectory",appDir+"/meta"},{"snapshotDirectory",appDir+"/secrets"},{"tradeId",terms.value("swapId")},
        {"role",roleName(role)},{"pins",pins},{"policy",policy},{"startQuoteFile",quotePath}};
    const auto journalPath=config("journal.json",journal);
    auto phase=journal;phase.remove("startQuoteFile");
    QJsonObject ltcService=light ? QJsonObject{{"ltcLight",ltc.value("light")}}
        : QJsonObject{{"ltcPort",ltc.value("rpcPort")},{"ltcWallet",ltc.value("wallet")},{"ltcAnchor",ltc.value("anchor")},{"ltcCookieFile",ltc.value("cookieFile")}};
    const auto merged=[&](QJsonObject base,const QJsonObject &more){for (auto it=more.begin();it!=more.end();++it) base.insert(it.key(),it.value());return base;};
    const QJsonObject native{{"nativeDaemonPort",xmz.value("daemonPort")},{"nativeWalletPort",xmz.value("claimWalletPort")},{"nativeLoginFile",xmz.value("claimLoginFile")}};
    const QJsonObject observer=role=="xmz" ? QJsonObject{{"walletPort",xmz.value("sourceWalletPort")},{"loginFile",xmz.value("sourceLoginFile")}}
                                           : QJsonObject{{"walletPort",xmz.value("observerWalletPort")},{"loginFile",xmz.value("observerLoginFile")}};
    const QString purpose=role=="xmz" ? "refund" : "payout";
    const auto paymentRpc=config("payment-rpc.json",merged(merged(ltcService,native),{{"schema","monzero-private-ltc-"+purpose+"-rpc-v4"},
        {"preparationObserver",observer},{"walletPasswordFile",xmz.value("walletPasswordFile")}}));
    const auto payment=config("payment.json",merged(phase,{{"schema",role=="xmz" ? "monzero-private-ltc-journal-v2" : "monzero-private-ltc-journal-v3"},
        {purpose+"RpcFile",paymentRpc}}));
    QJsonObject manifest{{"schema","monzero-private-ltc-app-v1"},{"journalConfigFile",journalPath},{"paymentConfigFile",payment}};
    QString settlement;
    if (role=="xmz") {
        const auto fundingRpc=config("funding-rpc.json",merged(ltcService,{{"schema","monzero-private-ltc-funding-rpc-v2"},
            {"nativeDaemonPort",xmz.value("daemonPort")},{"nativeWalletPort",xmz.value("sourceWalletPort")},{"nativeLoginFile",xmz.value("sourceLoginFile")},
            {"source",QJsonObject{{"wallet",xmz.value("sourceAddress")},{"account",0},{"priority",1}}}}));
        settlement=config("funding.json",merged(phase,{{"schema","monzero-private-ltc-journal-v4"},{"fundingRpcFile",fundingRpc}}));
        manifest.insert("fundingConfigFile",settlement);
    } else {
        const auto reservationRpc=config("reservation-rpc.json",merged(ltcService,{{"schema","monzero-private-ltc-reservation-rpc-v1"},
            {"reservationDirectory",appDir+"/reservations"},{"feeRateAtomsPerKb",local.contains("feeRateAtomsPerKb") ? local.value("feeRateAtomsPerKb") : QJsonValue(1000)}}));
        manifest.insert("reservationConfigFile",config("reservation.json",{{"schema","monzero-private-ltc-reservation-v1"},
            {"journalConfigFile",journalPath},{"rpcFile",reservationRpc}}));
        const auto redeemRpc=config("redeem-rpc.json",merged(ltcService,{{"schema","monzero-private-ltc-redeem-rpc-v1"},
            {"nativeDaemonPort",xmz.value("daemonPort")},{"observerWalletPort",xmz.value("observerWalletPort")},
            {"observerLoginFile",xmz.value("observerLoginFile")},{"safetyBlocks",local.contains("safetyBlocks") ? local.value("safetyBlocks") : QJsonValue(2)}}));
        settlement=config("redeem.json",{{"schema","monzero-private-ltc-redeem-v1"},{"journalConfigFile",journalPath},{"rpcFile",redeemRpc}});
    }
    manifest.insert("peerConfigFile",config("peer.json",{{"schema","monzero-private-ltc-peer-relay-v1"},{"mode","relay"},
        {"relays",QJsonArray::fromStringList(relays)},{"mailboxSecretFile",identityDir+"/mailbox.key"},{"peerMailbox",peerMailbox},
        {"identityFile",identityDir+"/identity.bin"},{"passphraseFile",identityDir+"/identity.passphrase"},{"settlementConfigFile",settlement}}));
    config("app.json",manifest);
    if (!ok || !QDir().rename(staging,appDir)) {QDir(staging).removeRecursively();return fail("the app folder could not be written");}
    return appDir+"/app.json";
}
}
