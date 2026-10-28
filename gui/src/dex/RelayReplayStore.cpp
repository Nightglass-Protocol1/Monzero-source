#include "RelayReplayStore.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <limits>

namespace {
constexpr int maximumRecords = 10000;
constexpr qint64 maximumFileSize = 4 * 1024 * 1024;
const QRegularExpression hex32("^[0-9a-f]{64}$");
const QRegularExpression unsignedInteger("^[1-9][0-9]{0,19}$");

bool canonicalSequence(const QJsonValue &value, quint64 &result)
{
    if (!value.isString() || !unsignedInteger.match(value.toString()).hasMatch())
        return false;
    bool ok = false;
    result = value.toString().toULongLong(&ok);
    return ok && result != 0 && value.toString() == QString::number(result);
}

QJsonObject envelopeFields(const RelayEnvelope::Evidence &evidence)
{
    const auto terms = evidence.terms();
    return {{"type", terms.type}, {"createdAt", QString::number(terms.createdAt)},
        {"expiresAt", QString::number(terms.expiresAt)},
        {"artifact", QString::fromLatin1(terms.artifact.toHex())},
        {"signature", QString::fromLatin1(evidence.signature().toHex())}};
}
}

RelayReplayStore::RelayReplayStore(QString path, QString expectedProtocol,
                                   QString expectedNetwork)
    : m_path(std::move(path)), m_expectedProtocol(std::move(expectedProtocol)),
      m_expectedNetwork(std::move(expectedNetwork))
{
    m_lock.reset(new QLockFile(m_path + ".lock"));
    m_lock->setStaleLockTime(0);
    if (!m_lock->tryLock(0))
        m_error = "Relay replay store is already locked";
    else
        load();
}

bool RelayReplayStore::ready() const { return m_error.isEmpty(); }
QString RelayReplayStore::error() const { return m_error; }
QJsonArray RelayReplayStore::records() const { return m_records; }

RelayEnvelope::Evidence RelayReplayStore::decodeEnvelope(const QJsonObject &record) const
{
    const auto object = record.value("envelope").toObject();
    if (object.size() != 5) return {};
    RelayEnvelope::Terms terms;
    terms.protocol = m_expectedProtocol;
    terms.network = m_expectedNetwork;
    terms.type = object.value("type").toString();
    bool createdOk = false, expiryOk = false;
    terms.createdAt = object.value("createdAt").toString().toLongLong(&createdOk);
    terms.expiresAt = object.value("expiresAt").toString().toLongLong(&expiryOk);
    if (!createdOk || !expiryOk
        || !canonicalSequence(record.value("sequence"), terms.sequence)) return {};
    terms.artifact = QByteArray::fromHex(object.value("artifact").toString().toLatin1());
    const auto evidence = RelayEnvelope::verify(terms,
        QByteArray::fromHex(record.value("senderPublicKey").toString().toLatin1()),
        QByteArray::fromHex(object.value("signature").toString().toLatin1()),
        m_expectedProtocol, m_expectedNetwork, terms.createdAt);
    if (!evidence.accepted() || envelopeFields(evidence) != object
        || QString::fromLatin1(evidence.commitment().toHex())
            != record.value("commitment").toString()) return {};
    return evidence;
}

RelayEnvelope::Evidence RelayReplayStore::pendingEnvelope(const QByteArray &sender) const
{
    if (!ready() || sender.size() != 32) return {};
    for (const auto &value : m_records) {
        const auto record = value.toObject();
        if (record.value("senderPublicKey").toString()
                == QString::fromLatin1(sender.toHex())
            && record.value("pending").toBool()) return decodeEnvelope(record);
    }
    return {};
}

