#ifndef MONZERO_BCH_FUNDING_RECEIPT_PROTOCOL_H
#define MONZERO_BCH_FUNDING_RECEIPT_PROTOCOL_H
#include "BchCancellationProtocol.h"
namespace BchFundingReceiptProtocol {
using namespace BchCancellationProtocol;
// Shared public-field validation; entry points below distinguish consent from history.
namespace detail {
inline bool validate(const QJsonObject &value,const QJsonObject &config,const QJsonObject &record,
    const QString &message,const QString &transaction) {
    QJsonObject admission;
    if (config.value("role")!="native-funder" || !expectedReview(config,record,&admission)
        || value.size()!=2 || !value.value("details").isObject()
        || message.isEmpty()!=transaction.isEmpty()
        || (!message.isEmpty() && (!hex(message,64) || !txid(transaction)))) return false;
    const auto d=value.value("details").toObject(),t=record.value("terms").toObject();
    const QString schema="monzero-bch-funding-receipt-review-v1";
    if (d.size()!=12 || d.value("schema")!=schema || d.value("action")!="accept-bch-funding-message"
        || d.value("swapId")!=config.value("tradeId") || pin(d.value("quote"))!=config.value("quoteCommitment").toString()
        || pin(d.value("admission")).isEmpty() || pin(d.value("messageCommitment")).isEmpty()
        || (!message.isEmpty() && pin(d.value("messageCommitment"))!=message)
        || d.value("bchNetwork")!=t.value("bchNetwork") || d.value("bchGenesis")!=t.value("bchGenesis")) return false;
    const auto outpoint=d.value("fundingOutpoint").toString().split(':');
    quint64 index=0,principal=0,expected=0,fee=0,maximum=0;
    if (outpoint.size()!=2 || !txid(outpoint.value(0)) || !decimal(outpoint.value(1),&index) || index>4294967295ULL
        || (!transaction.isEmpty() && outpoint.value(0)!=transaction)
        || !decimal(d.value("principalAtoms"),&principal) || !decimal(t.value("bchAtoms"),&expected)
        || !principal || principal!=expected || !decimal(d.value("feeAtoms"),&fee) || !fee
        || !decimal(t.value("bchFundingFeeMaxAtoms"),&maximum) || fee>maximum) return false;
    // Current BCH covenant output is P2SH32: HASH256, PUSH32, hash, EQUAL.
    const auto script=d.value("contractScript").toString();
    if (script.size()!=70 || !script.startsWith("aa20") || !script.endsWith("87") || !hex(script,70)) return false;
    QByteArray binding=schema.toUtf8();binding.append('\0');binding+=QJsonDocument(d).toJson(QJsonDocument::Compact);
    return value.value("commitment")==QString::fromLatin1(QCryptographicHash::hash(binding,QCryptographicHash::Sha256).toHex());
}
}
inline bool review(const QJsonObject &value,const QJsonObject &config,const QJsonObject &record,
    const QString &message,const QString &transaction) {
    return !message.isEmpty() && !transaction.isEmpty() && detail::validate(value,config,record,message,transaction);
}
// Saved status is history from the pinned helper, not new acceptance authority.
inline bool savedReview(const QJsonObject &value,const QJsonObject &config,const QJsonObject &record) {
    return detail::validate(value,config,record,{},{});
}
}
#endif
