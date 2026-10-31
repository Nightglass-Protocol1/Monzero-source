#ifndef MONZERO_BCH_SUCCESS_RELEASE_PROTOCOL_H
#define MONZERO_BCH_SUCCESS_RELEASE_PROTOCOL_H
#include "BchNativeProofProtocol.h"
namespace BchSuccessReleaseProtocol {
using namespace BchCancellationProtocol;
enum class Action { Prepare, RetainSignature, ReleaseSigned };
inline QString actionName(Action action) {
    switch (action) {
    case Action::Prepare: return "prepare-bch-success-release";
    case Action::RetainSignature: return "retain-bch-success-signature";
    case Action::ReleaseSigned: return "release-signed-bch-success";
    }
    return {};
}
inline QString responseSchema(Action action) {
    switch (action) {
    case Action::Prepare: return "monzero-bch-success-release-review-response-v1";
    case Action::RetainSignature: return "monzero-bch-success-signature-review-response-v1";
    case Action::ReleaseSigned: return "monzero-bch-success-signed-release-review-response-v1";
    }
    return {};
}
// The proof worker supplies its validated saved review and restored context.
// The private receipt digest is opaque here: it is not the public proof-review
// commitment. The pinned controller checks that digest against its journal.
inline bool review(const QJsonObject &value,const QJsonObject &config,const QJsonObject &record,
    const QJsonObject &source,const QJsonObject &proofReview,const QJsonObject &context,
    Action action=Action::Prepare) {
    const auto expectedAction=actionName(action);
    if (expectedAction.isEmpty() || !BchNativeProofProtocol::fundingContext(context)
        || !BchNativeProofProtocol::review(proofReview,config,record,
            context.value("bchFundingOutpoint").toString(),context.value("destination").toString(),
            context.value("admission").toString(),context.value("agreement").toString())
        || value.size()!=2 || !value.value("details").isObject()
        || !hex(source.value("deploymentCommitment").toString(),64)) return false;
    const auto d=value.value("details").toObject(),t=record.value("terms").toObject();
    const auto proof=proofReview.value("details").toObject();
    const QString schema="monzero-bch-success-release-review-v1";
    if (d.size()!=19 || d.value("schema")!=schema || d.value("action")!=expectedAction
        || d.value("swapId")!=config.value("tradeId") || d.value("quote")!=proof.value("quote")
        || d.value("agreement")!=proof.value("agreement") || d.value("admission")!=proof.value("admission")
        || pin(d.value("nativeProof")).isEmpty()
        || pin(d.value("bchDeployment"))!=source.value("deploymentCommitment").toString()
        || d.value("bchFundingOutpoint")!=proof.value("bchFundingOutpoint")
        || d.value("bchPayoutScript")!=t.value("bchPayoutScript")
        || d.value("nativeTxid")!=proof.value("txid")
        || d.value("nativeDestination")!=proof.value("destination")
        || d.value("nativeAmountAtoms")!=proof.value("amountAtoms")) return false;
    quint64 principal=0,fee=0,payout=0,bchDepth=0,nativeDepth=0,margin=0,delay=0,expected=0;
    if (!decimal(d.value("bchAtoms"),&principal) || !decimal(t.value("bchAtoms"),&expected) || principal!=expected
        || !decimal(d.value("bchFeeAtoms"),&fee) || !fee || !decimal(t.value("bchLockFeeAtoms"),&expected) || fee!=expected
        || principal<=fee || !decimal(d.value("bchPayoutAtoms"),&payout) || payout!=principal-fee
        || !integer(d.value("minimumBchConfirmations"),4294967295ULL,&bchDepth) || !bchDepth
        || !decimal(t.value("bchConfirmations"),&expected) || bchDepth!=expected
        || !integer(d.value("minimumNativeConfirmations"),4294967295ULL,&nativeDepth) || !nativeDepth
        || !decimal(t.value("xmzConfirmations"),&expected) || nativeDepth!=expected
        || !integer(d.value("cancellationMarginBlocks"),4294967295ULL,&margin) || !margin
        || d.value("cancellationMarginBlocks")!=source.value("safetyBlocks")
        || !decimal(t.value("cancelBlocks"),&delay) || bchDepth>=delay || margin>=delay-bchDepth) return false;
    QByteArray binding=schema.toUtf8();binding.append('\0');binding+=QJsonDocument(d).toJson(QJsonDocument::Compact);
    return value.value("commitment")==QString::fromLatin1(QCryptographicHash::hash(binding,QCryptographicHash::Sha256).toHex());
}
inline bool response(const QJsonObject &value,const QString &requestId,const QJsonObject &config,
    const QJsonObject &record,const QJsonObject &source,const QJsonObject &proofReview,const QJsonObject &context,
    Action action=Action::Prepare) {
    const auto id=QUuid(requestId);const auto schema=responseSchema(action);
    return !schema.isEmpty() && !id.isNull() && id.toString(QUuid::WithoutBraces)==requestId && value.size()==6
        && value.value("schema")==schema
        && value.value("requestId")==requestId && value.value("tradeId")==config.value("tradeId")
        && value.value("executable").isBool() && !value.value("executable").toBool()
        && value.value("chainObserved").isBool() && !value.value("chainObserved").toBool()
        && value.value("review").isObject() && review(value.value("review").toObject(),config,record,source,proofReview,context,action);
}
// Historical preparation state can be restored without RPC credentials. It is
// never fresh chain evidence, including when an identity signature is retained.
inline bool preparationStatusResponse(const QJsonObject &value,const QString &requestId,
    const QJsonObject &config,const QJsonObject &record,const QJsonObject &source,
    const QJsonObject &proofReview,const QJsonObject &context,quint64 now) {
    const auto id=QUuid(requestId);
    if (id.isNull() || id.toString(QUuid::WithoutBraces)!=requestId || value.size()!=7
        || value.value("schema")!="monzero-bch-success-preparation-status-response-v1"
        || value.value("requestId")!=requestId || value.value("tradeId")!=config.value("tradeId")
        || !value.value("executable").isBool() || value.value("executable").toBool()
        || !value.value("chainObserved").isBool() || value.value("chainObserved").toBool()
        || !value.value("review").isObject()
        || !review(value.value("review").toObject(),config,record,source,proofReview,context)) return false;
    if (value.value("preparation").isNull()) return true;
    if (!value.value("preparation").isObject()) return false;
    const auto saved=value.value("preparation").toObject();quint64 at=0,admitted=0;
    return saved.size()==3 && saved.value("state")=="prepared" && saved.value("signatureRetained").isBool()
        && decimal(saved.value("preparedAt"),&at) && at>0 && at<=now
        && integer(config.value("admittedAt"),9007199254740991ULL,&admitted) && at>=admitted;
}
// Preparation acknowledges private journal persistence only. Its chain check
// does not authorize a later signer/release operation or carry adaptor bytes.
inline bool preparationResponse(const QJsonObject &value,const QString &requestId,const QString &approved,
    const QJsonObject &config,const QJsonObject &record,const QJsonObject &source,
    const QJsonObject &proofReview,const QJsonObject &context) {
    const auto id=QUuid(requestId);
    if (id.isNull() || id.toString(QUuid::WithoutBraces)!=requestId || !hex(approved,64)
        || value.size()!=7 || value.value("schema")!="monzero-bch-success-preparation-response-v1"
        || value.value("requestId")!=requestId || value.value("tradeId")!=config.value("tradeId")
        || !value.value("executable").isBool() || value.value("executable").toBool()
        || !value.value("chainObserved").isBool() || !value.value("chainObserved").toBool()
        || value.value("state")!="prepared" || !value.value("review").isObject()) return false;
    const auto checked=value.value("review").toObject();
    return checked.value("commitment")==approved && review(checked,config,record,source,proofReview,context);
}
}
#endif
