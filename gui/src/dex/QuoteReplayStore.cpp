#include "QuoteReplayStore.h"
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#ifdef Q_OS_UNIX
#include <unistd.h>
#endif

namespace {
const qint64 maximumQuoteLifetime = 7 * 24 * 60 * 60;
const int maximumRecords = 10000;
const QRegularExpression hex64("^[0-9a-f]{64}$");
}

QuoteReplayStore::QuoteReplayStore(QString path) : m_path(std::move(path))
{
    const QFileInfo target(m_path), parent(target.absolutePath());
    const auto shared = QFileDevice::ReadGroup | QFileDevice::WriteGroup | QFileDevice::ExeGroup
        | QFileDevice::ReadOther | QFileDevice::WriteOther | QFileDevice::ExeOther;
    if (m_path.isEmpty() || !parent.isDir() || parent.isSymLink() || (parent.permissions() & shared)
        || target.isSymLink() || (target.exists() && (!target.isFile() || (target.permissions() & shared)))) {
        m_error = "Quote replay store requires private regular storage";
        return;
    }
#ifdef Q_OS_UNIX
    if (parent.ownerId() != geteuid() || (target.exists() && target.ownerId() != geteuid())) {
        m_error = "Quote replay store must belong to this user";
        return;
    }
#endif
    m_path = target.absoluteFilePath();
    m_lock.reset(new QLockFile(m_path + ".lock"));
    m_lock->setStaleLockTime(0);
    if (!m_lock->tryLock(0)) m_error = "Quote replay store is already locked";
    else load();
}

bool QuoteReplayStore::ready() const { return m_error.isEmpty(); }
QString QuoteReplayStore::error() const { return m_error; }
QJsonArray QuoteReplayStore::records() const { return m_records; }

QString QuoteReplayStore::replayId(const QByteArray &makerKey, const QByteArray &nonce)
{
    if (makerKey.size() != 32 || nonce.size() != 32) return {};
    const QByteArray input = QByteArray("MONZERO-DEX-QUOTE-NONCE-V1\n") + makerKey + nonce;
    return QString::fromLatin1(QCryptographicHash::hash(input, QCryptographicHash::Sha256).toHex());
}

bool QuoteReplayStore::load()
{
    QFile file(m_path);
    if (!file.exists()) return true;
    if (!file.open(QIODevice::ReadOnly) || file.size() > 4 * 1024 * 1024) {
        m_error = "Cannot read quote replay store"; return false;
    }
    QJsonParseError parse;
    const auto bytes = file.read(4 * 1024 * 1024 + 1);
    if (bytes.size() > 4 * 1024 * 1024 || !file.atEnd()) {
        m_error = "Quote replay store exceeds byte limit"; return false;
    }
    const auto document = QJsonDocument::fromJson(bytes, &parse);
    if (parse.error != QJsonParseError::NoError || !document.isObject()
        || (document.object().value("version").toInt(-1) != 1
            && document.object().value("version").toInt(-1) != 2)
        || !document.object().value("records").isArray()) {
        m_error = "Invalid quote replay store"; return false;
    }
    const QJsonArray records = document.object().value("records").toArray();
    if (records.size() > maximumRecords) {
        m_error = "Quote replay store exceeds record limit"; return false;
    }
    QSet<QString> ids;
    for (const auto value : records) {
        if (!value.isObject()) { m_error = "Invalid quote replay record"; return false; }
        const auto record = value.toObject();
        const QString id = record.value("id").toString();
        const QString commitment = record.value("commitment").toString();
        const QString expiryText = record.value("quoteExpiry").toString();
        const QString acceptedText = record.value("acceptedAt").toString();
        bool expiryOk = false, acceptedOk = false;
        const qint64 expiry = expiryText.toLongLong(&expiryOk);
        const qint64 accepted = acceptedText.toLongLong(&acceptedOk);
        const bool hasArtifact = record.contains("signedArtifact");
        const auto encoded = record.value("signedArtifact").toString().toLatin1();
        const auto artifact = QByteArray::fromBase64(encoded);
        if (record.size() != (hasArtifact ? 5 : 4)
            || (hasArtifact && (document.object().value("version").toInt() != 2
                || artifact.isEmpty() || artifact.size() > 98304 || artifact.toBase64() != encoded))
            || !hex64.match(id).hasMatch() || ids.contains(id)
            || !hex64.match(commitment).hasMatch() || !expiryOk || !acceptedOk
            || accepted < 0 || expiry <= accepted || expiry - accepted > maximumQuoteLifetime
            || expiryText != QString::number(expiry) || acceptedText != QString::number(accepted)) {
            m_error = "Invalid quote replay record"; return false;
        }
        ids.insert(id);
    }
    m_records = records;
    return true;
}

bool QuoteReplayStore::save(const QJsonArray &next)
{
    QSaveFile file(m_path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly)) { m_error = "Cannot write quote replay store"; return false; }
    if (!file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner)) {
        m_error = "Cannot secure quote replay permissions"; return false;
    }
    const QByteArray bytes = QJsonDocument(QJsonObject{{"version", 2}, {"records", next}})
        .toJson(QJsonDocument::Compact);
    if (bytes.size() > 4 * 1024 * 1024 || file.write(bytes) != bytes.size() || !file.commit()) {
        m_error = "Cannot commit quote replay store"; return false;
    }
    m_records = next;
    return true;
}

bool QuoteReplayStore::accept(const QByteArray &makerKey, const QByteArray &nonce,
                              const QByteArray &commitment, qint64 quoteExpiry, qint64 now,
                              const QByteArray &signedArtifact)
{
    const QString id = replayId(makerKey, nonce);
    const QString commitmentHex = QString::fromLatin1(commitment.toHex());
    if (!ready() || id.isEmpty() || commitment.size() != 32 || now < 0 || signedArtifact.size() > 98304
        || quoteExpiry <= now || quoteExpiry - now > maximumQuoteLifetime) return false;
    for (int i = 0; i < m_records.size(); ++i) {
        auto record = m_records.at(i).toObject();
        if (record.value("id") != id) continue;
        if (record.value("commitment") != commitmentHex
            || record.value("quoteExpiry") != QString::number(quoteExpiry)) return false;
        if (signedArtifact.isEmpty()) return true;
        const auto encoded = QString::fromLatin1(signedArtifact.toBase64());
        if (record.contains("signedArtifact")) return record.value("signedArtifact") == encoded;
        auto next = m_records;
        record["signedArtifact"] = encoded;
        next[i] = record;
        return save(next);
    }
    if (m_records.size() >= maximumRecords) return false;
    QJsonArray next = m_records;
    next.append(QJsonObject{{"id", id}, {"commitment", commitmentHex},
                            {"quoteExpiry", QString::number(quoteExpiry)},
                            {"acceptedAt", QString::number(now)}});
    if (!signedArtifact.isEmpty()) {
        auto record = next.last().toObject();
        record["signedArtifact"] = QString::fromLatin1(signedArtifact.toBase64());
        next[next.size() - 1] = record;
    }
    return save(next);
}

bool QuoteReplayStore::contains(const QByteArray &makerKey, const QByteArray &nonce) const
{
    const QString id = replayId(makerKey, nonce);
    if (!ready() || id.isEmpty()) return false;
    for (const auto value : m_records)
        if (value.toObject().value("id") == id) return true;
    return false;
}
