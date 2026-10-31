#include "LitecoinAppConfig.h"
#include "LitecoinJournalJob.h"
#include "LitecoinJournalProtocol.h"
#include <QJsonDocument>
#include <QCryptographicHash>
#include <QFileInfo>
#include <QDir>
#include <QRegularExpression>
#include "OfferIdentity.h"
#include "DexDeployment.h"

bool LitecoinAppConfig::load(const QString &path,LitecoinAppConfig &result) {
    LitecoinAppConfig candidate;
    const auto read=[&candidate](const QString &file) {
        const auto bytes=LitecoinJournalProtocol::privateConfig(file);
        if (!bytes.isEmpty()) candidate.sources.insert(file,bytes);
        return bytes;
    };
    const auto bytes=read(path);const auto doc=QJsonDocument::fromJson(bytes);const auto c=doc.object();
    if (!doc.isObject() || doc.toJson(QJsonDocument::Compact)!=bytes || c.size()!=5
        || c.value("schema")!="monzero-private-ltc-app-v1") return false;
    for (const auto key:{"journalConfigFile","peerConfigFile","paymentConfigFile"})
        if (!c.value(key).isString() || c.value(key).toString().isEmpty()) return false;
    candidate.journal=c.value("journalConfigFile").toString();candidate.peer=c.value("peerConfigFile").toString();
    candidate.payment=c.value("paymentConfigFile").toString();
    LitecoinJournalJob startup;
    if (!startup.configure(candidate.journal) || !startup.status().value("startConfigured").toBool()) return false;
    const auto original=QJsonDocument::fromJson(read(candidate.journal)).object();
    if (read(original.value("startQuoteFile").toString()).isEmpty()) return false;
    const bool native=original.value("role")=="native-funder";
    const QString roleKey=native?"fundingConfigFile":"reservationConfigFile";
    if (!c.value(roleKey).isString() || c.value(roleKey).toString().isEmpty()) return false;
    const auto phase=[&](const QString &file,const QString &schema,const QString &rpcKey) {
        LitecoinJournalJob validator;
        if (!validator.configure(file)) return false;
        const auto phase=QJsonDocument::fromJson(read(file)).object();
        if (phase.value("schema")!=schema) return false;
        for (const auto key:{"tradeId","role","policy","pins","metadataDirectory","snapshotDirectory","helperDigest"})
            if (phase.value(key)!=original.value(key)) return false;
        return !read(phase.value(rpcKey).toString()).isEmpty();
    };
    const QString purpose=native?"refund":"payout";
    if (!phase(candidate.payment,native?"monzero-private-ltc-journal-v2":"monzero-private-ltc-journal-v3",purpose+"RpcFile")) return false;
    const auto payment=QJsonDocument::fromJson(candidate.sources.value(candidate.payment)).object();
    const auto paymentRpc=QJsonDocument::fromJson(candidate.sources.value(payment.value(purpose+"RpcFile").toString())).object();
    if (paymentRpc.value("schema")!="monzero-private-ltc-"+purpose+"-rpc-v4") return false;
    const auto peerBytes=read(candidate.peer);const auto peerDoc=QJsonDocument::fromJson(peerBytes);const auto peer=peerDoc.object();
    if (!peerDoc.isObject() || peerDoc.toJson(QJsonDocument::Compact)!=peerBytes
        || peer.value("schema")!="monzero-private-ltc-peer-v2" || !peer.value("settlementConfigFile").isString()) return false;
    if (native) {
        candidate.funding=c.value(roleKey).toString();
        if (peer.value("settlementConfigFile")!=candidate.funding
            || !phase(candidate.funding,"monzero-private-ltc-journal-v4","fundingRpcFile")) return false;
    } else {
        candidate.reservation=c.value(roleKey).toString();
        for (const auto file:{candidate.reservation,peer.value("settlementConfigFile").toString()}) {
            const auto wrapperBytes=read(file);const auto wrapperDoc=QJsonDocument::fromJson(wrapperBytes);const auto wrapper=wrapperDoc.object();
            if (!wrapperDoc.isObject() || wrapperDoc.toJson(QJsonDocument::Compact)!=wrapperBytes || wrapper.size()!=3
                || wrapper.value("journalConfigFile")!=candidate.journal || !wrapper.value("rpcFile").isString()
                || wrapper.value("schema")!=(file==candidate.reservation?"monzero-private-ltc-reservation-v1":"monzero-private-ltc-redeem-v1")
                || read(wrapper.value("rpcFile").toString()).isEmpty()) return false;
        }
    }
    result=std::move(candidate);return true;
}

