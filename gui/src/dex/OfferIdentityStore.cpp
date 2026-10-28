#include "OfferIdentityStore.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#ifdef Q_OS_UNIX
#include <unistd.h>
#endif

namespace {
bool privateOwned(const QFileInfo &info)
{
    const auto shared = QFileDevice::ReadGroup | QFileDevice::WriteGroup
        | QFileDevice::ExeGroup | QFileDevice::ReadOther | QFileDevice::WriteOther
        | QFileDevice::ExeOther;
    if (info.isSymLink() || (info.permissions() & shared)) return false;
#ifdef Q_OS_UNIX
    if (info.ownerId() != geteuid()) return false;
#endif
    return true;
}
}

OfferIdentityStore::OfferIdentityStore(const QString &path, const QByteArray &passphrase,
                                     Mode mode)
{
    m_error = "Cannot open encrypted DEX identity";
    if (path.isEmpty() || passphrase.isEmpty() || passphrase.size() > 1024) return;
    const QFileInfo target(path);
    const QFileInfo parent(target.absolutePath());
    if (!parent.isDir() || !privateOwned(parent) || target.isSymLink()) return;
    m_lock.reset(new QLockFile(target.absoluteFilePath() + ".lock"));
    m_lock->setStaleLockTime(0);
    if (!m_lock->tryLock(0)) { m_error = "DEX identity is already locked"; return; }
    // Refresh metadata after taking the lock. Never replace or repair an identity.
    const QFileInfo current(target.absoluteFilePath());
    if (current.isSymLink()) return;
    if (mode == Mode::CreateNew) {
        if (current.exists()) { m_error = "DEX identity already exists"; return; }
        auto candidate = std::unique_ptr<OfferIdentity>(new OfferIdentity);
        const auto encrypted = candidate->encrypt(passphrase);
        if (encrypted.isEmpty()) return;
        QSaveFile file(current.absoluteFilePath());
        file.setDirectWriteFallback(false);
        if (!file.open(QIODevice::WriteOnly)
            || !file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner)
            || file.write(encrypted) != encrypted.size() || !file.commit()) return;
        m_identity = std::move(candidate);
    } else {
        if (!current.isFile() || !privateOwned(current) || current.size() > 512) return;
        QFile file(current.absoluteFilePath());
        if (!file.open(QIODevice::ReadOnly)) return;
        const auto bytes = file.read(513);
        if (!file.atEnd() || bytes.size() > 512) return;
        m_identity = OfferIdentity::decrypt(bytes, passphrase);
        if (!m_identity) return;
    }
    m_error.clear();
}
