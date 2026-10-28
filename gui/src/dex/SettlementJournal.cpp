#include "SettlementJournal.h"
#include <QFileInfo>
#ifdef Q_OS_UNIX
#include <unistd.h>
#endif
#include "CrossCurveProofEvidence.h"
#include "BitcoinRecoveryPsbtRpcVerifier.h"
#include "BitcoinSignedRecoveryEvidence.h"
#include "BitcoinRecoveryChainObserver.h"
#include "BitcoinRecoveryConfirmationTracker.h"
#include "BitcoinRecoveryMaturityVerifier.h"
#include "BitcoinLockPsbtRpcVerifier.h"
#include "MonzeroSuccessTransactionEvidence.h"
#include "MonzeroRefundTransactionEvidence.h"
#include "SignedQuoteAcceptor.h"
#include "ConfirmationTracker.h"

#include <QCryptographicHash>
#include <QDataStream>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>

namespace {
const int maximumRecords = 10000;
const qint64 maximumFileBytes = 8 * 1024 * 1024;
const QRegularExpression hex64("^[0-9a-f]{64}$");
const QRegularExpression artifactEvent(
    "^(pay|receive|xmz_fee)_(lock|redeem|cancel|punish|refund)_"
    "(prepared|signed|mature|broadcast|confirmed)$");
const QByteArray zeroHash(32, '\0');

QByteArray field(const QByteArray &value)
{
    QByteArray encoded;
    QDataStream stream(&encoded, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    stream << quint32(value.size());
    encoded += value;
    return encoded;
}

bool hasEvent(const QJsonArray &records, const QString &swapId, const QString &event,
              const QString &chain = {})
{
    for (const QJsonValue &value : records) {
        const QJsonObject record = value.toObject();
        if (record.value("swapId") == swapId && record.value("event") == event
            && (chain.isEmpty() || record.value("chain") == chain)) return true;
    }
    return false;
}

// Load-time index contains only records already validated in sequence. Empty
// chain keys represent an event on any chain; identifiers cannot contain '\n'.
struct EventIndex {
    QSet<QString> entries;
    void insert(const QString &swap, const QString &event, const QString &chain)
    {
        const auto prefix = swap + QLatin1Char('\n') + event + QLatin1Char('\n');
        entries.insert(prefix);
        entries.insert(prefix + chain);
    }
};

bool hasEvent(const EventIndex &index, const QString &swap, const QString &event,
              const QString &chain = {})
{
    return index.entries.contains(swap + QLatin1Char('\n') + event + QLatin1Char('\n') + chain);
}

QByteArray feeEarnedCommitment(const QByteArray &terms, const QByteArray &xmz,
                               const QByteArray &bitcoinConfirmation,
                               const QByteArray &bitcoinBroadcast,
                               const QString &xmzChain, const QString &bitcoinChain)
{
    QByteArray preimage("MONZERO-DEX-PROTOCOL-FEE-EARNED-V1\n");
    for (const QByteArray &value : {terms, xmz, bitcoinConfirmation,
             bitcoinBroadcast, xmzChain.toUtf8(), bitcoinChain.toUtf8()})
        preimage += field(value);
    return QCryptographicHash::hash(preimage, QCryptographicHash::Sha256);
}

QByteArray settledOutcomeCommitment(const QString &swapId,
    const QByteArray &reservation, const QByteArray &quote,
    const QByteArray &earned)
{
    QByteArray preimage("MONZERO-DEX-SETTLEMENT-OUTCOME-V1\n");
    for (const QByteArray &value : {swapId.toUtf8(), QByteArray("settled"),
             reservation, quote, earned})
        preimage += field(value);
    return QCryptographicHash::hash(preimage, QCryptographicHash::Sha256);
}

QByteArray xmzRefundCommitment(const QByteArray &terms,
    const QByteArray &principal, const QByteArray &fee, const QString &chain)
{
    QByteArray preimage("MONZERO-DEX-XMZ-REFUND-COMPLETE-V1\n");
    for (const QByteArray &value : {terms, principal, fee, chain.toUtf8()})
        preimage += field(value);
    return QCryptographicHash::hash(preimage, QCryptographicHash::Sha256);
}

QByteArray refundedOutcomeCommitment(const QString &swapId,
    const QByteArray &reservation, const QByteArray &quote,
    const QByteArray &xmzRefund, const QByteArray &bitcoinConfirmation,
    const QByteArray &bitcoinBroadcast)
{
    QByteArray preimage("MONZERO-DEX-SETTLEMENT-OUTCOME-V1\n");
    for (const QByteArray &value : {swapId.toUtf8(), QByteArray("refunded"),
             reservation, quote, xmzRefund, bitcoinConfirmation,
             bitcoinBroadcast})
        preimage += field(value);
    return QCryptographicHash::hash(preimage, QCryptographicHash::Sha256);
}

template<typename History>
bool transitionAllowed(const History &records, const QString &swapId,
                       const QString &event, const QString &chain)
{
    const bool accepted = hasEvent(records, swapId, "quote_accepted", "protocol");
    if (event == "quote_accepted") return chain == "protocol" && !accepted;
    if (!accepted) return false;
    if (event == "maker_dleq_verified" || event == "taker_dleq_verified")
        return chain == "protocol" && !hasEvent(records, swapId, event, chain);
    if (event == "protocol_fee_earned")
        return chain != "protocol" && !hasEvent(records, swapId, event, chain)
            && hasEvent(records, swapId, "pay_redeem_confirmed")
            && !hasEvent(records, swapId, "xmz_refund_confirmed");
    if (event == "protocol_fee_revoked")
        return chain != "protocol" && !hasEvent(records, swapId, event, chain)
            && hasEvent(records, swapId, "protocol_fee_earned", chain);
    if (event == "xmz_refund_confirmed")
        return chain != "protocol" && !hasEvent(records, swapId, event, chain)
            && hasEvent(records, swapId, "pay_refund_confirmed")
            && !hasEvent(records, swapId, "protocol_fee_earned");

    const QRegularExpressionMatch match = artifactEvent.match(event);
    if (!match.hasMatch() || chain == "protocol") return false;
    const QString prefix = match.captured(1) + QLatin1Char('_') + match.captured(2);
    const QString phase = match.captured(3);
    if (hasEvent(records, swapId, event, chain)) return false;
    if (phase == "prepared") return true;
    if (phase == "signed") return hasEvent(records, swapId, prefix + "_prepared", chain);
    if (phase == "mature")
        return (match.captured(2) == "cancel" || match.captured(2) == "punish")
            && hasEvent(records, swapId, prefix + "_prepared", chain);
    if (phase == "broadcast")
        return hasEvent(records, swapId, prefix + "_signed", chain)
            && ((match.captured(2) != "cancel" && match.captured(2) != "punish")
                || hasEvent(records, swapId, prefix + "_mature", chain));
    return hasEvent(records, swapId, prefix + "_broadcast", chain);
}
}

SettlementJournal::SettlementJournal(QString path) : m_path(std::move(path))
{
    if (m_path.isEmpty()) {
        m_error = "Missing settlement journal path";
        return;
    }
    const QFileInfo target(m_path), parent(target.absolutePath());
    const auto shared = QFileDevice::ReadGroup | QFileDevice::WriteGroup | QFileDevice::ExeGroup
        | QFileDevice::ReadOther | QFileDevice::WriteOther | QFileDevice::ExeOther;
    if (!parent.isDir() || parent.isSymLink() || (parent.permissions() & shared)
        || target.isSymLink() || (target.exists() && (!target.isFile() || (target.permissions() & shared)))) {
        m_error = "Settlement journal requires private regular storage";
        return;
    }
#ifdef Q_OS_UNIX
    if (parent.ownerId() != geteuid() || (target.exists() && target.ownerId() != geteuid())) {
        m_error = "Settlement journal must belong to this user";
        return;
    }
#endif
    m_path = target.absoluteFilePath();
    m_lock.reset(new QLockFile(m_path + ".lock"));
    m_lock->setStaleLockTime(0);
    if (!m_lock->tryLock(0)) m_error = "Settlement journal is already locked";
    else load();
}

bool SettlementJournal::ready() const { return m_error.isEmpty(); }
QString SettlementJournal::error() const { return m_error; }
QJsonArray SettlementJournal::records() const { return m_records; }

QString SettlementJournal::headHash() const
{
    if (m_records.isEmpty()) return QString::fromLatin1(zeroHash.toHex());
    return m_records.last().toObject().value("recordHash").toString();
}

RecoveredMutualQuote SettlementJournal::recoverQuote(const QString &swapId,
    const SignedQuoteAcceptor &acceptor, const QByteArray &makerKey,
    const QByteArray &nonce, qint64 now) const
{
    if (!ready() || !validIdentifier(swapId, 128)) return {};
    const auto quote = acceptor.recoverMutualQuote(makerKey, nonce, now);
    if (!quote.valid()) return {};
    for (const auto value : m_records) {
        const auto record = value.toObject();
        if (record.value("swapId") != swapId || record.value("event") != "quote_accepted"
            || record.value("chain") != "protocol") continue;
        bool ok = false;
        const auto timestamp = record.value("timestamp").toString().toLongLong(&ok);
        if (ok && timestamp >= quote.acceptedAt() && timestamp <= now
            && record.value("artifactHash") == QString::fromLatin1(quote.commitment().toHex()))
            return quote;
        return {};
    }
    return {};
}

bool SettlementJournal::validIdentifier(const QString &value, int maximum)
{
    const QRegularExpression pattern(
        QStringLiteral("^[a-z0-9][a-z0-9._:-]{0,%1}$").arg(maximum - 1));
    return pattern.match(value).hasMatch();
}

QByteArray SettlementJournal::recordHash(quint64 sequence, const QString &swapId,
                                         const QString &event, const QString &chain,
                                         const QByteArray &artifactHash, qint64 timestamp,
                                         const QByteArray &previousHash)
{
    QByteArray input("MONZERO-DEX-SETTLEMENT-JOURNAL-V1\n");
    input += field(QByteArray::number(sequence));
    input += field(swapId.toUtf8());
    input += field(event.toUtf8());
    input += field(chain.toUtf8());
    input += field(artifactHash);
    input += field(QByteArray::number(timestamp));
    input += field(previousHash);
    return QCryptographicHash::hash(input, QCryptographicHash::Sha256);
}

bool SettlementJournal::load()
{
    QFile file(m_path);
    if (!file.exists()) return true;
    if (!file.open(QIODevice::ReadOnly) || file.size() > maximumFileBytes) {
        m_error = "Cannot read settlement journal";
        return false;
    }
    QJsonParseError parse;
    const auto bytes = file.read(maximumFileBytes + 1);
    if (bytes.size() > maximumFileBytes || !file.atEnd()) {
        m_error = "Settlement journal exceeds byte limit";
        return false;
    }
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &parse);
    if (parse.error != QJsonParseError::NoError || !document.isObject()
        || document.object().value("version").toInt(-1) != 1
        || !document.object().value("records").isArray()) {
        m_error = "Invalid settlement journal";
        return false;
    }
    const QJsonArray records = document.object().value("records").toArray();
    if (records.size() > maximumRecords) {
        m_error = "Settlement journal exceeds record limit";
        return false;
    }

