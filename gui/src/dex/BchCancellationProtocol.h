#ifndef MONZERO_BCH_CANCELLATION_PROTOCOL_H
#define MONZERO_BCH_CANCELLATION_PROTOCOL_H
#include "BchAdmissionProtocol.h"
namespace BchCancellationProtocol {
using namespace BchAdmissionProtocol;
inline bool decimal(const QJsonValue &value,quint64 *result) {
    if (!value.isString()) return false;
    bool ok=false;const auto n=value.toString().toULongLong(&ok);
    if (!ok || QString::number(n)!=value.toString()) return false;
    *result=n;return true;
}
inline bool txid(const QJsonValue &value) {
    return value.isString() && hex(value.toString(),64) && value.toString()!=QString(64,'0');
}
inline bool review(const QJsonObject &value,const QJsonObject &config,const QJsonObject &record,
    const QString &deployment,quint64 now) {
    QJsonObject admission;
    if (!expectedReview(config,record,&admission) || !hex(deployment,64)
        || value.size()!=2 || !value.value("details").isObject()) return false;
    const auto details=value.value("details").toObject(),terms=record.value("terms").toObject(),policy=config.value("policy").toObject();
    const bool native=config.value("role")=="native-funder";
    const QString schema=native?"monzero-bch-native-cancel-relay-review-v1":"monzero-bch-cancel-relay-review-v1";
    if (details.size()!=(native?18:17) || details.value("schema")!=schema
        || details.value("action")!="broadcast-bch-cancellation" || details.value("swapId")!=config.value("tradeId")
        || pin(details.value("quote"))!=config.value("quoteCommitment").toString()
        || pin(details.value("bchDeployment"))!=deployment || pin(details.value("admission")).isEmpty()
        || details.value("bchNetwork")!=terms.value("bchNetwork") || details.value("bchGenesis")!=terms.value("bchGenesis")) return false;
    if (native) {
        if (pin(details.value("fundingReceipt")).isEmpty() || pin(details.value("nativePayment")).isEmpty()) return false;
    } else if (pin(details.value("privateRecovery")).isEmpty()) return false;
    const auto outpoint=details.value("fundingOutpoint").toString().split(':');
    quint64 outputIndex=0;
    quint64 principal=0,fee=0,amount=0,expectedFee=0,admitted=0,prepared=0,delay=0,confirmations=0,termDelay=0,termConfirmations=0;
    if (outpoint.size()!=2 || !txid(outpoint.value(0))
        || !decimal(outpoint.value(1),&outputIndex) || outputIndex>4294967295ULL
        || !txid(details.value("txid")) || !decimal(terms.value("bchAtoms"),&principal)
        || !decimal(terms.value("bchLockFeeAtoms"),&expectedFee) || !decimal(details.value("feeAtoms"),&fee)
        || !fee || fee!=expectedFee || principal<=fee || !decimal(details.value("recoveryAtoms"),&amount) || amount!=principal-fee
        || !integer(config.value("admittedAt"),9007199254740991ULL,&admitted)
        || !integer(details.value("preparedAt"),9007199254740991ULL,&prepared) || prepared<admitted || prepared>now
        || !integer(details.value("cancelDelayBlocks"),65535,&delay) || !delay
        || !integer(details.value("minimumBchConfirmations"),1000,&confirmations) || !confirmations
        || !decimal(terms.value("cancelBlocks"),&termDelay) || delay!=termDelay
        || !decimal(terms.value("bchConfirmations"),&termConfirmations) || confirmations!=termConfirmations
        || details.value("cancelDelayBlocks")!=policy.value("cancel_blocks")
        || details.value("minimumBchConfirmations")!=policy.value("bch_confirmations")) return false;
    const auto script=details.value("recoveryScript").toString();
    if (script.isEmpty() || script.size()>10000 || script.size()%2 || !hex(script,script.size())) return false;
    QByteArray binding=schema.toUtf8();binding.append('\0');binding+=QJsonDocument(details).toJson(QJsonDocument::Compact);
    return value.value("commitment")==QString::fromLatin1(QCryptographicHash::hash(binding,QCryptographicHash::Sha256).toHex());
}
inline bool relay(const QJsonObject &value,const QJsonObject &review) {
    return value.size()==3 && QStringList{"not-attempted","attempted","acknowledged"}.contains(value.value("state").toString())
        && value.value("txid")==review.value("details").toObject().value("txid")
        && value.value("confirmed").isBool() && !value.value("confirmed").toBool();
}
inline bool observation(const QJsonObject &value,const QJsonObject &review,quint64 started,quint64 now) {
    quint64 height=0,confirmations=0,observed=0,minimum=0;
    const auto details=review.value("details").toObject();
    return value.size()==7 && value.value("state")=="confirmed" && value.value("txid")==details.value("txid")
        && txid(value.value("block")) && txid(value.value("tip")) && decimal(value.value("height"),&height) && height>0
        && integer(details.value("minimumBchConfirmations"),1000,&minimum) && minimum>0
        && decimal(value.value("confirmations"),&confirmations) && confirmations>=minimum
        && decimal(value.value("observedAt"),&observed) && observed>=started && observed<=now;
}
}
#endif
