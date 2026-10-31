#ifndef MONZERO_BCH_REFUND_WALLET_PROTOCOL_H
#define MONZERO_BCH_REFUND_WALLET_PROTOCOL_H
#include "BchNativePreparationProtocol.h"
namespace BchRefundWalletProtocol {
using namespace BchNativePreparationProtocol;
// Public structural/binding checks on the pinned helper's historical result.
// The engine validates retained private records and address provenance; this is
// not independent chain verification or wallet-operation authority.
inline bool status(const QJsonObject &value,const QJsonObject &config,const QJsonObject &record,
    const QString &refund,const QString &deployment) {
    QJsonObject admission;
    if (config.value("role")!="native-funder" || !expectedReview(config,record,&admission)
        || !txid(refund) || !hex(deployment,64) || deployment==QString(64,'0') || value.size()!=17) return false;
    const auto terms=record.value("terms").toObject();
    const auto trade=config.value("tradeId").toString();
    auto filename=QUuid(trade).toString(QUuid::WithoutBraces);filename.remove('-');filename.prepend("swap-");
    if (value.value("schema")!="monzero-bch-native-refund-wallet-status-v1"
        || value.value("swapId")!=trade || value.value("walletFile")!=filename
        || pin(value.value("quote"))!=config.value("quoteCommitment").toString()
        || pin(value.value("admission")).isEmpty() || !address(value.value("address"))
        || !txid(value.value("nativePayment")) || value.value("refundHint")!=refund
        || pin(value.value("bchDeployment"))!=deployment
        || value.value("bchNetwork")!=terms.value("bchNetwork")
        || value.value("bchGenesis")!=terms.value("bchGenesis")
        || value.value("xmzNetwork")!=terms.value("xmzNetwork")
        || value.value("chainObserved")!=QJsonValue(false) || value.value("executable")!=QJsonValue(false)) return false;
    const auto state=value.value("state").toString();
    if (state=="not-attempted") {if (!value.value("walletIntent").isNull()) return false;}
    else if (state=="attempted" || state=="ready") {if (pin(value.value("walletIntent")).isEmpty()) return false;}
    else return false;
    const QJsonObject binding{{"quote",value.value("quote")},{"admission",value.value("admission")},
        {"wallet",filename},{"address",value.value("address")},{"nativePayment",value.value("nativePayment")},{"refund",refund}};
    QByteArray bytes="monzero-bch-native-refund-wallet-intent-v1";bytes.append('\0');
    bytes+=QJsonDocument(binding).toJson(QJsonDocument::Compact);
    return pin(value.value("walletBinding"))==QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex());
}
// Validate public approval details against the independently selected local service.
// The engine still owns fresh chain verification and operation authorization.
inline bool review(const QJsonObject &value,const QJsonObject &config,const QJsonObject &record,
    const QString &refund,const QString &deployment,const QString &action,const QJsonObject &backend) {
    if (value.size()!=2 || !value.value("details").isObject()
        || (action!="restore" && action!="open" && action!="check")) return false;
    const auto d=value.value("details").toObject();
    const QString schema="monzero-bch-native-refund-wallet-review-v1";
    if (d.size()!=20 || d.value("schema")!=schema || d.value("action")!=action
        || !d.value("walletBackend").isObject() || d.value("walletBackend").toObject()!=backend
        || backend.size()!=4 || backend.value("schema")!="monzero-local-wallet-endpoint-v1"
        || backend.value("host")!="127.0.0.1" || d.value("restoreHeight")!=QJsonValue(0)) return false;
    quint64 daemonPort=0,walletPort=0;
    if (!integer(backend.value("daemonPort"),65535,&daemonPort) || !daemonPort
        || !integer(backend.value("walletPort"),65535,&walletPort) || !walletPort || daemonPort==walletPort) return false;
    const auto state=d.value("state").toString();
    if ((action=="restore" && state!="not-attempted")
        || (action!="restore" && state!="attempted" && state!="ready")) return false;
    const auto terms=record.value("terms").toObject(),policy=config.value("policy").toObject();
    for (const auto &fields:QList<QStringList>{{"minimumBchConfirmations","bchConfirmations","bch_confirmations"},
        {"minimumXmzConfirmations","xmzConfirmations","xmz_confirmations"}}) {
        quint64 actual=0,agreed=0;
        if (!integer(d.value(fields[0]),1000,&actual) || !actual || !decimal(terms.value(fields[1]),&agreed)
            || actual!=agreed || d.value(fields[0])!=policy.value(fields[2])) return false;
    }
    QJsonObject historical=d;
    for (const auto key:{"action","restoreHeight","minimumBchConfirmations","minimumXmzConfirmations","walletBackend","refund"}) historical.remove(key);
    historical["schema"]="monzero-bch-native-refund-wallet-status-v1";
    historical["refundHint"]=d.value("refund");historical["chainObserved"]=false;historical["executable"]=false;
    if (!status(historical,config,record,refund,deployment)) return false;
    QByteArray bytes=schema.toUtf8();bytes.append('\0');bytes+=QJsonDocument(d).toJson(QJsonDocument::Compact);
    return value.value("commitment")==QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex());
}

