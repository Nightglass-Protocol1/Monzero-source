#ifndef MONZERO_LITECOIN_LIGHT_FUNDING_CONTEXT_H
#define MONZERO_LITECOIN_LIGHT_FUNDING_CONTEXT_H
#include "LitecoinJournalProtocol.h"
#include <QJsonDocument>
#include <QUuid>
#ifdef Q_OS_LINUX
#include <sys/file.h>
#include <cerrno>
#endif
// Immutable desktop intent and retained claim, separate from engine journals.
// An incomplete record fails closed; it is never deleted or overwritten here.
namespace LitecoinLightFundingContext {
struct Retained {QJsonObject payment,review;QString txid;};
#ifdef Q_OS_LINUX
struct Fd {int value=-1;~Fd(){if(value>=0)::close(value);}Fd(const Fd&)=delete;Fd& operator=(const Fd&)=delete;explicit Fd(int fd):value(fd){}};
inline bool read(int root,const QByteArray &name,QByteArray &bytes) {
    Fd file{::openat(root,name.constData(),O_RDONLY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC)};
    if(file.value<0){if(errno==ENOENT){bytes.clear();return true;}return false;}
    struct stat before{},after{};
    if(::fstat(file.value,&before)!=0 || !S_ISREG(before.st_mode) || before.st_uid!=::geteuid()
        || (before.st_mode&07777)!=0600 || before.st_nlink!=1 || before.st_size<=0 || before.st_size>4096)return false;
    QByteArray result;char buffer[4096];
    for(;;){const auto n=::read(file.value,buffer,sizeof(buffer));if(n<0 && errno==EINTR)continue;
        if(n<0)return false;if(n==0)break;result.append(buffer,int(n));if(result.size()>4096)return false;}
    if(::fstat(file.value,&after)!=0 || result.size()!=before.st_size || after.st_size!=before.st_size
        || before.st_mtim.tv_sec!=after.st_mtim.tv_sec || before.st_mtim.tv_nsec!=after.st_mtim.tv_nsec
        || before.st_ctim.tv_sec!=after.st_ctim.tv_sec || before.st_ctim.tv_nsec!=after.st_ctim.tv_nsec)return false;
    bytes=result;return true;
}
inline bool retain(int root,const QByteArray &name,const QByteArray &bytes) {
    if(bytes.isEmpty() || bytes.size()>4096)return false;
    QByteArray existing;if(!read(root,name,existing))return false;
    if(!existing.isEmpty()) {
        Fd file{::openat(root,name.constData(),O_RDONLY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC)};
        return existing==bytes && file.value>=0 && ::fsync(file.value)==0 && ::fsync(root)==0;
    }
    Fd file{::openat(root,name.constData(),O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600)};
    if(file.value<0)return false;
    qint64 offset=0;while(offset<bytes.size()) {const auto n=::write(file.value,bytes.constData()+offset,bytes.size()-offset);
        if(n<0 && errno==EINTR)continue;if(n<=0)return false;offset+=n;}
    if(::fsync(file.value)!=0 || ::fsync(root)!=0)return false;
    return read(root,name,existing) && existing==bytes;
}
#endif
inline QByteArray json(const QJsonObject &value){return QJsonDocument(value).toJson(QJsonDocument::Compact);}
inline bool hash(const QJsonValue &value){return value.isString() && QRegularExpression("\\A[0-9a-f]{64}\\z").match(value.toString()).hasMatch();}
inline bool validPayment(const QJsonObject &p) {
    if(p.size()!=5 || !p.value("address").isString() || p.value("address").toString().isEmpty() || p.value("address").toString().size()>128)return false;
    for(const auto key:{"amountAtomic","feeAtomic","minimumChangeAtomic"}) {
        bool ok=false;const auto v=p.value(key).toString();const auto n=v.toULongLong(&ok);if(!ok || n==0 || QString::number(n)!=v)return false;
    }
    const auto n=p.value("minimumConfirmations");return n.isDouble() && n.toDouble()>=1 && n.toDouble()<=1000 && n.toDouble()==n.toInt();
}
inline bool validReview(const QJsonObject &r) {
    return r.size()==2 && hash(r.value("signingCommitment")) && r.contains("changeAddress")
        && (r.value("changeAddress").isNull() || (r.value("changeAddress").isString()
            && !r.value("changeAddress").toString().isEmpty() && r.value("changeAddress").toString().size()<=128));
}
// Empty proposed fields mean read-only. Review retention requires an existing
// matching payment intent and a helper-validated claim. No signing consent saved.
inline bool access(const QString &directory,const QString &trade,const QString &configuration,
    Retained &out,const QJsonObject &payment=QJsonObject{},const QJsonObject &review=QJsonObject{},const QString &txid=QString{}) {
#ifndef Q_OS_LINUX
    Q_UNUSED(directory) Q_UNUSED(trade) Q_UNUSED(configuration) Q_UNUSED(out) Q_UNUSED(payment) Q_UNUSED(review) Q_UNUSED(txid)
    return false;
#else
    const QUuid id(trade);const QFileInfo info(directory);
    if(id.isNull() || id.toString(QUuid::WithoutBraces)!=trade || !hash(configuration)
        || !info.isAbsolute() || info.canonicalFilePath()!=directory || (!payment.isEmpty() && !validPayment(payment))
        || (review.isEmpty()!=txid.isEmpty()) || (!review.isEmpty() && (!validReview(review) || !hash(txid) || payment.isEmpty())))return false;
    Fd root{::open(QFile::encodeName(directory).constData(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC)};struct stat st{};
    if(root.value<0 || ::fstat(root.value,&st)!=0 || st.st_uid!=::geteuid() || (st.st_mode&0077)!=0
        || ::flock(root.value,LOCK_EX|LOCK_NB)!=0)return false;
    const auto prefix="ltc-light-dex-ui-"+trade.toLatin1();const auto requestName=prefix+"-payment.json",reviewName=prefix+"-review.json";
    QByteArray requestBytes,reviewBytes;if(!read(root.value,requestName,requestBytes) || !read(root.value,reviewName,reviewBytes))return false;
    if(!review.isEmpty() && requestBytes.isEmpty())return false;
    Retained saved;
    if(!requestBytes.isEmpty()) {
        const auto r=QJsonDocument::fromJson(requestBytes).object();
        if(r.size()!=4 || json(r)!=requestBytes || r.value("schema")!="monzero-ltc-light-dex-ui-payment-v1"
            || r.value("tradeId")!=trade || r.value("configurationDigest")!=configuration || !r.value("payment").isObject()
            || !validPayment(r.value("payment").toObject()))return false;
        saved.payment=r.value("payment").toObject();
    }
    if(!reviewBytes.isEmpty()) {
        const auto r=QJsonDocument::fromJson(reviewBytes).object();
        const auto digest=QString::fromLatin1(QCryptographicHash::hash(requestBytes,QCryptographicHash::Sha256).toHex());
        if(saved.payment.isEmpty() || r.size()!=5 || json(r)!=reviewBytes || r.value("schema")!="monzero-ltc-light-dex-ui-review-v1"
            || r.value("tradeId")!=trade || r.value("paymentDigest")!=digest || !validReview(r.value("review").toObject()) || !hash(r.value("txid")))return false;
        saved.review=r.value("review").toObject();saved.txid=r.value("txid").toString();
    }
    if(!payment.isEmpty()) {
        const auto proposed=json({{"schema","monzero-ltc-light-dex-ui-payment-v1"},{"tradeId",trade},{"configurationDigest",configuration},{"payment",payment}});
        if(!requestBytes.isEmpty() && requestBytes!=proposed)return false;
        if(!retain(root.value,requestName,proposed))return false;
        requestBytes=proposed;saved.payment=payment;
    }
    if(!review.isEmpty()) {
        const auto digest=QString::fromLatin1(QCryptographicHash::hash(requestBytes,QCryptographicHash::Sha256).toHex());
        const auto proposed=json({{"schema","monzero-ltc-light-dex-ui-review-v1"},{"tradeId",trade},{"paymentDigest",digest},{"review",review},{"txid",txid}});
        if(!retain(root.value,reviewName,proposed))return false;saved.review=review;saved.txid=txid;
    }
    out=saved;return true;
#endif
}
}
#endif
