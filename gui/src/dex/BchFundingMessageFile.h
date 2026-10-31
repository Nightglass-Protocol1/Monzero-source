#ifndef MONZERO_BCH_FUNDING_MESSAGE_FILE_H
#define MONZERO_BCH_FUNDING_MESSAGE_FILE_H
#include "BackendExecutable.h"
namespace BchFundingMessageFile {
// Dedicated maximum for the bounded numeric-byte-array funding envelope.
inline QByteArray read(const QString &path) {
#ifndef Q_OS_LINUX
    Q_UNUSED(path)
    return {};
#else
    constexpr qint64 maximum=6004096;
    const QFileInfo info(path);
    if (!info.isAbsolute() || info.canonicalFilePath()!=path) return {};
    const int fd=::open(QFile::encodeName(path).constData(),O_RDONLY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC);
    if (fd<0) return {};
    QFile file;
    if (!file.open(fd,QIODevice::ReadOnly,QFileDevice::AutoCloseHandle)) {::close(fd);return {};}
    struct stat before{},after{};
    if (::fstat(fd,&before)!=0 || !S_ISREG(before.st_mode) || before.st_uid!=::geteuid()
        || before.st_nlink!=1 || (before.st_mode&07777)!=0600 || before.st_size<=0 || before.st_size>maximum) return {};
    const auto bytes=file.read(maximum+1);
    if (::fstat(fd,&after)!=0 || bytes.size()!=before.st_size || before.st_size!=after.st_size
        || before.st_mtim.tv_sec!=after.st_mtim.tv_sec || before.st_mtim.tv_nsec!=after.st_mtim.tv_nsec
        || before.st_ctim.tv_sec!=after.st_ctim.tv_sec || before.st_ctim.tv_nsec!=after.st_ctim.tv_nsec) return {};
    struct stat named{};
    if (::lstat(QFile::encodeName(path).constData(),&named)!=0 || !S_ISREG(named.st_mode)
        || named.st_dev!=before.st_dev || named.st_ino!=before.st_ino) return {};
    return bytes;
#endif
}
}
#endif
