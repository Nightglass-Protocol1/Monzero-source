#ifndef MONZERO_BCH_NATIVE_OBSERVATION_PROTOCOL_H
#define MONZERO_BCH_NATIVE_OBSERVATION_PROTOCOL_H
#include "BchNativeProofProtocol.h"
namespace BchNativeObservationProtocol {
using namespace BchCancellationProtocol;
// A fresh, point-in-time result only. Callers must discard it on a failed check;
// this public representation is not transaction signing or release authority.
inline bool response(const QJsonObject &value,const QString &requestId,
    const QJsonObject &config,const QJsonObject &record,const QJsonObject &context,
    quint64 started,quint64 now) {
    const auto id=QUuid(requestId);
    if (id.isNull() || id.toString(QUuid::WithoutBraces)!=requestId || started>now
        || value.size()!=7 || value.value("schema")!="monzero-bch-native-observation-response-v1"
        || value.value("requestId")!=requestId || value.value("tradeId")!=config.value("tradeId")
        || !value.value("executable").isBool() || value.value("executable").toBool()
        || !value.value("chainObserved").isBool() || !value.value("chainObserved").toBool()
        || !BchNativeProofProtocol::fundingContext(context)
        || !value.value("review").isObject() || !BchNativeProofProtocol::review(value.value("review").toObject(),config,record,
            context.value("bchFundingOutpoint").toString(),context.value("destination").toString(),
            context.value("admission").toString(),context.value("agreement").toString())
        || !value.value("observation").isObject()) return false;
    const auto observed=value.value("observation").toObject();
    const auto details=value.value("review").toObject().value("details").toObject();
    quint64 height=0,tip=0,depth=0,minimum=0,at=0;
    return observed.size()==10 && observed.value("state")=="confirmed"
        && observed.value("txid")==details.value("txid")
        && observed.value("destination")==details.value("destination")
        && observed.value("amountAtoms")==details.value("amountAtoms")
        && observed.value("genesis")==details.value("genesis") && txid(observed.value("tip"))
        && decimal(observed.value("height"),&height) && height>0
        && decimal(observed.value("tipHeight"),&tip) && tip>=height
        && decimal(observed.value("confirmations"),&depth) && depth==tip-height+1
        && decimal(record.value("terms").toObject().value("xmzConfirmations"),&minimum) && minimum>0 && depth>=minimum
        && decimal(observed.value("observedAt"),&at) && at>=started && at<=now;
}
}
#endif
