#include "PrivateBackendConfig.h"
#include "BackendExecutable.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

bool PrivateBackendConfig::load(const QString &path, PrivateBackendConfig &result) {
    result={};
#ifndef Q_OS_LINUX
    Q_UNUSED(path)
    return false;
#else
    const QFileInfo info(path);
    if (!info.isAbsolute() || info.canonicalFilePath()!=path) return false;
    const int fd=::open(QFile::encodeName(path).constData(),O_RDONLY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC);
    if (fd<0) return false;
    QFile file;
    if (!file.open(fd,QIODevice::ReadOnly,QFileDevice::AutoCloseHandle)) { ::close(fd); return false; }
    struct stat before{}, after{};
    if (::fstat(fd,&before)!=0 || !S_ISREG(before.st_mode) || before.st_uid!=::geteuid()
        || before.st_nlink!=1 || (before.st_mode&07777)!=0600 || before.st_size<=0 || before.st_size>8192) return false;
    const auto bytes=file.read(8193);
    if (::fstat(fd,&after)!=0 || bytes.size()!=before.st_size || before.st_size!=after.st_size
        || before.st_mtim.tv_sec!=after.st_mtim.tv_sec || before.st_mtim.tv_nsec!=after.st_mtim.tv_nsec
        || before.st_ctim.tv_sec!=after.st_ctim.tv_sec || before.st_ctim.tv_nsec!=after.st_ctim.tv_nsec) return false;
    const auto document=QJsonDocument::fromJson(bytes);
    if (!document.isObject() || document.toJson(QJsonDocument::Compact)!=bytes) return false;
    const auto object=document.object();
    if (object.size()!=9 || object.value("schema")!="monzero-private-backend-v1") return false;
    for (const auto name : {"schema","directory","helper","helperDigest","rpc","rpcDigest","genesis","daemonPort","walletPort"})
        if (!object.value(name).isString()) return false;
    PrivateBackendConfig config;
    config.directory=object.value("directory").toString();
    config.helper=object.value("helper").toString();
    config.helperDigest=object.value("helperDigest").toString();
    config.rpc=object.value("rpc").toString();
    config.rpcDigest=object.value("rpcDigest").toString();
    config.genesis=object.value("genesis").toString();
    static const QRegularExpression hash("\\A[0-9a-f]{64}\\z");
    if (!hash.match(config.genesis).hasMatch()) return false;
    const auto port=[&](const char *name,quint16 &value) {
        const auto text=object.value(name).toString();
        bool ok=false; value=text.toUShort(&ok);
        return ok && value>0 && QString::number(value)==text;
    };
    if (!port("daemonPort",config.daemonPort) || !port("walletPort",config.walletPort)
        || config.daemonPort==config.walletPort) return false;
    const QFileInfo directory(config.directory);
    struct stat metadata{};
    if (!directory.isAbsolute() || directory.canonicalFilePath()!=config.directory
        || ::lstat(QFile::encodeName(config.directory).constData(),&metadata)!=0
        || !S_ISDIR(metadata.st_mode) || metadata.st_uid!=::geteuid() || (metadata.st_mode&0077)!=0) return false;
    if (!BackendExecutable::matches(config.helper,config.helperDigest)
        || !BackendExecutable::matches(config.rpc,config.rpcDigest)) return false;
    result=config;
    return true;
#endif
}
