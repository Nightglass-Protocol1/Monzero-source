#ifndef MONZERO_BACKEND_EXECUTABLE_H
#define MONZERO_BACKEND_EXECUTABLE_H
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QString>
#ifdef Q_OS_LINUX
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#elif defined(Q_OS_WIN)
// Keep windows.h from pulling in the old WinSock.h: Qt/boost::asio include winsock2 and the two clash
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace BackendExecutable {
// Prelaunch content check, NOT signing/provenance verification or a race-free
// execution handle. Expected digest must be trusted separately. Caller must keep
// the installation directory protected through execution and verify dependencies.
inline bool validDigest(const QString &expectedSha256) {
    if (expectedSha256.size()!=64) return false;
    for (auto c : expectedSha256) if (!QStringLiteral("0123456789abcdef").contains(c)) return false;
    return true;
}
inline bool matches(const QString &path,const QString &expectedSha256) {
#if defined(Q_OS_WIN)
    // Windows has no O_NOFOLLOW/st_nlink/uid; the same intent is kept with Win32: reject a reparse
    // point (symlink/junction), open denying writers for the whole check, and require the file's
    // identity (volume + index + size + write time, one hard link) to be unchanged before and after
    // hashing. The install directory is user-owned app data, as on Linux.
    if (!validDigest(expectedSha256)) return false;
    const QFileInfo entry(path);
    if (!entry.isAbsolute() || entry.isSymLink() || entry.canonicalFilePath()!=path) return false;
    const auto wide=QDir::toNativeSeparators(path).toStdWString();
    const DWORD attrs=::GetFileAttributesW(wide.c_str());
    if (attrs==INVALID_FILE_ATTRIBUTES || (attrs&FILE_ATTRIBUTE_DIRECTORY) || (attrs&FILE_ATTRIBUTE_REPARSE_POINT)) return false;
    const HANDLE h=::CreateFileW(wide.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    if (h==INVALID_HANDLE_VALUE) return false;
    struct Closer { HANDLE h; ~Closer(){ if (h!=INVALID_HANDLE_VALUE) ::CloseHandle(h); } } closer{h};
    BY_HANDLE_FILE_INFORMATION before{}, after{};
    if (!::GetFileInformationByHandle(h,&before)) return false;
    const quint64 size=(quint64(before.nFileSizeHigh)<<32)|before.nFileSizeLow;
    constexpr quint64 maximum=512ULL*1024*1024;
    if ((before.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT) || (before.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)
        || before.nNumberOfLinks!=1 || size==0 || size>maximum) return false;
    QCryptographicHash digest(QCryptographicHash::Sha256);
    quint64 remaining=size;
    char buffer[64*1024];
    while (remaining>0) {
        DWORD want=DWORD(qMin<quint64>(remaining,sizeof(buffer))), got=0;
        if (!::ReadFile(h,buffer,want,&got,nullptr) || got==0) return false;
        digest.addData(buffer,int(got));
        remaining-=got;
    }
    // Nothing must remain: the file is exactly its stated size
    DWORD trailing=0;
    const bool atEnd=::ReadFile(h,buffer,1,&trailing,nullptr) && trailing==0;
    // Re-open to confirm the file was not swapped under us during hashing
    const HANDLE h2=::CreateFileW(wide.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    if (h2==INVALID_HANDLE_VALUE) return false;
    struct Closer2 { HANDLE h; ~Closer2(){ ::CloseHandle(h); } } closer2{h2};
    if (!::GetFileInformationByHandle(h2,&after)) return false;
    const auto same=[](const BY_HANDLE_FILE_INFORMATION &a,const BY_HANDLE_FILE_INFORMATION &b){
        return a.dwVolumeSerialNumber==b.dwVolumeSerialNumber && a.nFileIndexHigh==b.nFileIndexHigh
            && a.nFileIndexLow==b.nFileIndexLow && a.nFileSizeHigh==b.nFileSizeHigh && a.nFileSizeLow==b.nFileSizeLow
            && a.nNumberOfLinks==b.nNumberOfLinks && a.dwFileAttributes==b.dwFileAttributes
            && a.ftLastWriteTime.dwLowDateTime==b.ftLastWriteTime.dwLowDateTime
            && a.ftLastWriteTime.dwHighDateTime==b.ftLastWriteTime.dwHighDateTime
            && a.ftCreationTime.dwLowDateTime==b.ftCreationTime.dwLowDateTime
            && a.ftCreationTime.dwHighDateTime==b.ftCreationTime.dwHighDateTime;
    };
    return atEnd && same(before,after)
        && QString::fromLatin1(digest.result().toHex())==expectedSha256;
#elif defined(Q_OS_LINUX)
    if (!validDigest(expectedSha256)) return false;
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
#else
    Q_UNUSED(path); Q_UNUSED(expectedSha256);
    return false;   // other platforms do not run bundled helpers
#endif
}
}
#endif
