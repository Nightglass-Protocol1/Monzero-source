#pragma once
#include "../BchPunishmentProtocol.h"
#include "BchCancellationTestFixture.h"
namespace {
struct PunishContext:Context {
    bool create() {
        if (!Context::create(true)) return false;
        auto t=record.value("terms").toObject();t["bchPayoutScript"]="51";t["punishBlocks"]="100";t["xmzConfirmations"]="100";t["bchRecoveryFeeAtoms"]="2000";t["bchMinimumOutputAtoms"]="546";record["terms"]=t;
        bindQuote();
        auto p=config.value("policy").toObject();p["maximum_bch_atoms"]=1000000;
        p["minimum_covenant_fee"]=500;p["maximum_covenant_fee"]=3000;p["minimum_output_atoms"]=546;config["policy"]=p;
        details.remove("fundingReceipt");details.remove("recoveryScript");details.remove("recoveryAtoms");details.remove("cancelDelayBlocks");
        details["schema"]="monzero-bch-punish-relay-review-v1";details["action"]="broadcast-bch-punishment";
        details["nativeFundingTxid"]=QString(64,'c');details["recoveryOutpoint"]=QString(64,'d')+":0";
        details["destinationScript"]=t.value("bchPayoutScript");details["feeAtoms"]=t.value("bchRecoveryFeeAtoms");
        details["payoutAtoms"]=QString::number(t.value("bchAtoms").toString().toULongLong()-t.value("bchLockFeeAtoms").toString().toULongLong()-t.value("bchRecoveryFeeAtoms").toString().toULongLong());
        details["punishDelayBlocks"]=p.value("punish_blocks");details["minimumXmzConfirmations"]=p.value("xmz_confirmations");
        return true;
    }
    void bindQuote() {
        const auto t=record.value("terms").toObject();
        QByteArray binding="monzero-native-bch-xmz-quote-v1";binding.append('\0');binding+=QJsonDocument(t).toJson(QJsonDocument::Compact);
        const auto digest=QCryptographicHash::hash(binding,QCryptographicHash::Sha256);
        record["commitment"]=QString::fromLatin1(digest.toHex());config["quoteCommitment"]=record.value("commitment");
        QJsonArray pin;for (const auto byte:digest) pin.append(int(static_cast<unsigned char>(byte)));details["quote"]=pin;
    }
    bool valid() const {return BchPunishmentProtocol::review(bound(details),config,record,deployment,now);}
};
}
