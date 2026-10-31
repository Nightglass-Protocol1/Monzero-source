#ifndef MONZERO_BCH_PUBLICATION_REPAIR_PROTOCOL_H
#define MONZERO_BCH_PUBLICATION_REPAIR_PROTOCOL_H
#include "BchCancellationProtocol.h"
namespace BchPublicationRepairProtocol {
using namespace BchCancellationProtocol;
inline bool stage(const QString &value) {return value=="prepared" || value=="signed";}
// The pinned helper validates private admission, receipt and candidate bytes.
// Qt only accepts their public commitments, never a frame or secret record.
inline bool review(const QJsonObject &value,const QJsonObject &config,const QJsonObject &record,
    const QJsonObject &source,const QString &expectedStage,quint64 now) {
    QJsonObject admission;
    const auto fundingSource=source.value("fundingSource").toString();
    if (!stage(expectedStage) || config.value("role")!="bch-funder"
        || !expectedReview(config,record,&admission)
        || !QStringList{"node-wallet","local-wallet"}.contains(fundingSource)
        || !hex(source.value("deploymentCommitment").toString(),64)
        || value.size()!=2 || !value.value("details").isObject()) return false;
    const auto d=value.value("details").toObject();
    const QString schema="monzero-bch-success-publication-repair-v1";
    if (d.size()!=13 || d.value("schema")!=schema || d.value("action")!="repair-private-success-publication"
        || d.value("swapId")!=config.value("tradeId")
        || pin(d.value("quote"))!=config.value("quoteCommitment").toString()
        || pin(d.value("bchDeployment"))!=source.value("deploymentCommitment").toString()
        || d.value("fundingSource")!=fundingSource || d.value("stage")!=expectedStage
        || pin(d.value("admission")).isEmpty() || pin(d.value("nativeReceipt")).isEmpty()
        || pin(d.value("candidate")).isEmpty()
        || !d.value("releasesPrivateMaterial").isBool() || d.value("releasesPrivateMaterial").toBool()
        || !d.value("chainApproval").isBool() || d.value("chainApproval").toBool()) return false;
    quint64 recorded=0,admitted=0;
    if (!integer(d.value("recordedAt"),9007199254740991ULL,&recorded)
        || !integer(config.value("admittedAt"),9007199254740991ULL,&admitted)
        || !recorded || recorded<admitted || recorded>now) return false;
    QByteArray binding=schema.toUtf8();binding.append('\0');binding+=QJsonDocument(d).toJson(QJsonDocument::Compact);
    return value.value("commitment")==QString::fromLatin1(QCryptographicHash::hash(binding,QCryptographicHash::Sha256).toHex());
}
inline bool response(const QJsonObject &value,const QString &requestId,const QJsonObject &config,
    const QJsonObject &record,const QJsonObject &source,const QString &expectedStage,quint64 now,
    bool submit,const QString &approved={}) {
    const auto id=QUuid(requestId);const QString action=submit?"submit":"review";
    if (id.isNull() || id.toString(QUuid::WithoutBraces)!=requestId
        || (submit?!hex(approved,64):!approved.isEmpty()) || value.size()!=8
        || value.value("schema")!=QString("monzero-bch-success-publication-repair-%1-response-v1").arg(action)
        || value.value("requestId")!=requestId || value.value("tradeId")!=config.value("tradeId")
        || value.value("state")!=(submit?"repaired":"reviewed")
        || !value.value("executable").isBool() || value.value("executable").toBool()
        || !value.value("chainObserved").isBool() || value.value("chainObserved").toBool()
        || !value.value("releasesPrivateMaterial").isBool() || value.value("releasesPrivateMaterial").toBool()
        || !value.value("review").isObject()
        || !review(value.value("review").toObject(),config,record,source,expectedStage,now)) return false;
    return !submit || value.value("review").toObject().value("commitment")==approved;
}
}
#endif