    QByteArray previous = zeroHash;
    EventIndex validatedEvents;
    QSet<QString> identities;
    quint64 expectedSequence = 1;
    for (const QJsonValue &value : records) {
        if (!value.isObject()) { m_error = "Invalid settlement journal record"; return false; }
        const QJsonObject record = value.toObject();
        const QString sequenceText = record.value("sequence").toString();
        const QString swapId = record.value("swapId").toString();
        const QString event = record.value("event").toString();
        const QString chain = record.value("chain").toString();
        const QString artifactText = record.value("artifactHash").toString();
        const QString timestampText = record.value("timestamp").toString();
        const QString previousText = record.value("previousHash").toString();
        const QString hashText = record.value("recordHash").toString();
        bool sequenceOk = false, timestampOk = false;
        const quint64 sequence = sequenceText.toULongLong(&sequenceOk);
        const qint64 timestamp = timestampText.toLongLong(&timestampOk);
        const QByteArray artifact = QByteArray::fromHex(artifactText.toLatin1());
        const QByteArray claimedPrevious = QByteArray::fromHex(previousText.toLatin1());
        const QByteArray claimedHash = QByteArray::fromHex(hashText.toLatin1());
        const QString identity = swapId + QLatin1Char('\n') + event + QLatin1Char('\n')
            + chain + QLatin1Char('\n') + artifactText;
        if (record.size() != 8 || !sequenceOk || sequence != expectedSequence
            || sequenceText != QString::number(sequence) || !timestampOk || timestamp < 0
            || timestampText != QString::number(timestamp) || !validIdentifier(swapId, 128)
            || !validIdentifier(event, 64) || !validIdentifier(chain, 64)
            || !hex64.match(artifactText).hasMatch() || artifact.size() != 32
            || artifact == zeroHash
            || !hex64.match(previousText).hasMatch() || claimedPrevious != previous
            || !hex64.match(hashText).hasMatch()
            || claimedHash != recordHash(sequence, swapId, event, chain, artifact,
                                         timestamp, previous)
            || identities.contains(identity)
            || !transitionAllowed(validatedEvents, swapId, event, chain)) {
            m_error = "Invalid settlement journal record";
            return false;
        }
        identities.insert(identity);
        validatedEvents.insert(swapId, event, chain);
        previous = claimedHash;
        ++expectedSequence;
    }
    m_records = records;
    return true;
}

