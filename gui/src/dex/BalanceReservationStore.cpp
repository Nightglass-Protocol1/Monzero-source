#include "BalanceReservationStore.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <limits>

BalanceReservationStore::BalanceReservationStore(QString path) : m_path(std::move(path))
{
    m_lock.reset(new QLockFile(m_path + ".lock"));
    m_lock->setStaleLockTime(0);
    if (!m_lock->tryLock(0)) m_error = "Reservation store is already locked";
    else load();
}

bool BalanceReservationStore::ready() const { return m_error.isEmpty(); }
QString BalanceReservationStore::error() const { return m_error; }
QJsonArray BalanceReservationStore::records() const { return m_records; }

bool BalanceReservationStore::atomic(const QString &text, quint64 &value, bool allowZero)
{
    static const QRegularExpression pattern("^(0|[1-9][0-9]{0,19})$");
    bool ok = false;
    value = text.toULongLong(&ok);
    return ok && pattern.match(text).hasMatch() && (allowZero || value != 0);
}

bool BalanceReservationStore::validId(const QString &id)
{
    static const QRegularExpression pattern("^[A-Za-z0-9][A-Za-z0-9._:-]{0,127}$");
    return pattern.match(id).hasMatch();
}

bool BalanceReservationStore::load()
{
    QFile file(m_path);
    if (!file.exists()) return true;
    if (!file.open(QIODevice::ReadOnly) || file.size() > 4 * 1024 * 1024) {
        m_error = "Cannot read reservation store"; return false;
    }
    QJsonParseError parse;
    const auto document = QJsonDocument::fromJson(file.readAll(), &parse);
    if (parse.error != QJsonParseError::NoError || !document.isObject()
        || document.object().value("version").toInt() != 1
        || !document.object().value("records").isArray()) {
        m_error = "Invalid reservation store"; return false;
    }
    QSet<QString> ids;
    for (const auto value : document.object().value("records").toArray()) {
        const auto record = value.toObject();
        quint64 ignored = 0;
        const QString id = record.value("id").toString(), state = record.value("state").toString();
        if (!value.isObject() || !validId(id) || ids.contains(id)
            || !atomic(record.value("amountAtomic").toString(), ignored)
            || (state != "reserved" && state != "committed" && state != "released"
                && state != "settled" && state != "refunded")) {
            m_error = "Invalid reservation record"; return false;
        }
        ids.insert(id);
    }
    m_records = document.object().value("records").toArray();
    return true;
}

bool BalanceReservationStore::save(const QJsonArray &next)
{
    QSaveFile file(m_path);
    if (!file.open(QIODevice::WriteOnly)) { m_error = "Cannot write reservation store"; return false; }
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    const QByteArray bytes = QJsonDocument(QJsonObject{{"version", 1}, {"records", next}})
        .toJson(QJsonDocument::Compact);
    if (file.write(bytes) != bytes.size() || !file.commit()) {
        m_error = "Cannot commit reservation store"; return false;
    }
    m_records = next;
    return true;
}

QString BalanceReservationStore::heldAtomic() const
{
    quint64 held = 0;
    for (const auto value : m_records) {
        const auto record = value.toObject();
        const QString state = record.value("state").toString();
        if (state == "released" || state == "settled" || state == "refunded") continue;
        quint64 amount = 0;
        if (!atomic(record.value("amountAtomic").toString(), amount)
            || amount > std::numeric_limits<quint64>::max() - held) return {};
        held += amount;
    }
    return QString::number(held);
}

bool BalanceReservationStore::reserve(const QString &id, const QString &amountText,
                                      const QString &unlockedText)
{
    if (!ready() || !validId(id)) return false;
    quint64 amount = 0, unlocked = 0, held = 0;
    if (!atomic(amountText, amount) || !atomic(unlockedText, unlocked, true)
        || !atomic(heldAtomic(), held, true)) return false;
    for (const auto value : m_records) {
        const auto record = value.toObject();
        if (record.value("id") == id)
            return record.value("state") == "reserved" && record.value("amountAtomic") == amountText;
    }
    if (held > unlocked || amount > unlocked - held) return false;
    QJsonArray next = m_records;
    next.append(QJsonObject{{"id", id}, {"amountAtomic", amountText}, {"state", "reserved"}});
    return save(next);
}

bool BalanceReservationStore::commit(const QString &id)
{
    if (!ready()) return false;
    QJsonArray next = m_records;
    for (int i = 0; i < next.size(); ++i) {
        auto record = next[i].toObject();
        if (record.value("id") != id) continue;
        if (record.value("state") == "committed") return true;
        if (record.value("state") != "reserved") return false;
        record["state"] = "committed"; next[i] = record; return save(next);
    }
    return false;
}

bool BalanceReservationStore::release(const QString &id)
{
    if (!ready()) return false;
    QJsonArray next = m_records;
    for (int i = 0; i < next.size(); ++i) {
        auto record = next[i].toObject();
        if (record.value("id") != id) continue;
        if (record.value("state") == "released") return true;
        if (record.value("state") != "reserved") return false;
        record["state"] = "released"; next[i] = record; return save(next);
    }
    return false;
}

bool BalanceReservationStore::finalize(const QString &id, const QString &outcome,
                                       const QString &evidenceId)
{
    static const QRegularExpression evidence("^[0-9a-f]{64}$");
    if (!ready() || (outcome != "settled" && outcome != "refunded")
        || !evidence.match(evidenceId).hasMatch()) return false;
    QJsonArray next = m_records;
    for (int i = 0; i < next.size(); ++i) {
        auto record = next[i].toObject();
        if (record.value("id") != id) continue;
        if (record.value("state") == outcome && record.value("evidenceId") == evidenceId) return true;
        if (record.value("state") != "committed") return false;
        record["state"] = outcome;
        record["evidenceId"] = evidenceId;
        next[i] = record;
        return save(next);
    }
    return false;
}
