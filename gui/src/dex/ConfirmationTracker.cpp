#include "ConfirmationTracker.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <limits>

namespace {
QVariantMap rejected(const QString &error)
{
    return {{"valid", false}, {"confirmed", false}, {"reorgDetected", false},
            {"executable", false}, {"error", error}};
}
}

ConfirmationTracker::ConfirmationTracker(QString path) : m_path(std::move(path))
{
    m_lock.reset(new QLockFile(m_path + ".lock"));
    m_lock->setStaleLockTime(0);
    if (!m_lock->tryLock(0)) m_error = "Confirmation store is already locked";
    else load();
}

bool ConfirmationTracker::ready() const { return m_error.isEmpty(); }
QString ConfirmationTracker::error() const { return m_error; }
QJsonArray ConfirmationTracker::records() const { return m_records; }

bool ConfirmationTracker::unsignedInteger(const QString &text, quint64 &value)
{
    static const QRegularExpression pattern("^(0|[1-9][0-9]{0,19})$");
    bool ok = false;
    value = text.toULongLong(&ok);
    return ok && pattern.match(text).hasMatch();
}

bool ConfirmationTracker::validChain(const QString &chain)
{
    static const QRegularExpression pattern("^[A-Z][A-Z0-9]{1,9}$");
    return pattern.match(chain).hasMatch();
}

bool ConfirmationTracker::validHash(const QString &hash)
{
    static const QRegularExpression pattern("^[0-9a-f]{64}$");
    return pattern.match(hash).hasMatch();
}

bool ConfirmationTracker::load()
{
    QFile file(m_path);
    if (!file.exists()) return true;
    if (!file.open(QIODevice::ReadOnly) || file.size() > 4 * 1024 * 1024) {
        m_error = "Cannot read confirmation store"; return false;
    }
    QJsonParseError parse;
    const auto document = QJsonDocument::fromJson(file.readAll(), &parse);
    if (parse.error != QJsonParseError::NoError || !document.isObject()
        || document.object().value("version").toInt(-1) != 1
        || !document.object().value("records").isArray()) {
        m_error = "Invalid confirmation store"; return false;
    }
    const QJsonArray records = document.object().value("records").toArray();
    if (records.size() > 10000) { m_error = "Confirmation store exceeds record limit"; return false; }
    QSet<QString> keys;
    for (const auto value : records) {
        if (!value.isObject()) { m_error = "Invalid confirmation record"; return false; }
        const QJsonObject record = value.toObject();
        const QString chain = record.value("chain").toString();
        const QString txid = record.value("transactionId").toString();
        const QString blockHash = record.value("blockHash").toString();
        quint64 block = 0, confirmations = 0, node = 0;
        if (record.size() != 9 || !validChain(chain) || !validHash(txid)
            || (!blockHash.isEmpty() && !validHash(blockHash))
            || !unsignedInteger(record.value("blockHeight").toString(), block)
            || !unsignedInteger(record.value("confirmations").toString(), confirmations)
            || !unsignedInteger(record.value("nodeHeight").toString(), node)
            || !record.value("requiredConfirmations").isDouble()
            || record.value("requiredConfirmations").toInt() < 1
            || record.value("requiredConfirmations").toInt() > 1000
            || !record.value("everReorged").isBool()
            || !record.value("state").isString() || keys.contains(chain + ":" + txid)) {
            m_error = "Invalid confirmation record"; return false;
        }
        const QString state = record.value("state").toString();
        if (state != "mempool" && state != "confirming" && state != "confirmed"
            && state != "missing" && state != "reorged") {
            m_error = "Invalid confirmation record"; return false;
        }
        const int required = record.value("requiredConfirmations").toInt();
        const bool everReorged = record.value("everReorged").toBool();
        const bool noBlockEvidence = blockHash.isEmpty() && block == 0 && confirmations == 0;
        const bool consistentBlockEvidence = !blockHash.isEmpty() && confirmations > 0
            && block <= node && confirmations == node - block + 1;
        const bool consistentState = everReorged ? state == "reorged"
            : ((state == "mempool" || state == "missing") ? noBlockEvidence
               : (consistentBlockEvidence
                  && ((state == "confirmed") == (confirmations >= static_cast<quint64>(required)))));
        if ((!noBlockEvidence && !consistentBlockEvidence) || !consistentState) {
            m_error = "Inconsistent confirmation record"; return false;
        }
        keys.insert(chain + ":" + txid);
    }
    m_records = records;
    return true;
}