bool SettlementJournal::save(const QJsonArray &next)
{
    QSaveFile file(m_path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly)) {
        m_error = "Cannot write settlement journal";
        return false;
    }
    if (!file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner)) {
        m_error = "Cannot secure settlement journal permissions";
        return false;
    }
    const QByteArray bytes = QJsonDocument(QJsonObject{{"version", 1}, {"records", next}})
        .toJson(QJsonDocument::Compact);
    if (bytes.size() > maximumFileBytes || file.write(bytes) != bytes.size()
        || !file.commit()) {
        m_error = "Cannot commit settlement journal";
        return false;
    }
    m_records = next;
    return true;
}

bool SettlementJournal::append(const QString &swapId, const QString &event,
                               const QString &chain, const QByteArray &artifactHash,
                               qint64 timestamp)
{
    if (!ready() || timestamp < 0
        || !validIdentifier(swapId, 128) || !validIdentifier(event, 64)
        || !validIdentifier(chain, 64) || artifactHash.size() != 32
        || artifactHash == zeroHash) return false;

    const QString artifactText = QString::fromLatin1(artifactHash.toHex());
    for (const QJsonValue &value : m_records) {
        const QJsonObject record = value.toObject();
        if (record.value("swapId") == swapId && record.value("event") == event
            && record.value("chain") == chain
            && record.value("artifactHash") == artifactText)
            return record.value("timestamp") == QString::number(timestamp);
    }
    // An exact durable retry does not consume capacity. In particular, restart
    // recovery must not lose idempotence merely because the journal is full.
    if (m_records.size() >= maximumRecords
        || !transitionAllowed(m_records, swapId, event, chain)) return false;

    const quint64 sequence = quint64(m_records.size()) + 1;
    const QByteArray previous = QByteArray::fromHex(headHash().toLatin1());
    const QByteArray hash = recordHash(sequence, swapId, event, chain, artifactHash,
                                       timestamp, previous);
    QJsonArray next = m_records;
    next.append(QJsonObject{{"sequence", QString::number(sequence)}, {"swapId", swapId},
                            {"event", event}, {"chain", chain},
                            {"artifactHash", artifactText},
                            {"timestamp", QString::number(timestamp)},
                            {"previousHash", QString::fromLatin1(previous.toHex())},
                            {"recordHash", QString::fromLatin1(hash.toHex())}});
    return save(next);
}

