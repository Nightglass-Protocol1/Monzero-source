#include "RelayOutboxStore.h"
#include "RelayReceipt.h"
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>
#ifdef Q_OS_UNIX
#include <unistd.h>
#endif

namespace {
constexpr int maximumRecords = 1024;
constexpr int maximumBytes = 8 * 1024 * 1024;
}

RelayOutboxStore::RelayOutboxStore(QString path, QByteArray sender,
    const RelayIngress::Coordinator &codec, Mode mode)
    : m_path(std::move(path)), m_sender(std::move(sender)), m_codec(codec)
{
    m_error = "Cannot open relay outbox";
    const QFileInfo target(m_path), parent(target.absolutePath());
    const auto shared = QFileDevice::ReadGroup | QFileDevice::WriteGroup | QFileDevice::ExeGroup
        | QFileDevice::ReadOther | QFileDevice::WriteOther | QFileDevice::ExeOther;
    if (m_path.isEmpty() || m_sender.size() != 32 || m_sender == QByteArray(32, '\0')
        || !parent.isDir() || parent.isSymLink() || (parent.permissions() & shared)) return;
#ifdef Q_OS_UNIX
    if (parent.ownerId() != geteuid()) return;
#endif
    m_path = target.absoluteFilePath();
    m_lock.reset(new QLockFile(m_path + ".lock"));
    m_lock->setStaleLockTime(0);
    if (!m_lock->tryLock(0)) return;
    const QFileInfo current(m_path);
    if (current.isSymLink()) return;
    if (mode == Mode::CreateNew) {
        if (current.exists() || !save({})) return;
        m_error.clear();
        return;
    }
    if (mode != Mode::OpenExisting || !current.isFile() || (current.permissions() & shared)) return;
    QFile file(m_path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > maximumBytes) return;
    const auto bytes = file.read(maximumBytes + 1);
    if (bytes.size() > maximumBytes || !file.atEnd()) return;
    QJsonParseError parse;
    const auto document = QJsonDocument::fromJson(bytes, &parse);
    const auto root = document.object();
    if (parse.error != QJsonParseError::NoError || !document.isObject() || root.size() != 3
        || (root.value("version").toInt(-1) != 1 && root.value("version").toInt(-1) != 2)
        || root.value("sender").toString().toLatin1() != m_sender.toHex()
        || !root.value("messages").isArray()) return;
    const auto records = root.value("messages").toArray();
    if (records.size() > maximumRecords) return;
    for (int i = 0; i < records.size(); ++i) {
        const auto record = records.at(i).toObject();
        bool ok = false;
        const auto text = record.value("createdAt").toString();
        const auto createdAt = text.toLongLong(&ok);
        const auto encoded = record.value("message").toString().toLatin1();
        const auto message = QByteArray::fromBase64(encoded);
        const bool hasReceipts = record.contains("receipts");
        if (record.size() != (hasReceipts ? 3 : 2)
            || (hasReceipts && (root.value("version").toInt() != 2 || !record.value("receipts").isObject()))
            || !ok || text != QString::number(createdAt)
            || message.toBase64() != encoded) return;
        const auto envelope = m_codec.inspectMessage(message, createdAt);
        if (!envelope.accepted() || envelope.senderPublicKey() != m_sender
            || envelope.terms().sequence != quint64(i + 1)
            || envelope.terms().createdAt != createdAt) return;
        const auto receipts = record.value("receipts").toObject();
        if (receipts.size() > 16) return;
        for (auto it = receipts.begin(); it != receipts.end(); ++it) {
            const auto receiverHex = it.key().toLatin1();
            const auto receiver = QByteArray::fromHex(receiverHex);
            const auto encodedReceipt = it.value().toString().toLatin1();
            const auto receipt = QByteArray::fromBase64(encodedReceipt);
            if (receiver.toHex() != receiverHex || receipt.toBase64() != encodedReceipt
                || !RelayReceipt::verify(receipt, envelope, receiver,
                                         QDateTime::currentSecsSinceEpoch())) return;
        }
    }
    m_records = records;
    m_error.clear();
}

bool RelayOutboxStore::save(const QJsonArray &records)
{
    const auto bytes = QJsonDocument(QJsonObject{{"version", 2},
        {"sender", QString::fromLatin1(m_sender.toHex())}, {"messages", records}})
        .toJson(QJsonDocument::Compact);
    if (bytes.size() > maximumBytes) return false;
    QSaveFile file(m_path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly)
        || !file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner)
        || file.write(bytes) != bytes.size() || !file.commit()) {
        m_error = "Cannot persist relay outbox";
        return false;
    }
    m_records = records;
    return true;
}

quint64 RelayOutboxStore::nextSequence() const
{
    return ready() && m_records.size() < maximumRecords ? quint64(m_records.size() + 1) : 0;
}

QByteArray RelayOutboxStore::message(quint64 sequence) const
{
    if (!ready() || sequence == 0 || sequence > quint64(m_records.size())) return {};
    return QByteArray::fromBase64(m_records.at(int(sequence - 1)).toObject()
        .value("message").toString().toLatin1());
}

bool RelayOutboxStore::append(const QByteArray &bytes, qint64 now)
{
    if (!ready()) return false;
    const auto envelope = m_codec.inspectMessage(bytes, now);
    if (!envelope.accepted() || envelope.senderPublicKey() != m_sender) return false;
    if (!m_codec.inspectMessage(bytes, envelope.terms().createdAt).accepted()) return false;
    const auto sequence = envelope.terms().sequence;
    if (sequence <= quint64(m_records.size())) return message(sequence) == bytes;
    if (sequence != nextSequence()) return false;
    auto next = m_records;
    next.append(QJsonObject{{"createdAt", QString::number(envelope.terms().createdAt)},
        {"message", QString::fromLatin1(bytes.toBase64())}});
    return save(next);
}

RelayEnvelope::Evidence RelayOutboxStore::historicalEnvelope(quint64 sequence) const
{
    const auto bytes = message(sequence);
    if (bytes.isEmpty()) return {};
    const auto createdAt = m_records.at(int(sequence - 1)).toObject()
        .value("createdAt").toString().toLongLong();
    return m_codec.inspectMessage(bytes, createdAt);
}

bool RelayOutboxStore::acknowledge(quint64 sequence, const QByteArray &receiver,
                                  const QByteArray &receipt, qint64 now)
{
    if (!RelayReceipt::verify(receipt, historicalEnvelope(sequence), receiver, now)) return false;
    auto next = m_records;
    auto record = next.at(int(sequence - 1)).toObject();
    auto receipts = record.value("receipts").toObject();
    const auto key = QString::fromLatin1(receiver.toHex());
    if (receipts.contains(key)) return acknowledged(sequence, receiver, now);
    if (receipts.size() >= 16) return false;
    receipts.insert(key, QString::fromLatin1(receipt.toBase64()));
    record.insert("receipts", receipts);
    next[int(sequence - 1)] = record;
    return save(next);
}

bool RelayOutboxStore::acknowledged(quint64 sequence, const QByteArray &receiver, qint64 now) const
{
    const auto envelope = historicalEnvelope(sequence);
    if (!envelope.accepted()) return false;
    const auto encoded = m_records.at(int(sequence - 1)).toObject().value("receipts")
        .toObject().value(QString::fromLatin1(receiver.toHex())).toString().toLatin1();
    return RelayReceipt::verify(QByteArray::fromBase64(encoded), envelope, receiver, now);
}
