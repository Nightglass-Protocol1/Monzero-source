#ifndef MONZERO_BCH_NATIVE_RELAY_PROTOCOL_H
#define MONZERO_BCH_NATIVE_RELAY_PROTOCOL_H
#include "BchNativePreparationProtocol.h"
namespace BchNativeRelayProtocol {
using namespace BchNativePreparationProtocol;
// Bind a separate broadcast approval to the already validated, saved preparation.
// Neither this public review nor an acknowledgement establishes confirmation.
inline bool review(const QJsonObject &value,const QJsonObject &config,const QJsonObject &record,
    const QJsonObject &source,const QJsonObject &preparedReview,const QJsonObject &preparation,quint64 now) {
    if (!BchNativePreparationProtocol::review(preparedReview,config,record,source)
        || !saved(preparation,preparedReview,config,now) || preparation.value("state")!="prepared"
        || value.size()!=2 || !value.value("details").isObject()) return false;
    const auto d=value.value("details").toObject(),p=preparedReview.value("details").toObject();
    const auto payment=preparation.value("payment").toObject(),t=record.value("terms").toObject();
    const QString schema="monzero-bch-native-relay-review-v1";
    if (d.size()!=23 || d.value("schema")!=schema || d.value("action")!="broadcast-xmz-funding"
        || d.value("fundingReceipt")!=p.value("receipt") || d.value("bchDeployment")!=p.value("deployment")
        || pin(d.value("preparation")).isEmpty() || pin(d.value("preparedPayment")).isEmpty()
        || d.value("network")!=t.value("xmzNetwork") || d.value("expiresAt")!=t.value("expiresAt")
        || d.value("cancellationMarginBlocks")!=p.value("safetyBlocks")) return false;
    for (const auto field:{"swapId","quote","admission","genesis","wallet","account","priority",
        "destination","amountAtoms","maximumFeeAtoms"})
        if (d.value(field)!=p.value(field)) return false;
    for (const auto field:{"txid","feeAtoms","weight"})
        if (d.value(field)!=payment.value(field)) return false;
    quint64 confirmations=0,expected=0;
    if (!integer(d.value("minimumBchConfirmations"),4294967295ULL,&confirmations)
        || !decimal(t.value("bchConfirmations"),&expected) || confirmations!=expected) return false;
    QByteArray binding=schema.toUtf8();binding.append('\0');binding+=QJsonDocument(d).toJson(QJsonDocument::Compact);
    return value.value("commitment")==QString::fromLatin1(QCryptographicHash::hash(binding,QCryptographicHash::Sha256).toHex());
}
// Call only after validating the associated review. Acknowledged means the
// wallet accepted the exact payment; observation is a separate operation.
inline bool relayState(const QJsonObject &value,const QJsonObject &checkedReview) {
    const auto state=value.value("state").toString();
    return value.size()==2 && (state=="attempted" || state=="acknowledged")
        && txid(value.value("txid"))
        && value.value("txid")==checkedReview.value("details").toObject().value("txid");
}
}
#endif
