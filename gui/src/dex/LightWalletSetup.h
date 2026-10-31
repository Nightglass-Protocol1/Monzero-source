#ifndef MONZERO_LIGHT_WALLET_SETUP_H
#define MONZERO_LIGHT_WALLET_SETUP_H
#include "LightWalletProfile.h"
#include "LitecoinJournalProtocol.h"
#include <QJsonDocument>
#include <QUuid>
#include <QDir>

namespace LightWalletSetup {
// Return only a canonical existing folder's launcher. The caller must bind its
// helper to the trusted installation during configure(), using the same bytes.
inline QString savedLauncher(const QString &directory) {
    if (!QFileInfo(directory).isAbsolute() || !QFileInfo(directory).isDir()
        || QFileInfo(directory).canonicalFilePath()!=directory) return {};
    const auto launcher=QDir(directory).filePath("launcher.json");
    return LitecoinJournalProtocol::privateConfig(launcher).isEmpty()?QString{}:launcher;
}
// No keys or password enter setup. The helper creates the encrypted wallet only
// after the user reviews these settings and approves creation separately.
inline QString prepare(const QString &coin,const QString &network,const QString &parent,
                       const QString &helper,const QString &digest,const QString &host,int port,int accountIndex=0,const QString &backupSource=QString{}) {
#ifndef Q_OS_LINUX
    Q_UNUSED(coin) Q_UNUSED(network) Q_UNUSED(parent) Q_UNUSED(helper) Q_UNUSED(digest) Q_UNUSED(host) Q_UNUSED(port) Q_UNUSED(accountIndex) Q_UNUSED(backupSource)
    return {};
#else
    if (accountIndex<0 || !LightWalletProfile::networkAllowed(coin,network) || !BackendExecutable::matches(helper,digest)
        || parent.size()>1024 || QFileInfo(parent).canonicalFilePath()!=parent || !QFileInfo(parent).isAbsolute()
        || (host.isEmpty()?port!=0:port<1 || port>65535)
        || (!host.isEmpty() && !QRegularExpression("\\A[a-zA-Z0-9][a-zA-Z0-9.:-]{0,252}\\z").match(host).hasMatch())) return {};
    if (!backupSource.isEmpty() && (QFileInfo(backupSource).canonicalFilePath()!=backupSource
        || !QFileInfo(backupSource).isAbsolute() || LitecoinJournalProtocol::privateRecord(backupSource+"/litecoin-wallet.backup",2000000).isEmpty())) return {};
    struct Descriptor {int fd;~Descriptor(){if(fd>=0)::close(fd);}};
    Descriptor base{::open(QFile::encodeName(parent).constData(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC)};
    struct stat st{};
    if (base.fd<0 || ::fstat(base.fd,&st)!=0 || st.st_uid!=::geteuid() || (st.st_mode&0022)) return {};
    const auto identity=QUuid::createUuid().toString(QUuid::WithoutBraces);
    const auto name=coin+"-"+network+"-"+identity;
    if (::mkdirat(base.fd,name.toUtf8().constData(),0700)!=0) return {};
    Descriptor root{::openat(base.fd,name.toUtf8().constData(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC)};
    if (root.fd<0) return {};
    const auto path=QDir(parent).filePath(name);
    QJsonObject settings{{"schema",LightWalletProfile::schema(coin,"setup",true)},{"network",network},
        {"walletId",identity},{"seedEncoding","bip39-english-v1"},{"accountIndex",accountIndex},{"maximumHeight",10000000}};
    if (!backupSource.isEmpty()) {
        settings.insert("schema",LightWalletProfile::schema(coin,"backup-setup",true));
        settings.remove("walletId");settings.remove("accountIndex");settings.remove("seedEncoding");
    }
    for (const auto &key:QStringList{"setupDirectory","vaultDirectory","addressBookDirectory","backupDirectory","spendDirectory","cacheDirectory"}) {
        if(key=="backupDirectory" && !backupSource.isEmpty()){settings.insert(key,backupSource);continue;}
        if (::mkdirat(root.fd,key.toUtf8().constData(),0700)!=0) return {};
        settings.insert(key,QDir(path).filePath(key));
    }
    if (!host.isEmpty()) settings.insert("endpoint",QJsonObject{{"host",host},{"port",port}});
    const auto config=QDir(path).filePath("setup.json");
    const auto launcher=QDir(path).filePath("launcher.json");
    const QJsonObject wrapper{{"schema",LightWalletProfile::schema(coin,"light-launcher",true)},
        {"helper",helper},{"helperDigest",digest},{"configFile",config}};
    const auto write=[&](const char *name,const QJsonObject &object) {
        const auto bytes=QJsonDocument(object).toJson(QJsonDocument::Compact);
        if (bytes.size()>4096) return false;
        Descriptor file{::openat(root.fd,name,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600)};
        if(file.fd<0) return false;
        qint64 offset=0;
        while(offset<bytes.size()) {const auto count=::write(file.fd,bytes.constData()+offset,size_t(bytes.size()-offset));if(count<=0)return false;offset+=count;}
        return ::fsync(file.fd)==0;
    };
    if (!write("setup.json",settings) || !write("launcher.json",wrapper) || ::fsync(root.fd)!=0 || ::fsync(base.fd)!=0) return {};
    // Revalidate the named paths after publication. Failures leave only public
    // setup records; never recursively delete a path that might have changed.
    if (LitecoinJournalProtocol::privateConfig(config)!=QJsonDocument(settings).toJson(QJsonDocument::Compact)
        || LitecoinJournalProtocol::privateConfig(launcher)!=QJsonDocument(wrapper).toJson(QJsonDocument::Compact)) return {};
    return launcher;
#endif
}
}
#endif
