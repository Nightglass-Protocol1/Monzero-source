#ifndef MONZERO_BCH_REFUND_RELEASE_PROTOCOL_H
#define MONZERO_BCH_REFUND_RELEASE_PROTOCOL_H
#include "BchCancellationProtocol.h"
namespace BchRefundReleaseProtocol {
using namespace BchCancellationProtocol;
inline bool outpoint(const QJsonValue &value,bool recovery=false) {
    if (!value.isString()) return false;
    const auto parts=value.toString().split(':');quint64 index=0;
    return parts.size()==2 && txid(parts.value(0)) && decimal(parts.value(1),&index)
        && index<=4294967295ULL && (!recovery || index==0);
}
inline bool review(const QJsonObject &value,const QJsonObject &config,const QJsonObject &record,
    const QString &deployment,quint32 margin,quint64 now) {
    QJsonObject admission;
    if (config.value("role")!="bch-funder" || !expectedReview(config,record,&admission)
        || !hex(deployment,64) || value.size()!=2 || !value.value("details").isObject()) return false;
    const auto d=value.value("details").toObject(),t=record.value("terms").toObject(),p=config.value("policy").toObject();
    if (d.size()!=20 || d.value("schema")!="monzero-bch-refund-relay-review-v1"
        || d.value("action")!="broadcast-bch-refund" || d.value("swapId")!=config.value("tradeId")
        || pin(d.value("quote"))!=config.value("quoteCommitment").toString()
        || pin(d.value("bchDeployment"))!=deployment || pin(d.value("admission")).isEmpty()
        || pin(d.value("privateRecovery")).isEmpty() || d.value("bchNetwork")!=t.value("bchNetwork")
        || d.value("bchGenesis")!=t.value("bchGenesis") || !outpoint(d.value("fundingOutpoint"))
        || !outpoint(d.value("recoveryOutpoint"),true) || !txid(d.value("txid"))
        || !d.value("revealsNativeSpendShare").isBool() || !d.value("revealsNativeSpendShare").toBool()) return false;
    quint64 principal=0,lockFee=0,fee=0,expectedFee=0,amount=0,admitted=0,prepared=0,delay=0,minimum=0,quotedDelay=0,quotedMinimum=0,safety=0;
    if (!decimal(t.value("bchAtoms"),&principal) || !decimal(t.value("bchLockFeeAtoms"),&lockFee)
        || !decimal(t.value("bchRecoveryFeeAtoms"),&expectedFee) || !decimal(d.value("feeAtoms"),&fee)
        || !fee || fee!=expectedFee || principal<=lockFee || principal-lockFee<=fee
        || !decimal(d.value("refundAtoms"),&amount) || amount!=principal-lockFee-fee
        || !integer(config.value("admittedAt"),9007199254740991ULL,&admitted)
        || !integer(d.value("preparedAt"),9007199254740991ULL,&prepared) || prepared<admitted || prepared>now
        || !integer(d.value("punishDelayBlocks"),65535,&delay) || !delay
        || !integer(d.value("minimumBchConfirmations"),1000,&minimum) || !minimum
        || !integer(d.value("punishmentMarginBlocks"),4294967295ULL,&safety) || !safety || safety!=margin
        || minimum+safety>=delay || !decimal(t.value("punishBlocks"),&quotedDelay) || delay!=quotedDelay
        || !decimal(t.value("bchConfirmations"),&quotedMinimum) || minimum!=quotedMinimum
        || d.value("punishDelayBlocks")!=p.value("punish_blocks")
        || d.value("minimumBchConfirmations")!=p.value("bch_confirmations")) return false;
    const auto script=d.value("destinationScript").toString();
    if (script.isEmpty() || script.size()>10000 || script.size()%2 || !hex(script,script.size())
        || d.value("destinationScript")!=t.value("bchRefundScript")) return false;
    QByteArray binding="monzero-bch-refund-relay-review-v1";binding.append('\0');binding+=QJsonDocument(d).toJson(QJsonDocument::Compact);
    return value.value("commitment")==QString::fromLatin1(QCryptographicHash::hash(binding,QCryptographicHash::Sha256).toHex());
}
}
#endif