bool SettlementJournal::appendQuoteAccepted(const QString &swapId,
                                            const AcceptedQuote &evidence,
                                            qint64 timestamp)
{
    if (!evidence.accepted()) return false;
    return append(swapId, "quote_accepted", "protocol",
                  evidence.commitment(), timestamp);
}

bool SettlementJournal::ensureMutualQuoteAccepted(const AcceptedQuote &quote, qint64 timestamp)
{
    if (!ready() || !quote.mutuallyAuthorized() || timestamp < 0) return false;
    const auto swapId = QString::fromLatin1(quote.commitment().toHex());
    for (const auto value : m_records) {
        const auto record = value.toObject();
        if (record.value("swapId") != swapId || record.value("event") != "quote_accepted"
            || record.value("chain") != "protocol") continue;
        bool ok = false;
        const auto originalTime = record.value("timestamp").toString().toLongLong(&ok);
        return ok && originalTime <= timestamp && record.value("artifactHash") == swapId
            && appendQuoteAccepted(swapId, quote, originalTime);
    }
    // Repairing a missing first event is admission, not expired-quote recovery.
    // Already durable events above may be retried after expiry without mutation.
    if (!SettlementTerms::build(quote.terms(), timestamp).valid) return false;
    return appendQuoteAccepted(swapId, quote, timestamp);
}

bool SettlementJournal::appendDleqVerification(
    const QString &swapId, const CrossCurveProofEvidence::Verification &evidence,
    qint64 timestamp)
{
    if (!evidence.accepted() || evidence.party().isEmpty()) return false;
    const QString expectedTerms =
        QString::fromLatin1(evidence.settlementTermsCommitment().toHex());
    bool matchingAcceptedTerms = false;
    for (const QJsonValue &value : m_records) {
        const QJsonObject record = value.toObject();
        if (record.value("swapId") == swapId
            && record.value("event") == "quote_accepted"
            && record.value("chain") == "protocol"
            && record.value("artifactHash") == expectedTerms) {
            matchingAcceptedTerms = true;
            break;
        }
    }
    if (!matchingAcceptedTerms) return false;
    return append(swapId, evidence.party() + "_dleq_verified", "protocol",
                  evidence.journalCommitment(), timestamp);
}

bool SettlementJournal::appendBitcoinLockPrepared(
    const QString &swapId, const BitcoinLockPsbtRpcVerifier::Evidence &evidence,
    qint64 timestamp)
{
    if (!evidence.accepted() || evidence.leg().isEmpty()
        || evidence.chain().isEmpty()) return false;
    const QString expectedTerms =
        QString::fromLatin1(evidence.settlementTermsCommitment().toHex());
    bool matchingAcceptedTerms = false;
    for (const QJsonValue &value : m_records) {
        const QJsonObject record = value.toObject();
        if (record.value("swapId") == swapId
            && record.value("event") == "quote_accepted"
            && record.value("chain") == "protocol"
            && record.value("artifactHash") == expectedTerms) {
            matchingAcceptedTerms = true;
            break;
        }
    }
    if (!matchingAcceptedTerms) return false;
    return append(swapId, evidence.leg() + "_lock_prepared", evidence.chain(),
                  evidence.journalCommitment(), timestamp);
}

bool SettlementJournal::appendBitcoinRecoveryPrepared(const QString &swapId,
    const RecoveredMutualQuote &quote,
    const BitcoinRecoveryPsbtRpcVerifier::Evidence &evidence, qint64 timestamp)
{
    if (!quote.valid() || !evidence.accepted() || timestamp < quote.acceptedAt()
        || evidence.settlementTermsCommitment() != quote.commitment()) return false;
    for (const auto value : m_records) {
        const auto record = value.toObject();
        if (record.value("swapId") != swapId || record.value("event") != "quote_accepted"
            || record.value("chain") != "protocol") continue;
        bool ok = false;
        const auto accepted = record.value("timestamp").toString().toLongLong(&ok);
        if (!ok || accepted < quote.acceptedAt() || accepted > timestamp
            || record.value("artifactHash") != QString::fromLatin1(quote.commitment().toHex())) return false;
        return appendBitcoinRecoveryPrepared(swapId, evidence, timestamp);
    }
    return false;
}

