#ifndef MONZERO_BCH_PUNISHMENT_PROTOCOL_H
#define MONZERO_BCH_PUNISHMENT_PROTOCOL_H
#include "BchCancellationProtocol.h"
namespace BchPunishmentProtocol {
using namespace BchCancellationProtocol;
inline bool outpoint(const QJsonValue &value,bool recovery) {
    if (!value.isString()) return false;
    const auto parts=value.toString().split(':');quint64 index=0;
    return parts.size()==2 && txid(parts[0]) && decimal(parts[1],&index)
        && index<=4294967295ULL && (!recovery || index==0);
}
inline bool review(const QJsonObject &value,const QJsonObject &config,const QJsonObject &record,
    const QString &deployment,quint64 now) {
    QJsonObject admission;
    if (config.value("role")!="native-funder" || !expectedReview(config,record,&admission)
        || !hex(deployment,64) || value.size()!=2 || !value.value("details").isObject()) return false;
    const auto d=value.value("details").toObject(),t=record.value("terms").toObject(),p=config.value("policy").toObject();
    const QString schema="monzero-bch-punish-relay-review-v1";
    if (d.size()!=20 || d.value("schema")!=schema || d.value("action")!="broadcast-bch-punishment"
        || d.value("swapId")!=config.value("tradeId") || pin(d.value("quote"))!=config.value("quoteCommitment").toString()
        || pin(d.value("admission")).isEmpty() || pin(d.value("nativePayment")).isEmpty()
        || pin(d.value("bchDeployment"))!=deployment || !txid(d.value("nativeFundingTxid")) || !txid(d.value("txid"))
        || d.value("bchNetwork")!=t.value("bchNetwork") || d.value("bchGenesis")!=t.value("bchGenesis")
        || !outpoint(d.value("fundingOutpoint"),false) || !outpoint(d.value("recoveryOutpoint"),true)
        || d.value("destinationScript")!=t.value("bchPayoutScript")) return false;
    const auto script=d.value("destinationScript").toString();
    if (script.isEmpty() || script.size()>10000 || script.size()%2 || !hex(script,script.size())) return false;
    quint64 principal=0,lockFee=0,fee=0,expectedFee=0,payout=0,admitted=0,prepared=0;
    if (!decimal(t.value("bchAtoms"),&principal) || !decimal(t.value("bchLockFeeAtoms"),&lockFee)
        || !decimal(t.value("bchRecoveryFeeAtoms"),&expectedFee) || !decimal(d.value("feeAtoms"),&fee)
        || !fee || fee!=expectedFee || principal<=lockFee || principal-lockFee<=fee
        || !decimal(d.value("payoutAtoms"),&payout) || payout!=principal-lockFee-fee
        || !integer(config.value("admittedAt"),9007199254740991ULL,&admitted)
        || !integer(d.value("preparedAt"),9007199254740991ULL,&prepared) || prepared<admitted || prepared>now) return false;
    quint64 maximum=0,minimumFee=0,maximumFee=0,minimumOutput=0,agreedOutput=0;
    if (!integer(p.value("maximum_bch_atoms"),9007199254740991ULL,&maximum)
        || principal>maximum || principal>2100000000000000ULL
        || !integer(p.value("minimum_covenant_fee"),9007199254740991ULL,&minimumFee) || !minimumFee
        || !integer(p.value("maximum_covenant_fee"),9007199254740991ULL,&maximumFee)
        || lockFee<minimumFee || lockFee>maximumFee || fee<minimumFee || fee>maximumFee
        || !integer(p.value("minimum_output_atoms"),9007199254740991ULL,&minimumOutput) || !minimumOutput
        || !decimal(t.value("bchMinimumOutputAtoms"),&agreedOutput)
        || agreedOutput!=minimumOutput || payout<minimumOutput) return false;
    for (const auto &fields:QList<QStringList>{{"punishDelayBlocks","punishBlocks","punish_blocks"},
        {"minimumBchConfirmations","bchConfirmations","bch_confirmations"},
        {"minimumXmzConfirmations","xmzConfirmations","xmz_confirmations"}}) {
        quint64 actual=0,expected=0;
        if (!integer(d.value(fields[0]),fields[0]=="punishDelayBlocks"?65535:1000,&actual) || !actual || !decimal(t.value(fields[1]),&expected)
            || actual!=expected || d.value(fields[0])!=p.value(fields[2])) return false;
    }
    QByteArray bytes=schema.toUtf8();bytes.append('\0');bytes+=QJsonDocument(d).toJson(QJsonDocument::Compact);
    return value.value("commitment")==QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex());
}
}
#endif
