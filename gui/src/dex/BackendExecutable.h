#ifndef MONZERO_BACKEND_EXECUTABLE_H
#define MONZERO_BACKEND_EXECUTABLE_H
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QString>
#ifdef Q_OS_LINUX
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace BackendExecutable {
// Prelaunch content check, NOT signing/provenance verification or a race-free
// execution handle. Expected digest must be trusted separately. Caller must keep
// the installation directory protected through execution and verify dependencies.
inline bool matches(const QString &path,const QString &expectedSha256) {
#ifndef Q_OS_LINUX
    return false;
#else
    if (expectedSha256.size()!=64) return false;
    for (auto c : expectedSha256) if (!QStringLiteral("0123456789abcdef").contains(c)) return false;
    const QFileInfo entry(path);
    if (!entry.isAbsolute() || entry.canonicalFilePath()!=path) return false;
    const int fd=::open(QFile::encodeName(path).constData(),O_RDONLY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC);
    if (fd<0) return false;
    QFile file;
    if (!file.open(fd,QIODevice::ReadOnly,QFileDevice::AutoCloseHandle)) { ::close(fd); return false; }
    struct stat before{}, after{}, named{};
    constexpr qint64 maximum=512LL*1024*1024;
    if (::fstat(fd,&before)!=0 || !S_ISREG(before.st_mode) || before.st_nlink!=1
        || (before.st_uid!=0 && before.st_uid!=::geteuid())
        || (before.st_mode & (S_IWGRP|S_IWOTH|S_ISUID|S_ISGID))
        || !(before.st_mode&S_IRUSR) || !(before.st_mode&S_IXUSR)
        || before.st_size<=0 || before.st_size>maximum) return false;
    QCryptographicHash digest(QCryptographicHash::Sha256);
    qint64 remaining=before.st_size;
    while (remaining>0) {
        const auto bytes=file.read(qMin<qint64>(remaining,64*1024));
        if (bytes.isEmpty()) return false;
        digest.addData(bytes);
        remaining-=bytes.size();
    }
    if (!file.read(1).isEmpty() || file.error()!=QFileDevice::NoError
        || ::fstat(fd,&after)!=0 || ::lstat(QFile::encodeName(path).constData(),&named)!=0) return false;
    const auto same=[](const struct stat &a,const struct stat &b) {
        return a.st_dev==b.st_dev && a.st_ino==b.st_ino && a.st_size==b.st_size
            && a.st_mode==b.st_mode && a.st_uid==b.st_uid && a.st_gid==b.st_gid && a.st_nlink==b.st_nlink
            && a.st_mtim.tv_sec==b.st_mtim.tv_sec && a.st_mtim.tv_nsec==b.st_mtim.tv_nsec
            && a.st_ctim.tv_sec==b.st_ctim.tv_sec && a.st_ctim.tv_nsec==b.st_ctim.tv_nsec;
    };
    return same(before,after) && same(after,named)
        && QString::fromLatin1(digest.result().toHex())==expectedSha256;
#endif
}
}
#endif
