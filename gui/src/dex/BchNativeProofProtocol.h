#ifndef MONZERO_BCH_NATIVE_PROOF_PROTOCOL_H
#define MONZERO_BCH_NATIVE_PROOF_PROTOCOL_H
#include "BchNativePreparationProtocol.h"
namespace BchNativeProofProtocol {
using namespace BchCancellationProtocol;
inline bool fundingContext(const QJsonObject &value) {
    const auto outpoint=value.value("bchFundingOutpoint").toString().split(':');quint64 index=0;
    return value.size()==4 && hex(value.value("admission").toString(),64)
        && hex(value.value("agreement").toString(),64)
        && BchNativePreparationProtocol::address(value.value("destination"))
        && outpoint.size()==2 && txid(outpoint.value(0))
        && decimal(outpoint.value(1),&index) && index<=4294967295ULL;
}
inline bool contextResponse(const QJsonObject &value,const QString &requestId,
    const QJsonObject &config,const QJsonObject &record) {
    QJsonObject admission;const auto id=QUuid(requestId);
    return !id.isNull() && id.toString(QUuid::WithoutBraces)==requestId
        && config.value("role")=="bch-funder" && expectedReview(config,record,&admission)
        && value.size()==6 && value.value("schema")=="monzero-bch-native-proof-context-response-v1"
        && value.value("requestId")==requestId && value.value("tradeId")==config.value("tradeId")
        && value.value("executable").isBool() && !value.value("executable").toBool()
        && value.value("chainObserved").isBool() && !value.value("chainObserved").toBool()
        && value.value("context").isObject() && fundingContext(value.value("context").toObject());
}
// The caller supplies the funding context retained by the pinned helper.
// The agreement digest is the negotiated commitment, not its checkpoint hash.
// A valid public review does not establish chain confirmation or release authority.
inline bool review(const QJsonObject &value,const QJsonObject &config,const QJsonObject &record,
    const QString &fundingOutpoint,const QString &destination,const QString &admissionDigest,const QString &agreementDigest) {
    QJsonObject admission;
    if (config.value("role")!="bch-funder" || !expectedReview(config,record,&admission)
        || value.size()!=2 || !value.value("details").isObject() || !hex(admissionDigest,64)
        || !hex(agreementDigest,64) || !BchNativePreparationProtocol::address(destination)) return false;
    const auto outpoint=fundingOutpoint.split(':');quint64 index=0;
    if (outpoint.size()!=2 || !txid(outpoint.value(0)) || !decimal(outpoint.value(1),&index)
        || index>4294967295ULL) return false;
    const auto d=value.value("details").toObject(),t=record.value("terms").toObject();
    const QString schema="monzero-bch-native-proof-review-v1";
    quint64 amount=0,expected=0;
    if (d.size()!=13 || d.value("schema")!=schema || d.value("action")!="retain-native-payment-proof"
        || d.value("swapId")!=config.value("tradeId") || pin(d.value("quote"))!=config.value("quoteCommitment").toString()
        || pin(d.value("agreement"))!=agreementDigest
        || pin(d.value("admission"))!=admissionDigest || d.value("bchFundingOutpoint")!=fundingOutpoint
        || d.value("genesis")!=t.value("xmzGenesis") || d.value("destination")!=destination
        || !decimal(d.value("amountAtoms"),&amount) || !amount || !decimal(t.value("xmzAtoms"),&expected) || amount!=expected
        || !txid(d.value("txid")) || pin(d.value("message")).isEmpty() || pin(d.value("signature")).isEmpty()) return false;
    QByteArray binding=schema.toUtf8();binding.append('\0');binding+=QJsonDocument(d).toJson(QJsonDocument::Compact);
    return value.value("commitment")==QString::fromLatin1(QCryptographicHash::hash(binding,QCryptographicHash::Sha256).toHex());
}
// Historical receipt does not renew the quote or establish chain confirmation.
inline bool receipt(const QJsonObject &value,const QJsonObject &config,quint64 now) {
    quint64 received=0,admitted=0;
    return value.size()==2 && value.value("state")=="retained"
        && decimal(value.value("receivedAt"),&received)
        && integer(config.value("admittedAt"),9007199254740991ULL,&admitted)
        && received>=admitted && received<=now;
}
// Validate the envelope before publishing any helper response to the UI.
inline bool response(const QJsonObject &value,const QString &operation,const QString &requestId,
    const QJsonObject &config,const QJsonObject &record,const QString &fundingOutpoint,
    const QString &destination,const QString &admissionDigest,const QString &agreementDigest,quint64 now) {
    const auto id=QUuid(requestId);
    if (!QStringList{"review","submit","status"}.contains(operation) || id.isNull()
        || id.toString(QUuid::WithoutBraces)!=requestId || value.size()!=7
        || value.value("schema")!="monzero-bch-native-proof-"+operation+"-response-v1"
        || value.value("requestId")!=requestId || value.value("tradeId")!=config.value("tradeId")
        || !value.value("executable").isBool() || value.value("executable").toBool()
        || !value.value("chainObserved").isBool() || value.value("chainObserved").toBool()) return false;
    if (value.value("review").isNull()) {
        QJsonObject admission;
        return operation=="status" && value.value("receipt").isNull()
            && config.value("role")=="bch-funder" && expectedReview(config,record,&admission);
    }
    if (!value.value("review").isObject() || !review(value.value("review").toObject(),config,record,
        fundingOutpoint,destination,admissionDigest,agreementDigest)) return false;
    if (value.value("receipt").isNull()) return operation=="review";
    return value.value("receipt").isObject() && receipt(value.value("receipt").toObject(),config,now);
}
}
#endif
