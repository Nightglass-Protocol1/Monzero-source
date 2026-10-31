#ifndef MONZERO_BCH_NATIVE_PREPARATION_PROTOCOL_H
#define MONZERO_BCH_NATIVE_PREPARATION_PROTOCOL_H
#include "BchCancellationProtocol.h"
namespace BchNativePreparationProtocol {
using namespace BchCancellationProtocol;
inline bool configurationVersion(const QJsonObject &source) {
    QStringList keys{"schema","admissionConfigFile","bchRpcFile","nativeRpcFile",
        "deploymentCommitment","wallet","account","priority","safetyBlocks"};
    if (source.value("schema")=="monzero-private-bch-native-preparation-v2") {
        if (source.value("bchObserverKind")!="owned-node") return false;
        keys.append("bchObserverKind");
    } else if (source.value("schema")!="monzero-private-bch-native-preparation-v1") return false;
    keys.sort();return source.keys()==keys;
}

inline bool address(const QJsonValue &v) {
    if (!v.isString()) return false;
    const auto s=v.toString();
    if (s.size()!=95 && s.size()!=106) return false;
    const QString alphabet="123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";
    for (const auto c:s) if (!alphabet.contains(c)) return false;
    return true;
}
inline bool review(const QJsonObject &value,const QJsonObject &config,const QJsonObject &record,
    const QJsonObject &source) {
    QJsonObject admission;
    if (config.value("role")!="native-funder" || !expectedReview(config,record,&admission)
        || value.size()!=2 || !value.value("details").isObject()) return false;
    const auto d=value.value("details").toObject(),t=record.value("terms").toObject();
    const QString schema="monzero-bch-native-preparation-review-v1";
    if (d.size()!=15 || d.value("schema")!=schema || d.value("action")!="prepare-bch-native-funding"
        || d.value("swapId")!=config.value("tradeId") || pin(d.value("quote"))!=config.value("quoteCommitment").toString()
        || pin(d.value("admission")).isEmpty() || pin(d.value("receipt")).isEmpty()
        || !hex(source.value("deploymentCommitment").toString(),64)
        || pin(d.value("deployment"))!=source.value("deploymentCommitment").toString()
        || d.value("genesis")!=t.value("xmzGenesis") || !address(d.value("wallet"))
        || d.value("wallet")!=source.value("wallet") || !address(d.value("destination"))) return false;
    quint64 amount=0,expected=0,fee=0,maximum=0,account=0,priority=0,margin=0,confirmations=0,delay=0;
    if (!decimal(d.value("amountAtoms"),&amount) || !amount || !decimal(t.value("xmzAtoms"),&expected) || amount!=expected
        || !decimal(d.value("maximumFeeAtoms"),&fee) || !fee || !decimal(t.value("xmzFundingFeeMaxAtoms"),&maximum) || fee!=maximum
        || !integer(d.value("account"),4294967295ULL,&account) || d.value("account")!=source.value("account")
        || !integer(d.value("priority"),4,&priority) || !priority || d.value("priority")!=source.value("priority")
        || !integer(d.value("safetyBlocks"),4294967295ULL,&margin) || !margin || d.value("safetyBlocks")!=source.value("safetyBlocks")
        || !decimal(t.value("bchConfirmations"),&confirmations) || !decimal(t.value("cancelBlocks"),&delay)
        || confirmations>=delay || margin>=delay-confirmations) return false;
    QByteArray binding=schema.toUtf8();binding.append('\0');binding+=QJsonDocument(d).toJson(QJsonDocument::Compact);
    return value.value("commitment")==QString::fromLatin1(QCryptographicHash::hash(binding,QCryptographicHash::Sha256).toHex());
}
// A saved record is historical evidence, never a request to retry construction.
inline bool saved(const QJsonObject &value,const QJsonObject &checkedReview,const QJsonObject &config,quint64 now) {
    quint64 at=0,admitted=0;
    if (value.size()!=3 || !decimal(value.value("attemptedAt"),&at)
        || !integer(config.value("admittedAt"),9007199254740991ULL,&admitted) || at<admitted || at>now) return false;
    if (value.value("state")=="attempted") return value.value("payment").isNull();
    if (value.value("state")!="prepared" || !value.value("payment").isObject()) return false;
    const auto payment=value.value("payment").toObject();quint64 fee=0,cap=0,weight=0;
    return payment.size()==3 && txid(payment.value("txid")) && decimal(payment.value("feeAtoms"),&fee) && fee
        && decimal(checkedReview.value("details").toObject().value("maximumFeeAtoms"),&cap) && fee<=cap
        && integer(payment.value("weight"),9007199254740991ULL,&weight) && weight>0;
}
}
#endif
