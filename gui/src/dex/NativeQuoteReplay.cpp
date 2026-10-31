#include "NativeQuoteReplay.h"
#include "NativeTradeQuote.h"
#include <QFileInfo>
#include <QFile>
#include <QJsonDocument>
#include <QUuid>
#include <QRegularExpression>
#ifdef Q_OS_LINUX
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cerrno>
#endif

bool NativeQuoteReplay::recover(const QString &directory, const QString &quoteId, qint64 now,
    const QByteArray &expectedBtcFunder, const QByteArray &expectedXmzFunder,
    QJsonObject &output)
{
#ifdef Q_OS_LINUX
    const QUuid id(quoteId);
    const QFileInfo info(directory);
    if (now < 0 || id.isNull() || id.toString(QUuid::WithoutBraces) != quoteId
        || !info.isAbsolute() || info.canonicalFilePath() != directory) return false;
    struct Fd { int value; ~Fd() { if (value >= 0) ::close(value); } };
    Fd root{::open(QFile::encodeName(directory).constData(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC)};
    struct stat meta{};
    if (root.value < 0 || ::fstat(root.value, &meta) != 0 || meta.st_uid != ::geteuid()
        || (meta.st_mode & 0077) != 0 || ::flock(root.value, LOCK_SH | LOCK_NB) != 0) return false;
    const auto name = ("quote-" + quoteId + ".json").toLatin1();
    Fd record{::openat(root.value, name.constData(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC)};
    if (record.value < 0 || ::fstat(record.value, &meta) != 0 || !S_ISREG(meta.st_mode)
        || meta.st_uid != ::geteuid() || (meta.st_mode & 0077) != 0 || meta.st_nlink != 2
        || meta.st_size <= 0 || meta.st_size > 16384) return false;
    QByteArray bytes;
    char chunk[4096];
    for (;;) {
        const auto count = ::read(record.value, chunk, sizeof(chunk));
        if (count < 0 && errno == EINTR) continue;
        if (count < 0) return false;
        if (count == 0) break;
        bytes.append(chunk, static_cast<int>(count));
        if (bytes.size() > 16384) return false;
    }
    if (bytes.size() != meta.st_size) return false;
    const auto doc = QJsonDocument::fromJson(bytes);
    if (!doc.isObject() || doc.toJson(QJsonDocument::Compact) != bytes) return false;
    const auto saved = doc.object();
    if (saved.size() != 5 || saved.value("schema") != "monzero-native-quote-reservation-v1"
        || !saved.value("terms").isObject() || !saved.value("acceptedAt").isString()
        || !saved.value("btcSignature").isString() || !saved.value("xmzSignature").isString()) return false;
    const auto time = saved.value("acceptedAt").toString();
    bool ok = false;
    const qint64 acceptedAt = time.toLongLong(&ok);
    if (!ok || acceptedAt < 0 || acceptedAt > now || QString::number(acceptedAt) != time) return false;
    const QRegularExpression signature("^[0-9a-f]{128}\\z");
    const auto btc = saved.value("btcSignature").toString();
    const auto xmz = saved.value("xmzSignature").toString();
    if (!signature.match(btc).hasMatch() || !signature.match(xmz).hasMatch()) return false;
    QJsonObject verified;
    const auto terms = saved.value("terms").toObject();
    if (!NativeTradeQuote::verifySignatures(QJsonDocument(terms).toJson(QJsonDocument::Compact), acceptedAt,
        QByteArray::fromHex(btc.toLatin1()), QByteArray::fromHex(xmz.toLatin1()),
        expectedBtcFunder, expectedXmzFunder, verified) || verified.value("quoteId") != quoteId) return false;
    const auto swapName = ("swap-" + verified.value("swapId").toString() + ".json").toLatin1();
    struct stat swapMeta{};
    if (::fstatat(root.value, swapName.constData(), &swapMeta, AT_SYMLINK_NOFOLLOW) != 0
        || !S_ISREG(swapMeta.st_mode) || swapMeta.st_dev != meta.st_dev || swapMeta.st_ino != meta.st_ino) return false;
    output = saved;
    return true;
#else
    Q_UNUSED(directory) Q_UNUSED(quoteId) Q_UNUSED(now)
    Q_UNUSED(expectedBtcFunder) Q_UNUSED(expectedXmzFunder) Q_UNUSED(output)
    return false;
#endif
}

bool NativeQuoteReplay::reserve(const QString &directory, const QByteArray &payload, qint64 now,
    const QByteArray &btcSignature, const QByteArray &xmzSignature,
    const QByteArray &expectedBtcFunder, const QByteArray &expectedXmzFunder)
{
    QJsonObject terms;
    if (!NativeTradeQuote::verifySignatures(payload, now, btcSignature, xmzSignature,
        expectedBtcFunder, expectedXmzFunder, terms)) return false;
#ifdef Q_OS_LINUX
    const QFileInfo info(directory);
    if (!info.isAbsolute() || info.canonicalFilePath() != directory) return false;
    struct Fd { int value; ~Fd() { if (value >= 0) ::close(value); } };
    Fd root{::open(QFile::encodeName(directory).constData(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC)};
    struct stat metadata{};
    if (root.value < 0 || ::fstat(root.value, &metadata) != 0
        || metadata.st_uid != ::geteuid() || (metadata.st_mode & 0077) != 0
        || ::flock(root.value, LOCK_EX | LOCK_NB) != 0) return false;
    const auto quote = ("quote-" + terms.value("quoteId").toString() + ".json").toLatin1();
    const auto swap = ("swap-" + terms.value("swapId").toString() + ".json").toLatin1();
    // Reject every existing path, including malformed files and symlinks.
    for (const auto &name : {quote, swap}) {
        if (::fstatat(root.value, name.constData(), &metadata, AT_SYMLINK_NOFOLLOW) == 0
            || errno != ENOENT) return false;
    }
    const QByteArray bytes = QJsonDocument(QJsonObject{
        {"schema", "monzero-native-quote-reservation-v1"},
        {"acceptedAt", QString::number(now)}, {"terms", terms},
        {"btcSignature", QString::fromLatin1(btcSignature.toHex())},
        {"xmzSignature", QString::fromLatin1(xmzSignature.toHex())}
    }).toJson(QJsonDocument::Compact);
    Fd record{::openat(root.value, quote.constData(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600)};
    if (record.value < 0) return false;
    qint64 offset = 0;
    while (offset < bytes.size()) {
        const auto count = ::write(record.value, bytes.constData() + offset, bytes.size() - offset);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) return false;
        offset += count;
    }
    // Both names refer to one immutable signed artifact. A failed/partial
    // operation is never reported as accepted and never rolled back here.
    if (::fsync(record.value) != 0
        || ::linkat(root.value, quote.constData(), root.value, swap.constData(), 0) != 0
        || ::fsync(root.value) != 0) return false;
    return true;
#else
    Q_UNUSED(directory)
    return false;
#endif
}