QString LitecoinAppConfig::digest() const {
    QJsonObject hashes;
    for (auto it=sources.cbegin();it!=sources.cend();++it)
        hashes.insert(it.key(),QString::fromLatin1(QCryptographicHash::hash(it.value(),QCryptographicHash::Sha256).toHex()));
    const auto bytes=QByteArrayLiteral("MONZERO-LTC-APP-SOURCES-V1\n")+QJsonDocument(hashes).toJson(QJsonDocument::Compact);
    return QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex());
}

QVariantMap LitecoinAppConfig::review(const QString &path) {
    if (!QFileInfo(path).isAbsolute() || QDir::cleanPath(path)!=path) return {};
    LitecoinAppConfig config;
    if (!load(path,config)) return {};
    const auto journal=QJsonDocument::fromJson(config.sources.value(config.journal)).object();
    const auto envelope=QJsonDocument::fromJson(config.sources.value(journal.value("startQuoteFile").toString())).object();
    const auto payload=envelope.value("payload").toString().toUtf8();
    const auto doc=QJsonDocument::fromJson(payload);const auto terms=doc.object();
    const QStringList fields{"schema","quoteId","swapId","ltcFunder","xmzFunder","ltcGenesis","xmzGenesis",
        "ltcNetwork","xmzNetwork","ltcAtoms","xmzAtoms","ltcPayout","ltcRefund","xmzPayout","xmzRefund",
        "ltcFundingFeeMaxAtoms","xmzNativeFeeMaxAtoms","ltcCancelFeeAtoms","ltcRefundFeeAtoms",
        "ltcRedeemFeeAtoms","ltcPunishFeeAtoms","ltcEarlyRefundFeeAtoms","ltcConfirmations","xmzConfirmations",
        "cancelBlocks","punishBlocks","expiresAt","appFeeXmzAtoms","ltcMinimumOutputAtoms",
        "ltcMinimumRelayFeeAtomsPerKb","xmzPayoutAtoms","xmzClaimFeeMaxAtoms","xmzRefundFeeMaxAtoms"};
    if (!doc.isObject() || doc.toJson(QJsonDocument::Compact)!=payload || terms.size()!=fields.size()
        || terms.value("schema")!="monzero-native-ltc-xmz-quote-v2"
        || !DexDeployment::litecoinNetworkAllowed(terms.value("ltcNetwork").toString())
        || terms.value("ltcGenesis")!=DexDeployment::litecoinGenesis()) return {};
    static const QRegularExpression number("\\A(?:0|[1-9][0-9]{0,19})\\z");
    for (const auto &field:fields) {
        const auto text=terms.value(field).toString();
        if (!terms.value(field).isString() || text.isEmpty()) return {};
        if (field.endsWith("Atoms") || field.endsWith("Blocks") || field.endsWith("Confirmations") || field=="expiresAt"
            || field=="ltcMinimumRelayFeeAtomsPerKb") {
            bool ok=false;text.toULongLong(&ok);
            if (!ok || !number.match(text).hasMatch()) return {};
        }
    }
    const auto key=[](const QString &value,int bytes) {
        const auto decoded=QByteArray::fromHex(value.toLatin1());
        return decoded.size()==bytes && decoded.toHex()==value.toLatin1()?decoded:QByteArray{};
    };
    const auto ltc=key(terms.value("ltcFunder").toString(),32),xmz=key(terms.value("xmzFunder").toString(),32);
    if (ltc.isEmpty() || xmz.isEmpty() || ltc==xmz
        || !OfferIdentity::verify(payload,"monzero-native-ltc-xmz-quote-v2-ltc-funder",ltc,key(envelope.value("ltcSignature").toString(),64))
        || !OfferIdentity::verify(payload,"monzero-native-ltc-xmz-quote-v2-xmz-funder",xmz,key(envelope.value("xmzSignature").toString(),64))) return {};
    const auto peer=QJsonDocument::fromJson(config.sources.value(config.peer)).object();
    const auto role=journal.value("role").toString();
    return {{"appPath",path},{"commitment",config.digest()},{"tradeId",journal.value("tradeId").toString()},
        {"network",terms.value("ltcNetwork").toString()},{"role",role},{"terms",terms.toVariantMap()},
        {"peerKey",terms.value(role=="native-funder"?"ltcFunder":"xmzFunder").toString()},
        {"peerCertificateSha256",peer.value("peerCertificateSha256").toString()},
        {"helper",journal.value("helper").toString()},{"helperDigest",journal.value("helperDigest").toString()},
        {"signaturesVerified",true},{"executable",false}};
}