bool RelayReplayStore::load()
{
    QFile file(m_path);
    if (!file.exists()) return true;
    if (!file.open(QIODevice::ReadOnly) || file.size() > maximumFileSize) {
        m_error = "Cannot read relay replay store";
        return false;
    }
    QJsonParseError parse;
    const auto document = QJsonDocument::fromJson(file.readAll(), &parse);
    if (parse.error != QJsonParseError::NoError || !document.isObject()
        || document.object().size() != 4
        || document.object().value("version").toInt(-1) != 4
        || document.object().value("protocol").toString() != m_expectedProtocol
        || document.object().value("network").toString() != m_expectedNetwork
        || !document.object().value("records").isArray()) {
        m_error = "Invalid relay replay store";
        return false;
    }
    const QJsonArray records = document.object().value("records").toArray();
    if (records.size() > maximumRecords) {
        m_error = "Relay replay store exceeds record limit";
        return false;
    }
    QSet<QString> senders;
    for (const auto &value : records) {
        if (!value.isObject()) {
            m_error = "Invalid relay replay record";
            return false;
        }
        const auto record = value.toObject();
        const QString sender = record.value("senderPublicKey").toString();
        const QString commitment = record.value("commitment").toString();
        quint64 sequence = 0;
        if (record.size() != 6 || !record.value("pending").isBool()
            || !record.value("artifactPayload").isObject()
            || (record.value("pending").toBool()
                && record.value("artifactPayload").toObject().isEmpty())
            || (!record.value("pending").toBool()
                && !record.value("artifactPayload").toObject().isEmpty())
            || QJsonDocument(record.value("artifactPayload").toObject()).toJson().size() > 4096
            || !hex32.match(sender).hasMatch()
            || senders.contains(sender) || !hex32.match(commitment).hasMatch()
            || commitment == QString(64, '0')
            || !canonicalSequence(record.value("sequence"), sequence)
            || !decodeEnvelope(record).accepted()) {
            m_error = "Invalid relay replay record";
            return false;
        }
        senders.insert(sender);
    }
    m_records = records;
    return true;
}

bool RelayReplayStore::save(const QJsonArray &next)
{
    QSaveFile file(m_path);
    if (!file.open(QIODevice::WriteOnly)) {
        m_error = "Cannot write relay replay store";
        return false;
    }
    if (!file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner)) {
        m_error = "Cannot restrict relay replay store permissions";
        return false;
    }
    const QByteArray bytes = QJsonDocument(QJsonObject{{"version", 4},
        {"protocol", m_expectedProtocol}, {"network", m_expectedNetwork},
        {"records", next}})
        .toJson(QJsonDocument::Compact);
    if (bytes.size() > maximumFileSize || file.write(bytes) != bytes.size()
        || !file.commit()) {
        m_error = "Cannot commit relay replay store";
        return false;
    }
    m_records = next;
    return true;
}

bool RelayReplayStore::accept(const RelayEnvelope::Evidence &candidate,
                              qint64 now)
{
    return record(candidate, now, false);
}

QJsonObject RelayReplayStore::pendingArtifact(const QByteArray &sender) const
{
    if (!ready()) return {};
    for (const auto &value : m_records) {
        const auto entry = value.toObject();
        if (entry.value("pending").toBool()
            && entry.value("senderPublicKey").toString()
                == QString::fromLatin1(sender.toHex()))
            return entry.value("artifactPayload").toObject();
    }
    return {};
}

bool RelayReplayStore::prepare(const RelayEnvelope::Evidence &candidate, qint64 now,
                               const QJsonObject &artifact)
{
    if (artifact.isEmpty() || QJsonDocument(artifact).toJson().size() > 4096) return false;
    if (pendingRecovery(candidate, now))
        return pendingArtifact(candidate.senderPublicKey()) == artifact;
    return record(candidate, now, true, artifact);
}

bool RelayReplayStore::pendingRecovery(const RelayEnvelope::Evidence &candidate,
                                       qint64 now) const
{
    if (!ready() || !candidate.accepted() || now < candidate.terms().createdAt)
        return false;
    const auto verified = RelayEnvelope::verify(candidate.terms(),
        candidate.senderPublicKey(), candidate.signature(), m_expectedProtocol,
        m_expectedNetwork, candidate.terms().createdAt);
    if (!verified.accepted() || verified.commitment() != candidate.commitment())
        return false;
    for (const auto &value : m_records) {
        const auto entry = value.toObject();
        if (entry.value("senderPublicKey").toString().toLatin1()
            != verified.senderPublicKey().toHex()) continue;
        return entry.value("pending").toBool()
            && entry.value("sequence").toString()
                == QString::number(verified.terms().sequence)
            && entry.value("commitment").toString().toLatin1()
                == verified.commitment().toHex();
    }
    return false;
}