bool SettlementJournal::appendBitcoinRecoveryPrepared(
    const QString &swapId, const BitcoinRecoveryPsbtRpcVerifier::Evidence &evidence,
    qint64 timestamp)
{
    if (!evidence.accepted() || evidence.leg().isEmpty()
        || evidence.branch().isEmpty() || evidence.chain().isEmpty()) return false;
    const QString expectedTerms =
        QString::fromLatin1(evidence.settlementTermsCommitment().toHex());
    bool matchingAcceptedTerms = false;
    for (const QJsonValue &value : m_records) {
        const QJsonObject record = value.toObject();
        if (record.value("swapId") == swapId
            && record.value("event") == "quote_accepted"
            && record.value("chain") == "protocol"
            && record.value("artifactHash") == expectedTerms) {
            matchingAcceptedTerms = true;
            break;
        }
    }
    if (!matchingAcceptedTerms) return false;
    return append(swapId, evidence.leg() + "_" + evidence.branch() + "_prepared",
                  evidence.chain(), evidence.journalCommitment(), timestamp);
}

bool SettlementJournal::appendBitcoinRecoverySigned(
    const QString &swapId, const BitcoinSignedRecoveryEvidence::Evidence &evidence,
    qint64 timestamp)
{
    if (!evidence.accepted() || evidence.leg().isEmpty()
        || evidence.branch().isEmpty() || evidence.chain().isEmpty()) return false;
    const QString preparedEvent = evidence.leg() + "_" + evidence.branch() + "_prepared";
    const QString expectedPrepared =
        QString::fromLatin1(evidence.preparedCommitment().toHex());
    bool matchingPrepared = false;
    for (const QJsonValue &value : m_records) {
        const QJsonObject record = value.toObject();
        if (record.value("swapId") == swapId
            && record.value("event") == preparedEvent
            && record.value("chain") == evidence.chain()
            && record.value("artifactHash") == expectedPrepared) {
            matchingPrepared = true;
            break;
        }
    }
    if (!matchingPrepared) return false;
    return append(swapId, evidence.leg() + "_" + evidence.branch() + "_signed",
                  evidence.chain(), evidence.journalCommitment(), timestamp);
}

bool SettlementJournal::appendBitcoinRecoveryMature(
    const QString &swapId,
    const BitcoinRecoveryMaturityVerifier::Evidence &evidence,
    qint64 timestamp)
{
    if (!evidence.accepted() || evidence.preparedCommitment().size() != 32
        || evidence.commitment().size() != 32 || evidence.leg().isEmpty()
        || (evidence.branch() != "cancel" && evidence.branch() != "punish")
        || evidence.chain().isEmpty()) return false;
    const QString preparedEvent =
        evidence.leg() + "_" + evidence.branch() + "_prepared";
    const QString expectedPrepared =
        QString::fromLatin1(evidence.preparedCommitment().toHex());
    bool matchingPrepared = false;
    for (const QJsonValue &value : m_records) {
        const QJsonObject record = value.toObject();
        if (record.value("swapId") == swapId
            && record.value("event") == preparedEvent
            && record.value("chain") == evidence.chain()
            && record.value("artifactHash") == expectedPrepared) {
            matchingPrepared = true;
            break;
        }
    }
    if (!matchingPrepared) return false;
    return append(swapId, evidence.leg() + "_" + evidence.branch() + "_mature",
                  evidence.chain(), evidence.commitment(), timestamp);
}

bool SettlementJournal::appendBitcoinRecoveryObserved(
    const QString &swapId,
    const BitcoinRecoveryChainObserver::Observation &observation,
    qint64 timestamp)
{
    if (!observation.accepted() || !observation.found() || observation.leg().isEmpty()
        || observation.branch().isEmpty() || observation.chain().isEmpty()
        || observation.signedCommitment().size() != 32
        || observation.broadcastCommitment().size() != 32) return false;
    const QString signedEvent = observation.leg() + "_" + observation.branch() + "_signed";
    const QString expectedSigned =
        QString::fromLatin1(observation.signedCommitment().toHex());
    bool matchingSigned = false;
    for (const QJsonValue &value : m_records) {
        const QJsonObject record = value.toObject();
        if (record.value("swapId") == swapId
            && record.value("event") == signedEvent
            && record.value("chain") == observation.chain()
            && record.value("artifactHash") == expectedSigned) {
            matchingSigned = true;
            break;
        }
    }
    if (!matchingSigned) return false;
    return append(swapId,
                  observation.leg() + "_" + observation.branch() + "_broadcast",
                  observation.chain(), observation.broadcastCommitment(), timestamp);
}

