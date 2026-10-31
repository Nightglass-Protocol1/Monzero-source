#include "NativeQuoteConfig.h"
#include "BackendExecutable.h"
#include <QJsonDocument>
#include <QRegularExpression>
#include <limits>

bool NativeQuoteConfig::load(const QString &path, qint64 now, NativeQuoteConfig &output) {
    output = {};
#ifndef Q_OS_LINUX
    Q_UNUSED(path) Q_UNUSED(now)
    return false;
#else
    const QFileInfo info(path);
    if (!info.isAbsolute() || info.canonicalFilePath() != path) return false;
    const int fd = ::open(QFile::encodeName(path).constData(), O_RDONLY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC);
    if (fd < 0) return false;
    QFile file;
    if (!file.open(fd, QIODevice::ReadOnly, QFileDevice::AutoCloseHandle)) { ::close(fd); return false; }
    struct stat before{}, after{};
    if (::fstat(fd, &before) != 0 || !S_ISREG(before.st_mode) || before.st_uid != ::geteuid()
        || before.st_nlink != 1 || (before.st_mode & 07777) != 0600
        || before.st_size <= 0 || before.st_size > 16384) return false;
    const auto bytes = file.read(16385);
    if (::fstat(fd, &after) != 0 || bytes.size() != before.st_size || before.st_size != after.st_size
        || before.st_mtim.tv_sec != after.st_mtim.tv_sec || before.st_mtim.tv_nsec != after.st_mtim.tv_nsec
        || before.st_ctim.tv_sec != after.st_ctim.tv_sec || before.st_ctim.tv_nsec != after.st_ctim.tv_nsec) return false;
    const auto doc = QJsonDocument::fromJson(bytes);
    if (!doc.isObject() || doc.toJson(QJsonDocument::Compact) != bytes) return false;
    const auto object = doc.object();
    if (object.size() != 10 || object.value("schema") != "monzero-private-native-quote-v1"
        || !object.value("policy").isObject() || !object.value("terms").isObject()) return false;
    for (const auto key : {"helper","helperDigest","directory","btcPeer","xmzPeer","btcSignature","xmzSignature"})
        if (!object.value(key).isString()) return false;
    const QRegularExpression keyPattern("^[0-9a-f]{64}\\z"), sigPattern("^[0-9a-f]{128}\\z");
    for (const auto key : {"btcPeer","xmzPeer"})
        if (!keyPattern.match(object.value(key).toString()).hasMatch()) return false;
    for (const auto key : {"btcSignature","xmzSignature"})
        if (!sigPattern.match(object.value(key).toString()).hasMatch()) return false;
    NativeQuoteConfig config;
    config.helper = object.value("helper").toString();
    config.helperDigest = object.value("helperDigest").toString();
    config.directory = object.value("directory").toString();
    config.btcPeer = QByteArray::fromHex(object.value("btcPeer").toString().toLatin1());
    config.xmzPeer = QByteArray::fromHex(object.value("xmzPeer").toString().toLatin1());
    config.btcSignature = QByteArray::fromHex(object.value("btcSignature").toString().toLatin1());
    config.xmzSignature = QByteArray::fromHex(object.value("xmzSignature").toString().toLatin1());
    config.payload = QJsonDocument(object.value("terms").toObject()).toJson(QJsonDocument::Compact);
    const auto policy = object.value("policy").toObject();
    if (policy.size() != 12) return false;
    for (auto it = policy.begin(); it != policy.end(); ++it) if (!it.value().isString()) return false;
    config.policy.btcGenesis = policy.value("btcGenesis").toString();
    config.policy.xmzGenesis = policy.value("xmzGenesis").toString();
    config.policy.btcNetwork = policy.value("btcNetwork").toString();
    config.policy.xmzNetwork = policy.value("xmzNetwork").toString();
    quint64 values[8]; int index = 0;
    for (const auto key : {"btcConfirmations","xmzConfirmations","cancelBlocks","punishBlocks",
         "maximumBtcPrincipal","maximumXmzPrincipal","maximumBtcNativeFee","maximumXmzNativeFee"}) {
        const auto text = policy.value(key).toString();
        bool ok = false;
        values[index] = text.toULongLong(&ok);
        if (!ok || values[index] == 0 || QString::number(values[index]) != text
            || (index < 4 && values[index] > std::numeric_limits<quint32>::max())) return false;
        ++index;
    }
    config.policy.btcConfirmations = values[0]; config.policy.xmzConfirmations = values[1];
    config.policy.cancelBlocks = values[2]; config.policy.punishBlocks = values[3];
    config.policy.maximumBtcPrincipal = values[4]; config.policy.maximumXmzPrincipal = values[5];
    config.policy.maximumBtcNativeFee = values[6]; config.policy.maximumXmzNativeFee = values[7];
    const QFileInfo root(config.directory);
    if (!root.isAbsolute() || root.canonicalFilePath() != config.directory
        || ::lstat(QFile::encodeName(config.directory).constData(), &after) != 0
        || !S_ISDIR(after.st_mode) || after.st_uid != ::geteuid() || (after.st_mode & 0077) != 0) return false;
    QJsonObject verified;
    if (!BackendExecutable::matches(config.helper, config.helperDigest)
        || !NativeTradeQuote::verifyForDeployment(config.payload, now, config.btcSignature,
            config.xmzSignature, config.btcPeer, config.xmzPeer, config.policy, verified)) return false;
    output = config;
    return true;
#endif
}