bool RelayReplayStore::complete(const RelayEnvelope::Evidence &candidate)
{
    if (!ready() || !candidate.accepted()) return false;
    QJsonArray next = m_records;
    for (int i = 0; i < next.size(); ++i) {
        auto entry = next.at(i).toObject();
        if (entry.value("senderPublicKey").toString().toLatin1()
            != candidate.senderPublicKey().toHex()) continue;
        if (entry.value("commitment").toString().toLatin1()
                != candidate.commitment().toHex()
            || entry.value("sequence").toString()
                != QString::number(candidate.terms().sequence)) return false;
        if (!entry.value("pending").toBool()) return true;
        entry["pending"] = false;
        entry["artifactPayload"] = QJsonObject{};
        next[i] = entry;
        return save(next);
    }
    return false;
}

bool RelayReplayStore::record(const RelayEnvelope::Evidence &candidate,
                              qint64 now, bool pending, const QJsonObject &artifact)
{
    if (!ready() || now < 0 || !candidate.accepted()) return false;
    const auto verified = RelayEnvelope::verify(candidate.terms(),
        candidate.senderPublicKey(), candidate.signature(), m_expectedProtocol,
        m_expectedNetwork, now);
    if (!verified.accepted() || verified.commitment() != candidate.commitment())
        return false;

    const auto terms = verified.terms();
    const QString sender = QString::fromLatin1(
        verified.senderPublicKey().toHex());
    const QString commitment = QString::fromLatin1(
        verified.commitment().toHex());
    QJsonArray next = m_records;
    for (int i = 0; i < next.size(); ++i) {
        const auto record = next.at(i).toObject();
        if (record.value("senderPublicKey") != sender) continue;
        quint64 previous = 0;
        if (!canonicalSequence(record.value("sequence"), previous)) return false;
        if (terms.sequence == previous) {
            if (record.value("commitment") != commitment) return false;
            if (pending && !record.value("pending").toBool()) {
                // Replay acceptance alone does not establish book application.
                // Persist the pending boundary even for an exact prior receipt.
                auto prepared = record;
                prepared["pending"] = true;
                prepared["artifactPayload"] = artifact;
                next[i] = prepared;
                return save(next);
            }
            return pending || !record.value("pending").toBool();
        }
        if (record.value("pending").toBool()) return false;
        if (previous == std::numeric_limits<quint64>::max()
            || terms.sequence != previous + 1)
            return false;
        next[i] = QJsonObject{{"senderPublicKey", sender},
            {"sequence", QString::number(terms.sequence)},
            {"commitment", commitment}, {"pending", pending},
            {"envelope", envelopeFields(verified)}, {"artifactPayload", artifact}};
        return save(next);
    }

    // A new authenticated sender starts a local relay session at sequence one.
    if (terms.sequence != 1 || next.size() >= maximumRecords) return false;
    next.append(QJsonObject{{"senderPublicKey", sender},
        {"sequence", QString::number(terms.sequence)},
        {"commitment", commitment}, {"pending", pending},
        {"envelope", envelopeFields(verified)}, {"artifactPayload", artifact}});
    return save(next);
}

quint64 RelayReplayStore::lastSequence(const QByteArray &senderPublicKey) const
{
    if (!ready() || senderPublicKey.size() != 32) return 0;
    const QString sender = QString::fromLatin1(senderPublicKey.toHex());
    for (const auto &value : m_records) {
        const auto record = value.toObject();
        if (record.value("senderPublicKey") != sender) continue;
        quint64 sequence = 0;
        return canonicalSequence(record.value("sequence"), sequence)
            ? sequence : 0;
    }
    return 0;
}
