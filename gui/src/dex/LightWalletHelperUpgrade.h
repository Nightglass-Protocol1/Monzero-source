#ifndef MONZERO_LIGHT_WALLET_HELPER_UPGRADE_H
#define MONZERO_LIGHT_WALLET_HELPER_UPGRADE_H
#include "LightWalletSetup.h"
#ifdef Q_OS_LINUX
#include <sys/file.h>
#include <sys/syscall.h>
#include <linux/fs.h>
#endif
namespace LightWalletHelperUpgrade {
inline QString hash(const QByteArray &bytes) {return QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex());}
inline QString path(const QString &directory,const QString &helper,const QString &digest) {
    return QDir(directory).filePath("launcher-helper-"+hash(helper.toUtf8()+QByteArray(1,'\0')+digest.toLatin1())+".json");
}
// An installed-helper-specific launcher never replaces the original settings.
// A damaged migration must fail closed, not fall back to another helper.
inline QString selected(const QString &directory,const QString &helper,const QString &digest) {
    const auto candidate=path(directory,helper,digest);
    if (QFileInfo::exists(candidate) || QFileInfo(candidate).isSymLink()) return candidate;
    return LightWalletSetup::savedLauncher(directory);
}
inline QJsonObject review(const QString &coin,const QString &directory,const QString &helper,const QString &digest) {
    if (!LightWalletProfile::supported(coin) || !BackendExecutable::matches(helper,digest)) return {};
    const auto launcher=LightWalletSetup::savedLauncher(directory);
    const auto bytes=LitecoinJournalProtocol::privateConfig(launcher);
    const auto document=QJsonDocument::fromJson(bytes);const auto old=document.object();
    if (!document.isObject() || document.toJson(QJsonDocument::Compact)!=bytes || old.size()!=4
        || old.value("schema")!=LightWalletProfile::schema(coin,"light-launcher",true)
        || !old.value("helper").isString() || !QFileInfo(old.value("helper").toString()).isAbsolute()
        || !QRegularExpression("\\A[0-9a-f]{64}\\z").match(old.value("helperDigest").toString()).hasMatch()
        || (old.value("helper")==helper && old.value("helperDigest")==digest)) return {};
    const auto config=LitecoinJournalProtocol::privateConfig(old.value("configFile").toString());
    const auto parsed=QJsonDocument::fromJson(config);
    if (!parsed.isObject() || parsed.toJson(QJsonDocument::Compact)!=config
        || !QStringList{LightWalletProfile::schema(coin,"setup",true),LightWalletProfile::schema(coin,"light",true),LightWalletProfile::schema(coin,"backup-setup",true)}.contains(parsed.object().value("schema").toString())) return {};
    QJsonObject result{{"coin",coin},{"directory",directory},{"previousHelper",old.value("helper")},
        {"previousDigest",old.value("helperDigest")},{"helper",helper},{"helperDigest",digest},
        {"launcherDigest",hash(bytes)},{"configurationFile",old.value("configFile")},{"configurationDigest",hash(config)}};
    result.insert("commitment",hash(QJsonDocument(result).toJson(QJsonDocument::Compact)));return result;
}
// Publish approved public settings only, with no replacement or helper execution.
inline QString apply(const QString &coin,const QString &directory,const QString &helper,const QString &digest,const QString &approval) {
#ifndef Q_OS_LINUX
    Q_UNUSED(coin) Q_UNUSED(directory) Q_UNUSED(helper) Q_UNUSED(digest) Q_UNUSED(approval) return {};
#else
    const auto reviewed=review(coin,directory,helper,digest);
    if (reviewed.isEmpty() || reviewed.value("commitment")!=approval) return {};
    struct Fd {int n;~Fd(){if(n>=0)::close(n);}};
    Fd root{::open(QFile::encodeName(directory).constData(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC)};
    struct stat st{};
    if(root.n<0 || ::fstat(root.n,&st)!=0 || st.st_uid!=::geteuid() || (st.st_mode&0077)
        || ::flock(root.n,LOCK_EX|LOCK_NB)!=0) return {};
    const QJsonObject wrapper{{"schema",LightWalletProfile::schema(coin,"light-launcher",true)},
        {"helper",helper},{"helperDigest",digest},{"configFile",reviewed.value("configurationFile")}};
    const auto bytes=QJsonDocument(wrapper).toJson(QJsonDocument::Compact);
    const auto target=path(directory,helper,digest);
    if (QFileInfo::exists(target) || QFileInfo(target).isSymLink()) {
        // A retry may follow a crash between rename and directory sync.
        Fd retained{::open(QFile::encodeName(target).constData(),O_RDONLY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC)};
        if (retained.n<0 || LitecoinJournalProtocol::privateConfig(target)!=bytes
            || review(coin,directory,helper,digest)!=reviewed || ::fsync(retained.n)!=0 || ::fsync(root.n)!=0) return {};
        return LitecoinJournalProtocol::privateConfig(target)==bytes?target:QString{};
    }
    const auto temporary=(".helper-upgrade-"+QUuid::createUuid().toString(QUuid::WithoutBraces)).toLatin1();
    Fd file{::openat(root.n,temporary.constData(),O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600)};
    if (file.n<0) return {};
    qint64 offset=0;
    while(offset<bytes.size()) {const auto count=::write(file.n,bytes.constData()+offset,size_t(bytes.size()-offset));if(count<=0)return {};offset+=count;}
    if (::fsync(file.n)!=0 || review(coin,directory,helper,digest)!=reviewed) return {};
    const auto name=QFileInfo(target).fileName().toLatin1();
    if (::syscall(SYS_renameat2,root.n,temporary.constData(),root.n,name.constData(),RENAME_NOREPLACE)!=0 || ::fsync(root.n)!=0) return {};
    return LitecoinJournalProtocol::privateConfig(target)==bytes && review(coin,directory,helper,digest)==reviewed?target:QString{};
#endif
}
}
#endif