// Only accepts a fresh response to this specific request; this is not permission
// to mutate a wallet. The mutation path must separately revalidate its approval.
inline bool reviewResponse(const QJsonObject &response,const QString &requestId,
    quint64 startedAt,quint64 now,const QJsonObject &config,const QJsonObject &record,
    const QString &refund,const QString &deployment,const QString &action,const QJsonObject &backend) {
    quint64 observed=0;
    if (QUuid(requestId).isNull() || now<startedAt || response.size()!=7
        || response.value("schema")!="monzero-bch-native-refund-wallet-review-response-v1"
        || response.value("requestId")!=requestId || response.value("tradeId")!=config.value("tradeId")
        || response.value("executable")!=QJsonValue(false) || response.value("chainObserved")!=QJsonValue(true)
        || !integer(response.value("observedAt"),9007199254740991ULL,&observed)
        || observed<startedAt || observed>now || now-observed>30 || !response.value("review").isObject()) return false;
    return review(response.value("review").toObject(),config,record,refund,deployment,action,backend);
}

// A saved-ready reply to an explicitly dispatched operation, not fresh chain or
// payout authority. Caller must enforce review expiry before dispatch and keep
// the dedicated service's operation token until this result is verified.
inline bool submissionResponse(const QJsonObject &response,const QString &requestId,
    const QJsonObject &approved,const QJsonObject &config,const QJsonObject &record,
    const QString &refund,const QString &deployment,const QString &action,const QJsonObject &backend) {
    if (QUuid(requestId).isNull() || response.size()!=8
        || !review(approved,config,record,refund,deployment,action,backend)
        || response.value("schema")!="monzero-bch-native-refund-wallet-submit-response-v1"
        || response.value("requestId")!=requestId || response.value("tradeId")!=config.value("tradeId")
        || response.value("action")!=action || response.value("approvedCommitment")!=approved.value("commitment")
        || response.value("chainObserved")!=QJsonValue(false) || response.value("executable")!=QJsonValue(false)
        || !response.value("wallet").isObject()) return false;
    const auto wallet=response.value("wallet").toObject(),details=approved.value("details").toObject();
    if (!status(wallet,config,record,refund,deployment) || wallet.value("state")!="ready") return false;
    for (const auto field:{"swapId","quote","admission","walletFile","address","nativePayment","walletBinding",
        "bchDeployment","bchNetwork","bchGenesis","xmzNetwork"}) {
        if (wallet.value(field)!=details.value(field)) return false;
    }
    if (wallet.value("refundHint")!=details.value("refund")) return false;
    return details.value("walletIntent").isNull() || wallet.value("walletIntent")==details.value("walletIntent");
}

}
#endif