bool ConfirmationTracker::save(const QJsonArray &next)
{
    QSaveFile file(m_path);
    if (!file.open(QIODevice::WriteOnly)) { m_error = "Cannot write confirmation store"; return false; }
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    const QByteArray bytes = QJsonDocument(QJsonObject{{"version", 1}, {"records", next}})
        .toJson(QJsonDocument::Compact);
    if (file.write(bytes) != bytes.size() || !file.commit()) {
        m_error = "Cannot commit confirmation store"; return false;
    }
    m_records = next;
    return true;
}

QVariantMap ConfirmationTracker::observe(const QString &chain, const QString &transactionId,
                                         bool found, const QString &blockHash,
                                         const QString &blockHeightText,
                                         const QString &confirmationsText,
                                         const QString &nodeHeightText,
                                         int requiredConfirmations)
{
    if (!ready() || !validChain(chain) || !validHash(transactionId)
        || requiredConfirmations < 1 || requiredConfirmations > 1000)
        return rejected("Invalid confirmation observation identity or policy");
    quint64 blockHeight = 0, confirmations = 0, nodeHeight = 0;
    if (!unsignedInteger(blockHeightText, blockHeight)
        || !unsignedInteger(confirmationsText, confirmations)
        || !unsignedInteger(nodeHeightText, nodeHeight))
        return rejected("Invalid confirmation observation heights");
    if (found) {
        if (confirmations == 0) {
            if (!blockHash.isEmpty() || blockHeight != 0)
                return rejected("Mempool transaction cannot contain block evidence");
        } else {
            if (!validHash(blockHash) || blockHeight > nodeHeight
                || confirmations != nodeHeight - blockHeight + 1)
                return rejected("Inconsistent confirmed transaction evidence");
        }
    } else if (!blockHash.isEmpty() || blockHeight != 0 || confirmations != 0) {
        return rejected("Missing transaction cannot contain block evidence");
    }

    const QString key = chain + ":" + transactionId;
    QJsonArray next = m_records;
    int index = -1;
    for (int i = 0; i < next.size(); ++i)
        if (next[i].toObject().value("chain").toString() + ":"
            + next[i].toObject().value("transactionId").toString() == key) { index = i; break; }
    if (index < 0 && !found) return rejected("Cannot begin tracking with missing transaction evidence");
    if (index < 0 && next.size() >= 10000) return rejected("Confirmation store is full");

    QJsonObject previous = index >= 0 ? next[index].toObject() : QJsonObject{};
    if (index >= 0 && previous.value("requiredConfirmations").toInt() != requiredConfirmations)
        return rejected("Confirmation policy cannot change while tracking a transaction");
    quint64 previousConfirmations = 0;
    unsignedInteger(previous.value("confirmations").toString(), previousConfirmations);
    const QString previousBlock = previous.value("blockHash").toString();
    bool reorg = previous.value("everReorged").toBool();
    if (index >= 0 && ((!found && previousConfirmations > 0)
        || (found && confirmations < previousConfirmations)
        || (!previousBlock.isEmpty() && !blockHash.isEmpty() && previousBlock != blockHash)))
        reorg = true;

    QString state;
    if (!found) state = reorg ? "reorged" : "missing";
    else if (confirmations == 0) state = reorg ? "reorged" : "mempool";
    else if (confirmations >= static_cast<quint64>(requiredConfirmations))
        state = reorg ? "reorged" : "confirmed";
    else state = reorg ? "reorged" : "confirming";
    QJsonObject record{{"chain", chain}, {"transactionId", transactionId},
                       {"blockHash", blockHash}, {"blockHeight", blockHeightText},
                       {"confirmations", confirmationsText}, {"nodeHeight", nodeHeightText},
                       {"requiredConfirmations", requiredConfirmations},
                       {"everReorged", reorg}, {"state", state}};
    if (index >= 0) next[index] = record; else next.append(record);
    if (!save(next)) return rejected(m_error);
    return {{"valid", true}, {"state", state},
            {"confirmed", state == "confirmed"}, {"reorgDetected", reorg},
            {"executable", false}};
}

QVariantMap ConfirmationTracker::current(const QString &chain,
                                          const QString &transactionId) const
{
    if (!ready() || !validChain(chain) || !validHash(transactionId))
        return rejected("Invalid confirmation lookup identity");
    for (const QJsonValue &value : m_records) {
        const QJsonObject record = value.toObject();
        if (record.value("chain") != chain
            || record.value("transactionId") != transactionId) continue;
        const QString state = record.value("state").toString();
        const bool reorg = record.value("everReorged").toBool();
        return {{"valid", true}, {"state", state},
            {"confirmed", state == "confirmed"}, {"reorgDetected", reorg},
            {"blockHash", record.value("blockHash")},
            {"confirmations", record.value("confirmations")},
            {"executable", false}};
    }
    return rejected("Unknown confirmation identity");
}