bool SettlementJournal::appendBitcoinRecoveryConfirmed(
    const QString &swapId,
    const BitcoinRecoveryConfirmationTracker::Tracker &bitcoinTracker,
    const QByteArray &bitcoinBroadcastCommitment,
    qint64 timestamp)
{
    const auto status = bitcoinTracker.current(bitcoinBroadcastCommitment);
    if (!status.accepted() || !status.confirmed() || status.reorgDetected()
        || status.leg().isEmpty() || status.branch().isEmpty()
        || status.chain().isEmpty() || status.broadcastCommitment().size() != 32
        || status.commitment().size() != 32) return false;
    const QString broadcastEvent = status.leg() + "_" + status.branch() + "_broadcast";
    const QString expectedBroadcast =
        QString::fromLatin1(status.broadcastCommitment().toHex());
    bool matchingBroadcast = false;
    for (const QJsonValue &value : m_records) {
        const QJsonObject record = value.toObject();
        if (record.value("swapId") == swapId
            && record.value("event") == broadcastEvent
            && record.value("chain") == status.chain()
            && record.value("artifactHash") == expectedBroadcast) {
            matchingBroadcast = true;
            break;
        }
    }
    if (!matchingBroadcast) return false;
    return append(swapId, status.leg() + "_" + status.branch() + "_confirmed",
                  status.chain(), status.commitment(), timestamp);
}

bool SettlementJournal::appendProtocolFeeEarned(
    const QString &swapId,
    const MonzeroSuccessTransactionEvidence::Evidence &xmzEvidence,
    const BitcoinRecoveryConfirmationTracker::Tracker &bitcoinTracker,
    const QByteArray &bitcoinBroadcastCommitment,
    qint64 timestamp)
{
    if (!xmzEvidence.accepted() || xmzEvidence.commitment().size() != 32
        || xmzEvidence.settlementTermsCommitment().size() != 32
        || xmzEvidence.chain().isEmpty()) return false;

    // Fetch under the tracker's lifetime lock here. A previously returned
    // Status is deliberately insufficient because it may predate a reorg.
    const auto bitcoin = bitcoinTracker.current(bitcoinBroadcastCommitment);
    if (!bitcoin.accepted() || !bitcoin.confirmed() || bitcoin.reorgDetected()
        || bitcoin.leg() != "pay" || bitcoin.branch() != "redeem"
        || bitcoin.chain().isEmpty() || bitcoin.chain() == xmzEvidence.chain()
        || bitcoin.settlementTermsCommitment()
            != xmzEvidence.settlementTermsCommitment()
        || bitcoin.confirmationBasisCommitment().size() != 32) return false;

    const QString terms = QString::fromLatin1(
        xmzEvidence.settlementTermsCommitment().toHex());
    const QString broadcast = QString::fromLatin1(
        bitcoin.broadcastCommitment().toHex());
    bool acceptedQuote = false, matchingBroadcast = false, confirmedRedeem = false;
    for (const QJsonValue &value : m_records) {
        const QJsonObject record = value.toObject();
        if (record.value("swapId") != swapId) continue;
        if (record.value("event") == "quote_accepted"
            && record.value("chain") == "protocol"
            && record.value("artifactHash") == terms) acceptedQuote = true;
        if (record.value("event") == "pay_redeem_broadcast"
            && record.value("chain") == bitcoin.chain()
            && record.value("artifactHash") == broadcast) matchingBroadcast = true;
        if (record.value("event") == "pay_redeem_confirmed"
            && record.value("chain") == bitcoin.chain()) confirmedRedeem = true;
    }
    if (!acceptedQuote || !matchingBroadcast || !confirmedRedeem) return false;

    return append(swapId, "protocol_fee_earned", xmzEvidence.chain(),
                  feeEarnedCommitment(xmzEvidence.settlementTermsCommitment(),
                      xmzEvidence.commitment(),
                      bitcoin.confirmationBasisCommitment(),
                      bitcoin.broadcastCommitment(), xmzEvidence.chain(),
                      bitcoin.chain()),
                  timestamp);
}

