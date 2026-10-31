#ifndef MONZERO_LITECOIN_PAYMENT_REQUEST_H
#define MONZERO_LITECOIN_PAYMENT_REQUEST_H
#include "LitecoinJournalProtocol.h"
#include <QJsonDocument>
#include <QCryptographicHash>
#include <QUuid>
#include <QJsonArray>
#include <algorithm>
#ifdef Q_OS_LINUX
#include <sys/file.h>
#include <dirent.h>
#include <cerrno>
#endif
namespace LitecoinPaymentRequest {
// Retain exact request context before launching the helper. Partial records
// deliberately fail closed; there is no automatic deletion or new-ID fallback.
inline bool retain(const QString &directory,const QJsonObject &context,QString &requestId) {
#ifdef Q_OS_LINUX
    const auto payload=QJsonDocument(context).toJson(QJsonDocument::Compact);
    if (payload.isEmpty() || payload.size()>3072) return false;
    const QFileInfo info(directory);
    if (!info.isAbsolute() || info.canonicalFilePath()!=directory) return false;
    struct Fd {int value;~Fd() {if (value>=0) ::close(value);}};
    Fd root{::open(QFile::encodeName(directory).constData(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC)};
    struct stat metadata{};
    if (root.value<0 || ::fstat(root.value,&metadata)!=0 || metadata.st_uid!=::geteuid()
        || (metadata.st_mode&0077)!=0 || ::flock(root.value,LOCK_EX|LOCK_NB)!=0) return false;
    const auto name="ltc-ui-request-"+QCryptographicHash::hash(payload,QCryptographicHash::Sha256).toHex()+".json";
    Fd record{::openat(root.value,name.constData(),O_RDONLY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC)};
    QString identity;
    if (record.value>=0) {
        if (::fstat(record.value,&metadata)!=0 || !S_ISREG(metadata.st_mode) || metadata.st_uid!=::geteuid()
            || (metadata.st_mode&07777)!=0600 || metadata.st_nlink!=1 || metadata.st_size<=0 || metadata.st_size>4096) return false;
        QByteArray bytes;char chunk[4096];
        for (;;) {
            const auto count=::read(record.value,chunk,sizeof(chunk));
            if (count<0 && errno==EINTR) continue;
            if (count<0) return false;if (count==0) break;
            bytes.append(chunk,int(count));if (bytes.size()>4096) return false;
        }
        const auto document=QJsonDocument::fromJson(bytes);const auto saved=document.object();
        if (bytes.size()!=metadata.st_size || !document.isObject() || document.toJson(QJsonDocument::Compact)!=bytes
            || saved.size()!=3 || saved.value("schema")!="monzero-ltc-ui-payment-request-v1"
            || saved.value("context")!=context || !saved.value("requestId").isString()) return false;
        identity=saved.value("requestId").toString();const QUuid id(identity);
        if (id.isNull() || id.toString(QUuid::WithoutBraces)!=identity) return false;
    } else {
        if (errno!=ENOENT) return false;
        identity=QUuid::createUuid().toString(QUuid::WithoutBraces);
        const auto bytes=QJsonDocument(QJsonObject{{"schema","monzero-ltc-ui-payment-request-v1"},
            {"context",context},{"requestId",identity}}).toJson(QJsonDocument::Compact);
        record.value=::openat(root.value,name.constData(),O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
        if (record.value<0) return false;
        qint64 offset=0;
        while (offset<bytes.size()) {
            const auto count=::write(record.value,bytes.constData()+offset,bytes.size()-offset);
            if (count<0 && errno==EINTR) continue;
            if (count<=0) return false;offset+=count;
        }
    }
    if (::fsync(record.value)!=0 || ::fsync(root.value)!=0) return false;
    requestId=identity;return true;
#else
    Q_UNUSED(directory) Q_UNUSED(context) Q_UNUSED(requestId)
    return false;
#endif
}
#ifdef Q_OS_LINUX
// Caller holds the shared directory lease. An absent review is a legacy request,
// not permission to infer a commitment from the native spend manifest.
inline bool readReview(int root,const QJsonObject &context,const QString &identity,QJsonObject &review) {
    const auto payload=QJsonDocument(context).toJson(QJsonDocument::Compact);
    const auto name="ltc-ui-review-"+QCryptographicHash::hash(payload,QCryptographicHash::Sha256).toHex()+".json";
    struct Fd {int value;~Fd() {if(value>=0) ::close(value);}} record{::openat(root,name.constData(),O_RDONLY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC)};
    if (record.value<0) {if(errno==ENOENT) {review={};return true;}return false;}
    struct stat metadata{};
    if (::fstat(record.value,&metadata)!=0 || !S_ISREG(metadata.st_mode) || metadata.st_uid!=::geteuid()
        || (metadata.st_mode&07777)!=0600 || metadata.st_nlink!=1 || metadata.st_size<=0 || metadata.st_size>4096) return false;
    QByteArray bytes;char chunk[4096];
    for (;;) {
        const auto count=::read(record.value,chunk,sizeof(chunk));
        if (count<0 && errno==EINTR) continue;
        if (count<0) return false;if (count==0) break;
        bytes.append(chunk,int(count));if(bytes.size()>4096) return false;
    }
    const auto document=QJsonDocument::fromJson(bytes);const auto saved=document.object();
    if (bytes.size()!=metadata.st_size || !document.isObject() || document.toJson(QJsonDocument::Compact)!=bytes
        || saved.size()!=4 || saved.value("schema")!="monzero-ltc-ui-payment-review-v1" || saved.value("context")!=context
        || saved.value("requestId")!=identity || !saved.value("review").isObject() || saved.value("review").toObject().isEmpty()) return false;
    review=saved.value("review").toObject();return true;
}
#endif
inline bool retainReview(const QString &directory,const QJsonObject &context,const QString &identity,const QJsonObject &review) {
#ifdef Q_OS_LINUX
    const auto bytes=QJsonDocument(QJsonObject{{"schema","monzero-ltc-ui-payment-review-v1"},
        {"context",context},{"requestId",identity},{"review",review}}).toJson(QJsonDocument::Compact);
    if (review.isEmpty() || bytes.size()>4096) return false;
    const QFileInfo info(directory);
    if (!info.isAbsolute() || info.canonicalFilePath()!=directory) return false;
    struct Fd {int value;~Fd() {if(value>=0) ::close(value);}};
    Fd root{::open(QFile::encodeName(directory).constData(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC)};
    struct stat metadata{};
    if (root.value<0 || ::fstat(root.value,&metadata)!=0 || metadata.st_uid!=::geteuid()
        || (metadata.st_mode&0077)!=0 || ::flock(root.value,LOCK_EX|LOCK_NB)!=0) return false;
    const auto requestBytes=QJsonDocument(QJsonObject{{"schema","monzero-ltc-ui-payment-request-v1"},
        {"context",context},{"requestId",identity}}).toJson(QJsonDocument::Compact);
    const auto requestName="ltc-ui-request-"+QCryptographicHash::hash(QJsonDocument(context).toJson(QJsonDocument::Compact),QCryptographicHash::Sha256).toHex()+".json";
    Fd request{::openat(root.value,requestName.constData(),O_RDONLY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC)};
    if (request.value<0 || ::fstat(request.value,&metadata)!=0 || !S_ISREG(metadata.st_mode) || metadata.st_uid!=::geteuid()
        || (metadata.st_mode&07777)!=0600 || metadata.st_nlink!=1 || metadata.st_size!=requestBytes.size() || metadata.st_size>4096) return false;
    QByteArray retained;char chunk[4096];
    for (;;) {
        const auto count=::read(request.value,chunk,sizeof(chunk));
        if (count<0 && errno==EINTR) continue;
        if (count<0) return false;if(count==0) break;
        retained.append(chunk,int(count));if(retained.size()>4096) return false;
    }
    if (retained!=requestBytes) return false;
    QJsonObject existing;if (!readReview(root.value,context,identity,existing) || (!existing.isEmpty() && existing!=review)) return false;
    const auto payload=QJsonDocument(context).toJson(QJsonDocument::Compact);
    const auto name="ltc-ui-review-"+QCryptographicHash::hash(payload,QCryptographicHash::Sha256).toHex()+".json";
    Fd record{::openat(root.value,name.constData(),existing.isEmpty()?O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC:O_RDONLY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC,0600)};
    if (record.value<0) return false;
    if (existing.isEmpty()) {
        qint64 offset=0;
        while (offset<bytes.size()) {
            const auto count=::write(record.value,bytes.constData()+offset,bytes.size()-offset);
            if (count<0 && errno==EINTR) continue;
            if (count<=0) return false;offset+=count;
        }
    }
    if (::fsync(record.value)!=0 || ::fsync(root.value)!=0) return false;
    QJsonObject saved;return readReview(root.value,context,identity,saved) && saved==review;
#else
    Q_UNUSED(directory) Q_UNUSED(context) Q_UNUSED(identity) Q_UNUSED(review)
    return false;
#endif
}
// Read-only listing. Malformed records abort the complete result; private
// request details are exposed only when their wallet context matches exactly.
inline bool list(const QString &directory,const QJsonObject &wallet,QJsonArray &output) {
#ifdef Q_OS_LINUX
    const QFileInfo info(directory);
    if (!info.isAbsolute() || info.canonicalFilePath()!=directory) return false;
    struct Fd {int value;~Fd() {if (value>=0) ::close(value);}};
    Fd root{::open(QFile::encodeName(directory).constData(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC)};
    struct stat metadata{};
    if (root.value<0 || ::fstat(root.value,&metadata)!=0 || metadata.st_uid!=::geteuid()
        || (metadata.st_mode&0077)!=0 || ::flock(root.value,LOCK_SH|LOCK_NB)!=0) return false;
    const int duplicate=::dup(root.value);if (duplicate<0) return false;
    struct Directory {DIR *value;~Directory() {if(value) ::closedir(value);}} entries{::fdopendir(duplicate)};
    if (!entries.value) {::close(duplicate);return false;}
    QList<QJsonObject> found;int count=0,requests=0;
    for (;;) {
        errno=0;const auto entry=::readdir(entries.value);
        if (!entry) {if(errno!=0) return false;break;}
        if (++count>50000) return false;
        const QByteArray name(entry->d_name);
        if (!name.startsWith("ltc-ui-request-")) continue;
        if (++requests>1000 || !QRegularExpression("\\Altc-ui-request-[0-9a-f]{64}\\.json\\z").match(QString::fromLatin1(name)).hasMatch()) return false;
        Fd record{::openat(root.value,name.constData(),O_RDONLY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC)};
        if (record.value<0 || ::fstat(record.value,&metadata)!=0 || !S_ISREG(metadata.st_mode)
            || metadata.st_uid!=::geteuid() || (metadata.st_mode&07777)!=0600 || metadata.st_nlink!=1
            || metadata.st_size<=0 || metadata.st_size>4096) return false;
        QByteArray bytes;char chunk[4096];
        for (;;) {
            const auto length=::read(record.value,chunk,sizeof(chunk));
            if (length<0 && errno==EINTR) continue;
            if (length<0) return false;if (length==0) break;
            bytes.append(chunk,int(length));if(bytes.size()>4096) return false;
        }
        const auto document=QJsonDocument::fromJson(bytes);const auto saved=document.object();
        if (bytes.size()!=metadata.st_size || !document.isObject() || document.toJson(QJsonDocument::Compact)!=bytes
            || saved.size()!=3 || saved.value("schema")!="monzero-ltc-ui-payment-request-v1"
            || !saved.value("context").isObject() || !saved.value("requestId").isString()) return false;
        const auto context=saved.value("context").toObject();
        if (context.size()!=3 || !context.value("wallet").isObject() || !context.value("payment").isObject()
            || !context.value("scan").isObject()) return false;
        const auto payload=QJsonDocument(context).toJson(QJsonDocument::Compact);
        if (payload.size()>3072 || name!="ltc-ui-request-"+QCryptographicHash::hash(payload,QCryptographicHash::Sha256).toHex()+".json") return false;
        const auto identity=saved.value("requestId").toString();const QUuid id(identity);
        if (id.isNull() || id.toString(QUuid::WithoutBraces)!=identity) return false;
        QJsonObject review;if (!readReview(root.value,context,identity,review)) return false;
        if (context.value("wallet")==wallet) {
            QJsonObject item{{"requestId",identity},{"payment",context.value("payment")},{"scan",context.value("scan")}};
            if (!review.isEmpty()) item.insert("review",review);
            found.append(item);
        }
    }
    std::sort(found.begin(),found.end(),[](const QJsonObject &a,const QJsonObject &b){return a.value("requestId").toString()<b.value("requestId").toString();});
    QJsonArray result;for(const auto &item:found) result.append(item);output=result;return true;
#else
    Q_UNUSED(directory) Q_UNUSED(wallet) Q_UNUSED(output)
    return false;
#endif
}

}
#endif
