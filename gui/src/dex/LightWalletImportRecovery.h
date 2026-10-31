#ifndef MONZERO_LIGHT_WALLET_IMPORT_RECOVERY_H
#define MONZERO_LIGHT_WALLET_IMPORT_RECOVERY_H
#include "LightWalletDiscoveryProtocol.h"
#include <QDir>
#include <QUuid>
#ifdef Q_OS_LINUX
#include <sys/file.h>
#include <sys/syscall.h>
#include <linux/fs.h>
#include <cerrno>
#endif
namespace LightWalletImportRecovery {
inline QString directory(const QString &setup,const QString &digest) {return QDir(setup).filePath("import-recovery-"+digest);}
inline bool valid(const QJsonObject &record,const QByteArray &setup,const QByteArray &pending,const QString &coin) {
    const auto response=record.value("response").toObject(),scan=record.value("scan").toObject();
    const auto known=record.value("knownMinimum").toArray();const auto request=response.value("requestId").toString();
    const QUuid id(request);
    if (record.size()!=7 || record.value("schema")!=LightWalletProfile::schema(coin,"light-import-recovery-ui")
        || record.value("setupDigest")!=LightWalletDiscoveryProtocol::digest(setup)
        || record.value("configurationDigest")!=LightWalletDiscoveryProtocol::digest(pending)
        || id.isNull() || id.toString(QUuid::WithoutBraces)!=request || scan.size()!=3
        || !LightWalletDiscoveryProtocol::integer(scan.value("gap"),500) || scan.value("gap").toInt()==0
        || !LightWalletDiscoveryProtocol::integer(scan.value("addressesPerBranch"),500) || scan.value("addressesPerBranch").toInt()==0
        || !LightWalletDiscoveryProtocol::integer(scan.value("historyEntriesPerBranch"),1000) || scan.value("historyEntriesPerBranch").toInt()==0
        || !LightWalletDiscoveryProtocol::pair(record.value("knownMinimum"))
        || !LightWalletDiscoveryProtocol::integer(record.value("maximumDownloadHeaders"),10000)) return false;
    // Saved evidence is historical. Rechecking the current allocation hash here
    // would prevent reopening after the approved allocation transition.
    return LightWalletDiscoveryProtocol::validate(response,pending,coin,request,scan,known,
        quint32(record.value("maximumDownloadHeaders").toInt()),false);
}
inline QJsonObject load(const QString &root,const QString &digest,const QByteArray &setup,const QByteArray &pending,const QString &coin) {
    if (!LightWalletDiscoveryProtocol::hash(digest)) return {};
    const auto bytes=LitecoinJournalProtocol::privateConfig(directory(root,digest)+"/review.json",16384);
    const auto document=QJsonDocument::fromJson(bytes);const auto record=document.object();
    if (!document.isObject() || document.toJson(QJsonDocument::Compact)!=bytes || !valid(record,setup,pending,coin)
        || record.value("response").toObject().value("reviewDigest")!=digest) return {};
    return record;
}
inline QJsonArray list(const QString &root,const QByteArray &setup,const QByteArray &pending,const QString &coin) {
    QJsonArray result;const auto names=QDir(root).entryList({"import-recovery-*"},QDir::Dirs|QDir::NoDotAndDotDot,QDir::Name);
    if (names.size()>100) return result;
    for (const auto &name:names) {
        const auto digest=name.mid(QStringLiteral("import-recovery-").size());const auto record=load(root,digest,setup,pending,coin);
        if (record.isEmpty()) continue;
        const auto coverage=record.value("response").toObject().value("review").toObject().value("coverage").toObject();
        result.append(QJsonObject{{"reviewDigest",digest},{"height",coverage.value("height")},
            {"backupDirectory",directory(root,digest)+"/backup"},{"backupVerified",false}});
    }
    return result;
}
// Immutable public context only. Atomically publish after syncing the temporary
// file, preserving exact retry bytes and never overwriting another review.
inline bool retain(const QString &root,const QJsonObject &record) {
#ifndef Q_OS_LINUX
    Q_UNUSED(root) Q_UNUSED(record) return false;
#else
    const auto digest=record.value("response").toObject().value("reviewDigest").toString();
    const auto bytes=QJsonDocument(record).toJson(QJsonDocument::Compact);
    if (!LightWalletDiscoveryProtocol::hash(digest) || bytes.isEmpty() || bytes.size()>16384
        || !QFileInfo(root).isAbsolute() || QFileInfo(root).canonicalFilePath()!=root) return false;
    struct Fd {int n;~Fd(){if(n>=0)::close(n);}};
    const auto owned=[](int fd) {struct stat s{};return fd>=0 && ::fstat(fd,&s)==0 && s.st_uid==::geteuid() && !(s.st_mode&0077);};
    Fd base{::open(QFile::encodeName(root).constData(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC)};
    if (!owned(base.n) || ::flock(base.n,LOCK_EX|LOCK_NB)!=0) return false;
    const auto name=("import-recovery-"+digest).toLatin1();
    if (::mkdirat(base.n,name.constData(),0700)!=0 && errno!=EEXIST) return false;
    Fd session{::openat(base.n,name.constData(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC)};
    if (!owned(session.n) || ::flock(session.n,LOCK_EX|LOCK_NB)!=0) return false;
    for (const auto child:{"journal","backup"}) {
        if (::mkdirat(session.n,child,0700)!=0 && errno!=EEXIST) return false;
        Fd folder{::openat(session.n,child,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC)};
        if (!owned(folder.n) || ::fsync(folder.n)!=0) return false;
    }
    const auto path=directory(root,digest)+"/review.json";
    Fd existing{::openat(session.n,"review.json",O_RDONLY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC)};
    if (existing.n>=0) {
        if (LitecoinJournalProtocol::privateConfig(path)!=bytes || ::fsync(existing.n)!=0) return false;
    } else {
        if (errno!=ENOENT) return false;
        const auto temporary=(QUuid::createUuid().toString(QUuid::WithoutBraces)+".tmp").toLatin1();
        Fd file{::openat(session.n,temporary.constData(),O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600)};
        if (file.n<0) return false;
        qint64 offset=0;
        while(offset<bytes.size()) {
            const auto count=::write(file.n,bytes.constData()+offset,size_t(bytes.size()-offset));
            if(count<0 && errno==EINTR)continue;
            if(count<=0){::unlinkat(session.n,temporary.constData(),0);return false;}offset+=count;
        }
        if (::fsync(file.n)!=0) {::unlinkat(session.n,temporary.constData(),0);return false;}
        if (::syscall(SYS_renameat2,session.n,temporary.constData(),session.n,"review.json",RENAME_NOREPLACE)!=0) {
            ::unlinkat(session.n,temporary.constData(),0);return false;
        }
    }
    return ::fsync(session.n)==0 && ::fsync(base.n)==0 && LitecoinJournalProtocol::privateConfig(path)==bytes;
#endif
}
}
#endif