bool SettlementJournal::appendProtocolFeeRevoked(
    const QString &swapId,
    const MonzeroSuccessTransactionEvidence::Evidence &xmzEvidence,
    const BitcoinRecoveryConfirmationTracker::Tracker &bitcoinTracker,
    const QByteArray &bitcoinBroadcastCommitment,
    qint64 timestamp)
{
    if (!xmzEvidence.accepted() || xmzEvidence.commitment().size() != 32
        || xmzEvidence.settlementTermsCommitment().size() != 32
        || xmzEvidence.chain().isEmpty()) return false;
    const auto bitcoin = bitcoinTracker.current(bitcoinBroadcastCommitment);
    if (!bitcoin.accepted() || bitcoin.confirmed() || !bitcoin.reorgDetected()
        || bitcoin.leg() != "pay" || bitcoin.branch() != "redeem"
        || bitcoin.chain().isEmpty() || bitcoin.chain() == xmzEvidence.chain()
        || bitcoin.settlementTermsCommitment()
            != xmzEvidence.settlementTermsCommitment()
        || bitcoin.confirmationBasisCommitment().size() != 32) return false;

    const QString terms = QString::fromLatin1(
        xmzEvidence.settlementTermsCommitment().toHex());
    const QString broadcast = QString::fromLatin1(
        bitcoin.broadcastCommitment().toHex());
    const QByteArray earned = feeEarnedCommitment(
        xmzEvidence.settlementTermsCommitment(), xmzEvidence.commitment(),
        bitcoin.confirmationBasisCommitment(), bitcoin.broadcastCommitment(),
        xmzEvidence.chain(), bitcoin.chain());
    const QString earnedText = QString::fromLatin1(earned.toHex());
    bool acceptedQuote = false, matchingBroadcast = false, matchingEarned = false;
    for (const QJsonValue &value : m_records) {
        const QJsonObject record = value.toObject();
        if (record.value("swapId") != swapId) continue;
        if (record.value("event") == "quote_accepted"
            && record.value("chain") == "protocol"
            && record.value("artifactHash") == terms) acceptedQuote = true;
        if (record.value("event") == "pay_redeem_broadcast"
            && record.value("chain") == bitcoin.chain()
            && record.value("artifactHash") == broadcast) matchingBroadcast = true;
        if (record.value("event") == "protocol_fee_earned"
            && record.value("chain") == xmzEvidence.chain()
            && record.value("artifactHash") == earnedText) matchingEarned = true;
    }
    if (!acceptedQuote || !matchingBroadcast || !matchingEarned) return false;

    QByteArray preimage("MONZERO-DEX-PROTOCOL-FEE-REVOKED-V1\n");
    for (const QByteArray &value : {earned, bitcoin.commitment(),
             bitcoin.broadcastCommitment(),
             xmzEvidence.settlementTermsCommitment()})
        preimage += field(value);
    return append(swapId, "protocol_fee_revoked", xmzEvidence.chain(),
                  QCryptographicHash::hash(preimage, QCryptographicHash::Sha256),
                  timestamp);
}

SettlementOutcome::Evidence SettlementJournal::settledEvidence(
    const QString &swapId, const AcceptedQuote &quote,
    const MonzeroSuccessTransactionEvidence::Evidence &xmzEvidence,
    const BitcoinRecoveryConfirmationTracker::Tracker &bitcoinTracker,
    const QByteArray &bitcoinBroadcastCommitment) const
{
    SettlementOutcome::Evidence result;
    if (!ready() || !validIdentifier(swapId, 128) || !quote.accepted()
        || quote.commitment().size() != 32
        || quote.fillReservationCommitment().size() != 32
        || !xmzEvidence.accepted() || xmzEvidence.commitment().size() != 32
        || xmzEvidence.settlementTermsCommitment() != quote.commitment()
        || xmzEvidence.chain().isEmpty()) return result;

    const auto bitcoin = bitcoinTracker.current(bitcoinBroadcastCommitment);
    if (!bitcoin.accepted() || !bitcoin.confirmed() || bitcoin.reorgDetected()
        || bitcoin.leg() != "pay" || bitcoin.branch() != "redeem"
        || bitcoin.chain().isEmpty() || bitcoin.chain() == xmzEvidence.chain()
        || bitcoin.settlementTermsCommitment() != quote.commitment()
        || bitcoin.confirmationBasisCommitment().size() != 32) return result;

    const QByteArray earned = feeEarnedCommitment(quote.commitment(),
        xmzEvidence.commitment(), bitcoin.confirmationBasisCommitment(),
        bitcoin.broadcastCommitment(), xmzEvidence.chain(), bitcoin.chain());
    const QString quoteText = QString::fromLatin1(quote.commitment().toHex());
    const QString broadcastText = QString::fromLatin1(
        bitcoin.broadcastCommitment().toHex());
    const QString earnedText = QString::fromLatin1(earned.toHex());
    bool accepted = false, broadcast = false, confirmed = false, fee = false;
    for (const QJsonValue &value : m_records) {
        const QJsonObject record = value.toObject();
        if (record.value("swapId") != swapId) continue;
        const QString event = record.value("event").toString();
        const QString chain = record.value("chain").toString();
        const QString artifact = record.value("artifactHash").toString();
        accepted |= event == "quote_accepted" && chain == "protocol"
            && artifact == quoteText;
        broadcast |= event == "pay_redeem_broadcast" && chain == bitcoin.chain()
            && artifact == broadcastText;
        // The tracker commitment evolves with confirmation depth. The exact
        // broadcast is bound above and current() revalidates its latched
        // confirmation basis, so any typed confirmed transition for that
        // branch is sufficient here.
        confirmed |= event == "pay_redeem_confirmed" && chain == bitcoin.chain();
        fee |= event == "protocol_fee_earned" && chain == xmzEvidence.chain()
            && artifact == earnedText;
    }
    if (!accepted || !broadcast || !confirmed || !fee
        || hasEvent(m_records, swapId, "xmz_refund_confirmed")) return result;

    result.m_swapId = swapId;
    result.m_outcome = "settled";
    result.m_fillReservationCommitment = quote.fillReservationCommitment();
    result.m_quoteCommitment = quote.commitment();
    result.m_commitment = settledOutcomeCommitment(swapId,
        result.m_fillReservationCommitment, result.m_quoteCommitment, earned);
    return result;
}

