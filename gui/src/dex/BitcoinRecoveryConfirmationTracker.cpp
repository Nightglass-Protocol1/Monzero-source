#include "BitcoinRecoveryConfirmationTracker.h"

#include <QCryptographicHash>
#include <QDataStream>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>

namespace {
const QRegularExpression hex64("^[0-9a-f]{64}$");
const QRegularExpression identifier("^[a-z0-9][a-z0-9._:-]{0,63}$");

bool number(const QString &text, quint64 &value)
{
    static const QRegularExpression integer("^(0|[1-9][0-9]{0,19})$");
    bool ok = false;
    value = text.toULongLong(&ok);
    return ok && integer.match(text).hasMatch();
}

QByteArray field(const QByteArray &value)
{
    QByteArray encoded;
    QDataStream stream(&encoded, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    stream << quint32(value.size());
    encoded += value;
    return encoded;
}

QByteArray confirmationCommitment(const QByteArray &broadcast,
                                  const QByteArray &terms,
                                  const QByteArray &observation,
                                  const QString &state, bool reorg)
{
    QByteArray preimage("MONZERO-DEX-BTC-RECOVERY-CONFIRMATION-V2\n");
    for (const QByteArray &value : {broadcast, terms, observation, state.toUtf8(),
             reorg ? QByteArray("reorg") : QByteArray("stable")})
        preimage += field(value);
    return QCryptographicHash::hash(preimage, QCryptographicHash::Sha256);
}
}

namespace BitcoinRecoveryConfirmationTracker {
Tracker::Tracker(QString path) : m_path(std::move(path))
{
    if (m_path.isEmpty()) { m_error = "Missing recovery confirmation path"; return; }
    m_lock.reset(new QLockFile(m_path + ".lock"));
    m_lock->setStaleLockTime(0);
    if (!m_lock->tryLock(0)) m_error = "Recovery confirmation store is already locked";
    else load();
}

bool Tracker::ready() const { return m_error.isEmpty(); }
QString Tracker::error() const { return m_error; }
QJsonArray Tracker::records() const { return m_records; }

bool Tracker::load()
{
    QFile file(m_path);
    if (!file.exists()) return true;
    if (!file.open(QIODevice::ReadOnly) || file.size() > 4 * 1024 * 1024) {
        m_error = "Cannot read recovery confirmation store"; return false;
    }
    QJsonParseError parse;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parse);
    if (parse.error != QJsonParseError::NoError || !document.isObject()
        || document.object().value("version").toInt(-1) != 2
        || !document.object().value("records").isArray()) {
        m_error = "Invalid recovery confirmation store"; return false;
    }
    const QJsonArray records = document.object().value("records").toArray();
    if (records.size() > 10000) {
        m_error = "Recovery confirmation store exceeds record limit"; return false;
    }
    QSet<QString> identities;
    for (const QJsonValue &value : records) {
        if (!value.isObject()) { m_error = "Invalid recovery confirmation record"; return false; }
        const QJsonObject record = value.toObject();
        quint64 block = 0, confirmations = 0, node = 0;
        const QString broadcast = record.value("broadcastCommitment").toString();
        const QString observation = record.value("observationCommitment").toString();
        const QString terms = record.value("settlementTermsCommitment").toString();
        const QString confirmationBasis =
            record.value("confirmationBasisCommitment").toString();
        const QString blockHash = record.value("blockHash").toString();
        const QString state = record.value("state").toString();
        if (record.size() != 15 || !hex64.match(broadcast).hasMatch()
            || !hex64.match(observation).hasMatch() || identities.contains(broadcast)
            || !hex64.match(terms).hasMatch()
            || (!confirmationBasis.isEmpty()
                && !hex64.match(confirmationBasis).hasMatch())
            || !hex64.match(record.value("txid").toString()).hasMatch()
            || !identifier.match(record.value("chain").toString()).hasMatch()
            || !identifier.match(record.value("leg").toString()).hasMatch()
            || !identifier.match(record.value("branch").toString()).hasMatch()
            || (!blockHash.isEmpty() && !hex64.match(blockHash).hasMatch())
            || !number(record.value("blockHeight").toString(), block)
            || !number(record.value("confirmations").toString(), confirmations)
            || !number(record.value("nodeHeight").toString(), node)
            || !record.value("requiredConfirmations").isDouble()
            || record.value("requiredConfirmations").toInt() < 1
            || record.value("requiredConfirmations").toInt() > 1000
            || !record.value("everReorged").isBool()
            || (state != "mempool" && state != "confirming"
                && state != "confirmed" && state != "reorged")) {
            m_error = "Invalid recovery confirmation record"; return false;
        }
        const bool reorg = record.value("everReorged").toBool();
        const int required = record.value("requiredConfirmations").toInt();
        const bool mempool = blockHash.isEmpty() && block == 0 && confirmations == 0;
        const bool mined = !blockHash.isEmpty() && confirmations > 0 && block <= node
            && confirmations == node - block + 1;
        const QString expectedState = reorg ? "reorged"
            : mempool ? "mempool"
            : confirmations >= static_cast<quint64>(required) ? "confirmed" : "confirming";
        if ((!mempool && !mined) || state != expectedState
            || (state == "confirmed" && confirmationBasis.isEmpty())) {
            m_error = "Inconsistent recovery confirmation record"; return false;
        }
        identities.insert(broadcast);
    }
    m_records = records;
    return true;
}

bool Tracker::save(const QJsonArray &next)
{
    QSaveFile file(m_path);
    if (!file.open(QIODevice::WriteOnly)) {
        m_error = "Cannot write recovery confirmation store"; return false;
    }
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    const QByteArray bytes = QJsonDocument(
        QJsonObject{{"version", 2}, {"records", next}}).toJson(QJsonDocument::Compact);
    if (bytes.size() > 4 * 1024 * 1024 || file.write(bytes) != bytes.size()
        || !file.commit()) {
        m_error = "Cannot commit recovery confirmation store"; return false;
    }
    m_records = next;
    return true;
}

Status Tracker::observe(const BitcoinRecoveryChainObserver::Observation &observation)
{
    Status status;
    status.m_details = {{"valid", false}, {"confirmed", false},
                        {"reorgDetected", false}, {"executable", false}};
    quint64 block = 0, confirmations = 0, node = 0;
    if (!ready() || !observation.accepted()
        || observation.broadcastCommitment().size() != 32
        || observation.commitment().size() != 32
        || observation.settlementTermsCommitment().size() != 32
        || !number(observation.blockHeight(), block)
        || !number(observation.confirmations(), confirmations)
        || !number(observation.nodeHeight(), node)) {
        status.m_details["error"] = "Invalid recovery confirmation observation";
        return status;
    }
    const QString identity = QString::fromLatin1(observation.broadcastCommitment().toHex());
    QJsonArray next = m_records;
    int index = -1;
    for (int i = 0; i < next.size(); ++i)
        if (next[i].toObject().value("broadcastCommitment") == QJsonValue(identity)) {
            index = i; break;
        }
    if (index < 0 && next.size() >= 10000) {
        status.m_details["error"] = "Recovery confirmation store is full"; return status;
    }
    const QJsonObject previous = index < 0 ? QJsonObject{} : next[index].toObject();
    if (!observation.found() && index < 0) {
        status.m_details["error"] = "Missing recovery has no prior broadcast observation";
        return status;
    }
    if (index >= 0 && (previous.value("txid") != QJsonValue(observation.txid())
        || previous.value("chain") != QJsonValue(observation.chain())
        || previous.value("leg") != QJsonValue(observation.leg())
        || previous.value("branch") != QJsonValue(observation.branch())
        || previous.value("settlementTermsCommitment") != QJsonValue(
            QString::fromLatin1(observation.settlementTermsCommitment().toHex()))
        || previous.value("requiredConfirmations").toInt()
            != observation.requiredConfirmations())) {
        status.m_details["error"] = "Recovery confirmation identity or policy changed";
        return status;
    }
    quint64 oldConfirmations = 0, oldNode = 0;
    number(previous.value("confirmations").toString(), oldConfirmations);
    number(previous.value("nodeHeight").toString(), oldNode);
    const QString oldBlock = previous.value("blockHash").toString();
    bool reorg = previous.value("everReorged").toBool();
    if (index >= 0 && (!observation.found() || confirmations < oldConfirmations || node < oldNode
        || (!oldBlock.isEmpty() && observation.blockHash() != oldBlock))) reorg = true;
    const QString state = reorg ? "reorged"
        : observation.blockHash().isEmpty() ? "mempool"
        : observation.confirmed() ? "confirmed" : "confirming";
    const QByteArray currentCommitment = confirmationCommitment(
        observation.broadcastCommitment(), observation.settlementTermsCommitment(),
        observation.commitment(), state, reorg);
    QString confirmationBasis =
        previous.value("confirmationBasisCommitment").toString();
    if (state == "confirmed" && confirmationBasis.isEmpty())
        confirmationBasis = QString::fromLatin1(currentCommitment.toHex());
    const QJsonObject record{{"broadcastCommitment", identity},
        {"observationCommitment", QString::fromLatin1(observation.commitment().toHex())},
        {"settlementTermsCommitment", QString::fromLatin1(
            observation.settlementTermsCommitment().toHex())},
        {"confirmationBasisCommitment", confirmationBasis},
        {"txid", observation.txid()}, {"chain", observation.chain()},
        {"leg", observation.leg()}, {"branch", observation.branch()},
        {"blockHash", observation.blockHash()}, {"blockHeight", observation.blockHeight()},
        {"confirmations", observation.confirmations()}, {"nodeHeight", observation.nodeHeight()},
        {"requiredConfirmations", observation.requiredConfirmations()},
        {"everReorged", reorg}, {"state", state}};
    if (index < 0) next.append(record); else next[index] = record;
    if (!save(next)) { status.m_details["error"] = m_error; return status; }

    return current(observation.broadcastCommitment());
}

Status Tracker::current(const QByteArray &broadcastCommitment) const
{
    Status status;
    status.m_details = {{"valid", false}, {"confirmed", false},
                        {"reorgDetected", false}, {"executable", false}};
    if (!ready() || broadcastCommitment.size() != 32) {
        status.m_details["error"] = "Invalid recovery confirmation identity";
        return status;
    }
    const QString identity = QString::fromLatin1(broadcastCommitment.toHex());
    QJsonObject record;
    for (const QJsonValue &value : m_records) {
        if (value.toObject().value("broadcastCommitment") == QJsonValue(identity)) {
            record = value.toObject();
            break;
        }
    }
    if (record.isEmpty()) {
        status.m_details["error"] = "Unknown recovery confirmation identity";
        return status;
    }
    const QString state = record.value("state").toString();
    const bool reorg = record.value("everReorged").toBool();
    const QByteArray observationCommitment = QByteArray::fromHex(
        record.value("observationCommitment").toString().toLatin1());
    status.m_broadcastCommitment = broadcastCommitment;
    status.m_settlementTermsCommitment = QByteArray::fromHex(
        record.value("settlementTermsCommitment").toString().toLatin1());
    status.m_confirmationBasisCommitment = QByteArray::fromHex(
        record.value("confirmationBasisCommitment").toString().toLatin1());
    status.m_leg = record.value("leg").toString();
    status.m_branch = record.value("branch").toString();
    status.m_chain = record.value("chain").toString();
    status.m_reorgDetected = reorg;
    status.m_confirmed = state == "confirmed";
    status.m_commitment = confirmationCommitment(status.m_broadcastCommitment,
        status.m_settlementTermsCommitment, observationCommitment, state, reorg);
    status.m_details = {{"valid", true}, {"state", state},
        {"confirmed", status.m_confirmed}, {"reorgDetected", reorg},
        {"executable", false}};
    return status;
}
}
