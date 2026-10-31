#ifndef MONZERO_BCH_LIGHT_FUNDING_CONTEXT_H
#define MONZERO_BCH_LIGHT_FUNDING_CONTEXT_H
#include "LitecoinLightFundingContext.h"

// Desktop intent only: no keys, passwords or reusable signing/relay approvals.
// BCH P2PKH signing changes txid, so unsigned review and final identity are
// retained separately. Every stage binds the exact bytes of its predecessor.
namespace BchLightFundingContext {
struct Retained {QJsonObject payment,review;QString unsignedTxid,signedTxid;};
inline QString digest(const QByteArray &bytes) {
    return QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex());
}
inline bool access(const QString &directory,const QString &trade,const QString &configuration,
    Retained &out,const QJsonObject &payment=QJsonObject{},const QJsonObject &review=QJsonObject{},
    const QString &unsignedTxid=QString{},const QString &signedTxid=QString{}) {
    out={};
#ifndef Q_OS_LINUX
    Q_UNUSED(directory) Q_UNUSED(trade) Q_UNUSED(configuration) Q_UNUSED(payment)
    Q_UNUSED(review) Q_UNUSED(unsignedTxid) Q_UNUSED(signedTxid)
    return false;
#else
    namespace Store=LitecoinLightFundingContext;
    const QUuid id(trade);const QFileInfo info(directory);
    if(id.isNull() || id.toString(QUuid::WithoutBraces)!=trade || !Store::hash(configuration)
        || !info.isAbsolute() || info.canonicalFilePath()!=directory
        || (!payment.isEmpty() && !Store::validPayment(payment))
        || (review.isEmpty()!=unsignedTxid.isEmpty())
        || (!review.isEmpty() && (!Store::validReview(review) || !Store::hash(unsignedTxid)))
        || (!signedTxid.isEmpty() && !Store::hash(signedTxid)))return false;
    Store::Fd root{::open(QFile::encodeName(directory).constData(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC)};
    struct stat st{};
    if(root.value<0 || ::fstat(root.value,&st)!=0 || st.st_uid!=::geteuid() || (st.st_mode&0077)!=0
        || ::flock(root.value,LOCK_EX|LOCK_NB)!=0)return false;
    const auto prefix="bch-light-dex-ui-"+trade.toLatin1();
    const auto paymentName=prefix+"-payment.json",reviewName=prefix+"-review.json",signedName=prefix+"-signed.json";
    QByteArray paymentBytes,reviewBytes,signedBytes;
    if(!Store::read(root.value,paymentName,paymentBytes) || !Store::read(root.value,reviewName,reviewBytes)
        || !Store::read(root.value,signedName,signedBytes))return false;
    Retained saved;
    if(!paymentBytes.isEmpty()) {
        const auto r=QJsonDocument::fromJson(paymentBytes).object();
        if(r.size()!=4 || Store::json(r)!=paymentBytes || r.value("schema")!="monzero-bch-light-dex-ui-payment-v1"
            || r.value("tradeId")!=trade || r.value("configurationDigest")!=configuration
            || !Store::validPayment(r.value("payment").toObject()))return false;
        saved.payment=r.value("payment").toObject();
    }
    if(!reviewBytes.isEmpty()) {
        const auto r=QJsonDocument::fromJson(reviewBytes).object();
        if(paymentBytes.isEmpty() || r.size()!=5 || Store::json(r)!=reviewBytes
            || r.value("schema")!="monzero-bch-light-dex-ui-review-v1" || r.value("tradeId")!=trade
            || r.value("paymentDigest")!=digest(paymentBytes) || !Store::validReview(r.value("review").toObject())
            || !Store::hash(r.value("unsignedTxid")))return false;
        saved.review=r.value("review").toObject();saved.unsignedTxid=r.value("unsignedTxid").toString();
    }
    if(!signedBytes.isEmpty()) {
        const auto r=QJsonDocument::fromJson(signedBytes).object();
        if(reviewBytes.isEmpty() || r.size()!=4 || Store::json(r)!=signedBytes
            || r.value("schema")!="monzero-bch-light-dex-ui-signed-v1" || r.value("tradeId")!=trade
            || r.value("reviewDigest")!=digest(reviewBytes) || !Store::hash(r.value("signedTxid")))return false;
        saved.signedTxid=r.value("signedTxid").toString();
    }
    // Validate the entire proposed transition before writing any stage.
    QByteArray proposedPayment=paymentBytes,proposedReview=reviewBytes,proposedSigned=signedBytes;
    if(!payment.isEmpty()) {
        proposedPayment=Store::json({{"schema","monzero-bch-light-dex-ui-payment-v1"},{"tradeId",trade},
            {"configurationDigest",configuration},{"payment",payment}});
        if(!paymentBytes.isEmpty() && proposedPayment!=paymentBytes)return false;
    }
    if(!review.isEmpty()) {
        if(proposedPayment.isEmpty())return false;
        proposedReview=Store::json({{"schema","monzero-bch-light-dex-ui-review-v1"},{"tradeId",trade},
            {"paymentDigest",digest(proposedPayment)},{"review",review},{"unsignedTxid",unsignedTxid}});
        if(!reviewBytes.isEmpty() && proposedReview!=reviewBytes)return false;
    }
    if(!signedTxid.isEmpty()) {
        if(proposedReview.isEmpty())return false;
        proposedSigned=Store::json({{"schema","monzero-bch-light-dex-ui-signed-v1"},{"tradeId",trade},
            {"reviewDigest",digest(proposedReview)},{"signedTxid",signedTxid}});
        if(!signedBytes.isEmpty() && proposedSigned!=signedBytes)return false;
    }
    if(!payment.isEmpty() && !Store::retain(root.value,paymentName,proposedPayment))return false;
    if(!review.isEmpty() && !Store::retain(root.value,reviewName,proposedReview))return false;
    if(!signedTxid.isEmpty() && !Store::retain(root.value,signedName,proposedSigned))return false;
    if(!payment.isEmpty())saved.payment=payment;
    if(!review.isEmpty()){saved.review=review;saved.unsignedTxid=unsignedTxid;}
    if(!signedTxid.isEmpty())saved.signedTxid=signedTxid;
    out=saved;return true;
#endif
}
}
#endif
