#pragma once
#include "BchCancellationTestFixture.h"
#include "../BchRefundReleaseProtocol.h"
namespace {
struct RefundContext:Context {
    bool create() {
        if (!Context::create()) return false;auto terms=record.value("terms").toObject();
        terms["punishBlocks"]="30";terms["bchRecoveryFeeAtoms"]="2000";terms["bchRefundScript"]="51";
        record["terms"]=terms;QByteArray binding="monzero-native-bch-xmz-quote-v1";binding.append('\0');
        binding+=QJsonDocument(terms).toJson(QJsonDocument::Compact);
        const auto hash=QCryptographicHash::hash(binding,QCryptographicHash::Sha256);
        record["commitment"]=QString::fromLatin1(hash.toHex());config["quoteCommitment"]=record.value("commitment");
        auto policy=config.value("policy").toObject();policy["punish_blocks"]=30;config["policy"]=policy;
        QJsonArray quote;for (auto b:hash) quote.append(int(static_cast<unsigned char>(b)));
        auto &d=details;d["quote"]=quote;d["schema"]="monzero-bch-refund-relay-review-v1";d["action"]="broadcast-bch-refund";
        d.remove("recoveryScript");d.remove("recoveryAtoms");d.remove("cancelDelayBlocks");
        d["recoveryOutpoint"]=QString(64,'c')+":0";d["destinationScript"]="51";d["refundAtoms"]="97000";d["feeAtoms"]="2000";
        d["punishDelayBlocks"]=30;d["punishmentMarginBlocks"]=5;d["revealsNativeSpendShare"]=true;
        return true;
    }
};
}