bool SettlementJournal::appendXmzRefundConfirmed(
    const QString &swapId,
    const MonzeroRefundTransactionEvidence::Evidence &principalRefund,
    const MonzeroRefundTransactionEvidence::Evidence &feeRefund,
    qint64 timestamp)
{
    if (!principalRefund.accepted() || !feeRefund.accepted()
        || principalRefund.leg() != "receive" || feeRefund.leg() != "xmz_fee"
        || principalRefund.chain().isEmpty()
        || principalRefund.chain() != feeRefund.chain()
        || principalRefund.transactionId() == feeRefund.transactionId()
        || principalRefund.settlementTermsCommitment()
            != feeRefund.settlementTermsCommitment()) return false;
    const QString terms = QString::fromLatin1(
        principalRefund.settlementTermsCommitment().toHex());
    bool accepted = false;
    for (const QJsonValue &value : m_records) {
        const QJsonObject record = value.toObject();
        accepted |= record.value("swapId") == swapId
            && record.value("event") == "quote_accepted"
            && record.value("chain") == "protocol"
            && record.value("artifactHash") == terms;
    }
    if (!accepted) return false;
    return append(swapId, "xmz_refund_confirmed", principalRefund.chain(),
        xmzRefundCommitment(principalRefund.settlementTermsCommitment(),
            principalRefund.commitment(), feeRefund.commitment(),
            principalRefund.chain()), timestamp);
}

SettlementOutcome::Evidence SettlementJournal::refundedEvidence(
    const QString &swapId, const AcceptedQuote &quote,
    const MonzeroRefundTransactionEvidence::Evidence &principalRefund,
    const MonzeroRefundTransactionEvidence::Evidence &feeRefund,
    const BitcoinRecoveryConfirmationTracker::Tracker &bitcoinTracker,
    const QByteArray &bitcoinBroadcastCommitment) const
{
    SettlementOutcome::Evidence result;
    if (!ready() || !validIdentifier(swapId, 128) || !quote.accepted()
        || quote.commitment().size() != 32
        || quote.fillReservationCommitment().size() != 32
        || !principalRefund.accepted() || !feeRefund.accepted()
        || principalRefund.leg() != "receive" || feeRefund.leg() != "xmz_fee"
        || principalRefund.chain().isEmpty()
        || principalRefund.chain() != feeRefund.chain()
        || principalRefund.transactionId() == feeRefund.transactionId()
        || principalRefund.settlementTermsCommitment() != quote.commitment()
        || feeRefund.settlementTermsCommitment() != quote.commitment()) return result;
    const auto bitcoin = bitcoinTracker.current(bitcoinBroadcastCommitment);
    if (!bitcoin.accepted() || !bitcoin.confirmed() || bitcoin.reorgDetected()
        || bitcoin.leg() != "pay" || bitcoin.branch() != "refund"
        || bitcoin.chain().isEmpty() || bitcoin.chain() == principalRefund.chain()
        || bitcoin.settlementTermsCommitment() != quote.commitment()
        || bitcoin.confirmationBasisCommitment().size() != 32) return result;

    const QByteArray xmz = xmzRefundCommitment(quote.commitment(),
        principalRefund.commitment(), feeRefund.commitment(), principalRefund.chain());
    const QString quoteText = QString::fromLatin1(quote.commitment().toHex());
    const QString broadcastText = QString::fromLatin1(
        bitcoin.broadcastCommitment().toHex());
    const QString xmzText = QString::fromLatin1(xmz.toHex());
    bool accepted = false, broadcast = false, confirmed = false, refunded = false;
    for (const QJsonValue &value : m_records) {
        const QJsonObject record = value.toObject();
        if (record.value("swapId") != swapId) continue;
        const QString event = record.value("event").toString();
        const QString chain = record.value("chain").toString();
        const QString artifact = record.value("artifactHash").toString();
        accepted |= event == "quote_accepted" && chain == "protocol"
            && artifact == quoteText;
        broadcast |= event == "pay_refund_broadcast" && chain == bitcoin.chain()
            && artifact == broadcastText;
        confirmed |= event == "pay_refund_confirmed" && chain == bitcoin.chain();
        refunded |= event == "xmz_refund_confirmed"
            && chain == principalRefund.chain() && artifact == xmzText;
    }
    if (!accepted || !broadcast || !confirmed || !refunded
        || hasEvent(m_records, swapId, "protocol_fee_earned")) return result;
    result.m_swapId = swapId;
    result.m_outcome = "refunded";
    result.m_fillReservationCommitment = quote.fillReservationCommitment();
    result.m_quoteCommitment = quote.commitment();
    result.m_commitment = refundedOutcomeCommitment(swapId,
        result.m_fillReservationCommitment, result.m_quoteCommitment, xmz,
        bitcoin.confirmationBasisCommitment(), bitcoin.broadcastCommitment());
    return result;
}

bool SettlementJournal::xmzEvidenceCurrent(const ConfirmationTracker &tracker,
    const QString &transactionId, const QString &blockHash) const
{
    const QVariantMap current = tracker.current("XMZ", transactionId);
    return ready() && current.value("valid").toBool()
        && current.value("confirmed").toBool()
        && !current.value("reorgDetected").toBool()
        && current.value("blockHash").toString() == blockHash;
}
