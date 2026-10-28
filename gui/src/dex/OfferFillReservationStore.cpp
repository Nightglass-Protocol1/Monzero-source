#include "OfferFillReservationStore.h"

#include <QCryptographicHash>
#include <QDataStream>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QHash>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStringList>
#include <limits>
#ifdef Q_OS_UNIX
#include <unistd.h>
#endif

namespace {
constexpr int maximumRecords = 10000;
constexpr qint64 maximumHoldSeconds = 15 * 60;
constexpr qint64 maximumFileSize = 16 * 1024 * 1024;
const QRegularExpression uuid(
    "^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$");
const QRegularExpression hex32("^[0-9a-f]{64}$");
const QRegularExpression hex64("^[0-9a-f]{128}$");

bool privateLocation(const QString &path)
{
    const QFileInfo file(path), parent(file.absolutePath());
    const auto shared = QFileDevice::ReadGroup | QFileDevice::WriteGroup | QFileDevice::ExeGroup
        | QFileDevice::ReadOther | QFileDevice::WriteOther | QFileDevice::ExeOther;
    if (path.isEmpty() || !parent.isDir() || parent.isSymLink()
        || (parent.permissions() & shared) || file.isSymLink()) return false;
#ifdef Q_OS_UNIX
    if (parent.ownerId() != geteuid() || (file.exists() && file.ownerId() != geteuid())) return false;
#endif
    return !file.exists() || (file.isFile() && !(file.permissions() & shared));
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

bool atomic(const QString &text, quint64 &value)
{
    bool ok = false;
    value = text.toULongLong(&ok);
    return ok && value > 0 && text == QString::number(value);
}

bool timeValue(const QJsonValue &value, qint64 &result)
{
    if (!value.isString()) return false;
    bool ok = false;
    result = value.toString().toLongLong(&ok);
    return ok && result >= 0 && value.toString() == QString::number(result);
}

QByteArray reservationCommitment(const QString &id, const QByteArray &match,
    const QByteArray &buy, const QByteArray &sell, const QString &amount,
    qint64 createdAt, qint64 expiresAt)
{
    QByteArray input("MONZERO-DEX-FILL-RESERVATION-V1\n");
    for (const QByteArray &value : {id.toUtf8(), match, buy, sell,
             amount.toLatin1(), QByteArray::number(createdAt),
             QByteArray::number(expiresAt)})
        input += field(value);
    return QCryptographicHash::hash(input, QCryptographicHash::Sha256);
}
}

OfferFillReservationStore::OfferFillReservationStore(QString path,
    QString expectedProtocol, QString expectedNetwork,
    const OfferBookStore &offerBook)
    : m_path(std::move(path)), m_expectedProtocol(std::move(expectedProtocol)),
      m_expectedNetwork(std::move(expectedNetwork)), m_offerBook(offerBook)
{
    if (!privateLocation(m_path)) {
        m_error = "Fill reservation store requires private regular-file storage";
        return;
    }
    m_path = QFileInfo(m_path).absoluteFilePath();
    m_lock.reset(new QLockFile(m_path + ".lock"));
    m_lock->setStaleLockTime(0);
    if (!m_lock->tryLock(0)) m_error = "Fill reservation store is already locked";
    else load();
}

bool OfferFillReservationStore::ready() const { return m_error.isEmpty(); }
QString OfferFillReservationStore::error() const { return m_error; }
QJsonArray OfferFillReservationStore::records() const { return m_records; }

QJsonObject OfferFillReservationStore::encodeOffer(
    const SignedOffer::Evidence &offer) const
{
    const auto terms = offer.terms();
    return {{"protocolId", terms.protocolId}, {"network", terms.network},
        {"offerId", terms.offerId}, {"pair", terms.pair}, {"side", terms.side},
        {"amountAtomic", terms.amountAtomic}, {"priceAtomic", terms.priceAtomic},
        {"createdAt", QString::number(terms.createdAt)},
        {"expiresAt", QString::number(terms.expiresAt)},
        {"makerPublicKey", QString::fromLatin1(offer.makerPublicKey().toHex())},
        {"signature", QString::fromLatin1(offer.signature().toHex())},
        {"commitment", QString::fromLatin1(offer.commitment().toHex())}};
}

bool OfferFillReservationStore::decodeOffer(const QJsonObject &object,
                                             SignedOffer::Evidence &offer) const
{
    if (object.size() != 12) return false;
    SignedOffer::Terms terms;
    terms.protocolId = object.value("protocolId").toString();
    terms.network = object.value("network").toString();
    terms.offerId = object.value("offerId").toString();
    terms.pair = object.value("pair").toString();
    terms.side = object.value("side").toString();
    terms.amountAtomic = object.value("amountAtomic").toString();
    terms.priceAtomic = object.value("priceAtomic").toString();
    if (!timeValue(object.value("createdAt"), terms.createdAt)
        || !timeValue(object.value("expiresAt"), terms.expiresAt)) return false;
    const QString key = object.value("makerPublicKey").toString();
    const QString signature = object.value("signature").toString();
    const QString commitment = object.value("commitment").toString();
    if (!hex32.match(key).hasMatch() || !hex64.match(signature).hasMatch()
        || !hex32.match(commitment).hasMatch()) return false;
    offer = SignedOffer::verify(terms, QByteArray::fromHex(key.toLatin1()),
        QByteArray::fromHex(signature.toLatin1()), m_expectedProtocol,
        m_expectedNetwork, terms.createdAt);
    return offer.accepted() && offer.commitment().toHex() == commitment.toLatin1();
}

OfferFillReservation::Evidence OfferFillReservationStore::evidence(
    const QJsonObject &record) const
{
    OfferFillReservation::Evidence result;
    const QString commitment = record.value("commitment").toString();
    if (!hex32.match(commitment).hasMatch()) return result;
    result.m_commitment = QByteArray::fromHex(commitment.toLatin1());
    result.m_reservationId = record.value("reservationId").toString();
    result.m_amountAtomic = record.value("amountAtomic").toString();
    timeValue(record.value("expiresAt"), result.m_expiresAt);
    result.m_details = {{"valid", true},
        {"reserved", record.value("state") == "reserved"},
        {"state", record.value("state").toString()},
        {"reservationId", result.m_reservationId},
        {"amountAtomic", result.m_amountAtomic},
        {"expiresAt", QString::number(result.m_expiresAt)},
        {"executable", false}};
    return result;
}

bool OfferFillReservationStore::validRecord(const QJsonObject &record) const
{
    if (record.size() != 12 || !uuid.match(record.value("reservationId").toString()).hasMatch()
        || !record.value("executable").isBool()
        || record.value("executable").toBool()
        || !record.value("buy").isObject() || !record.value("sell").isObject())
        return false;
    SignedOffer::Evidence buy, sell;
    if (!decodeOffer(record.value("buy").toObject(), buy)
        || !decodeOffer(record.value("sell").toObject(), sell)) return false;
    qint64 createdAt = 0, expiresAt = 0;
    quint64 requested = 0;
    const QString amount = record.value("amountAtomic").toString();
    const QString matchHex = record.value("matchCommitment").toString();
    const QString commitmentHex = record.value("commitment").toString();
    const QString state = record.value("state").toString();
    const QString quoteCommitment = record.value("quoteCommitment").toString();
    const QString outcomeCommitment = record.value("outcomeCommitment").toString();
    if (!atomic(amount, requested) || !timeValue(record.value("createdAt"), createdAt)
        || !timeValue(record.value("expiresAt"), expiresAt)
        || expiresAt <= createdAt || expiresAt - createdAt > maximumHoldSeconds
        || !hex32.match(matchHex).hasMatch() || !hex32.match(commitmentHex).hasMatch()
        || !QStringList{"reserved", "committed", "settled", "refunded", "released"}.contains(state)
        || ((state == "committed" || state == "settled" || state == "refunded")
            != hex32.match(quoteCommitment).hasMatch())
        || ((state == "settled" || state == "refunded")
            != hex32.match(outcomeCommitment).hasMatch())
        || ((state == "reserved" || state == "released")
            && !quoteCommitment.isEmpty()))
        return false;
    const auto crossed = OfferMatcher::cross(buy, sell, m_expectedProtocol,
        m_expectedNetwork, createdAt);
    if (!crossed.accepted() || crossed.commitment().toHex() != matchHex.toLatin1()
        || requested > crossed.maximumBaseAtomic().toULongLong()
        || expiresAt > crossed.validUntil()) return false;
    return reservationCommitment(record.value("reservationId").toString(),
        crossed.commitment(), buy.commitment(), sell.commitment(), amount,
        createdAt, expiresAt).toHex() == commitmentHex.toLatin1();
}

bool OfferFillReservationStore::load()
{
    if (!privateLocation(m_path)) {
        m_error = "Unsafe fill reservation storage"; return false;
    }
    QFile file(m_path);
    if (!file.exists()) return true;
    if (!file.open(QIODevice::ReadOnly) || file.size() > maximumFileSize) {
        m_error = "Cannot read fill reservation store"; return false;
    }
    QJsonParseError parse;
    const auto document = QJsonDocument::fromJson(file.readAll(), &parse);
    if (parse.error != QJsonParseError::NoError || !document.isObject()
        || document.object().size() != 2
        || (document.object().value("version").toInt(-1) != 1
            && document.object().value("version").toInt(-1) != 2)
        || !document.object().value("records").isArray()) {
        m_error = "Invalid fill reservation store"; return false;
    }
    auto records = document.object().value("records").toArray();
    if (document.object().value("version").toInt() == 1) {
        for (int i = 0; i < records.size(); ++i) {
            QJsonObject record = records.at(i).toObject();
            record["outcomeCommitment"] = QString();
            records[i] = record;
        }
    }
    if (records.size() > maximumRecords) {
        m_error = "Fill reservation store exceeds record limit"; return false;
    }
    QSet<QString> ids;
    QHash<QByteArray, quint64> capacities;
    for (const auto &value : records) {
        if (!value.isObject() || !validRecord(value.toObject())
            || ids.contains(value.toObject().value("reservationId").toString())) {
            m_error = "Invalid fill reservation record"; return false;
        }
        SignedOffer::Evidence buy, sell;
        if (!decodeOffer(value.toObject().value("buy").toObject(), buy)
            || !decodeOffer(value.toObject().value("sell").toObject(), sell)) {
            m_error = "Invalid fill reservation offer"; return false;
        }
        for (const auto &offer : {buy, sell}) {
            const quint64 capacity = offer.amountAtomic().toULongLong();
            if (capacities.contains(offer.commitment())
                && capacities.value(offer.commitment()) != capacity) {
                m_error = "Inconsistent fill capacity"; return false;
            }
            capacities.insert(offer.commitment(), capacity);
        }
        ids.insert(value.toObject().value("reservationId").toString());
    }
    m_records = records;
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    for (auto it = capacities.cbegin(); it != capacities.cend(); ++it) {
        if (!capacityAvailable(it.key(), it.value(), 0, now)) {
            m_error = "Persisted fills exceed offer capacity";
            m_records = {};
            return false;
        }
    }
    return true;
}

bool OfferFillReservationStore::save(const QJsonArray &next)
{
    if (!privateLocation(m_path)) {
        m_error = "Unsafe fill reservation storage"; return false;
    }
    QSaveFile file(m_path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly)
        || !file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner)) {
        m_error = "Cannot write fill reservation store"; return false;
    }
    const QByteArray bytes = QJsonDocument(QJsonObject{{"version", 2},
        {"records", next}}).toJson(QJsonDocument::Compact);
    if (bytes.size() > maximumFileSize || file.write(bytes) != bytes.size()
        || !file.commit()) {
        m_error = "Cannot commit fill reservation store"; return false;
    }
    m_records = next;
    return true;
}

bool OfferFillReservationStore::capacityAvailable(const QByteArray &offerCommitment,
    quint64 capacity, quint64 requested, qint64 now) const
{
    quint64 used = 0;
    const QString target = QString::fromLatin1(offerCommitment.toHex());
    for (const auto &value : m_records) {
        const auto record = value.toObject();
        const QString state = record.value("state").toString();
        qint64 expiry = 0;
        timeValue(record.value("expiresAt"), expiry);
        if (state == "released" || state == "refunded"
            || (state == "reserved" && expiry <= now)) continue;
        const auto buy = record.value("buy").toObject();
        const auto sell = record.value("sell").toObject();
        if (buy.value("commitment") != target && sell.value("commitment") != target)
            continue;
        quint64 amount = 0;
        if (!atomic(record.value("amountAtomic").toString(), amount)
            || used > std::numeric_limits<quint64>::max() - amount) return false;
        used += amount;
    }
    return used <= capacity && requested <= capacity - used;
}

OfferFillReservation::Evidence OfferFillReservationStore::reserve(
    const QString &reservationId, const SignedOffer::Evidence &buy,
    const SignedOffer::Evidence &sell, const QString &amountAtomic,
    qint64 expiresAt, qint64 now)
{
    OfferFillReservation::Evidence rejected;
    if (!ready() || !uuid.match(reservationId).hasMatch() || now < 0
        || expiresAt <= now || expiresAt - now > maximumHoldSeconds) return rejected;
    quint64 requested = 0;
    if (!atomic(amountAtomic, requested)) return rejected;
    const auto crossed = m_offerBook.cross(buy.offerId(), sell.offerId(), now);
    const auto direct = OfferMatcher::cross(buy, sell, m_expectedProtocol,
        m_expectedNetwork, now);
    if (!crossed.accepted() || !direct.accepted()
        || crossed.commitment() != direct.commitment()
        || expiresAt > crossed.validUntil()
        || requested > crossed.maximumBaseAtomic().toULongLong()) return rejected;
    const QByteArray commitment = reservationCommitment(reservationId,
        crossed.commitment(), buy.commitment(), sell.commitment(), amountAtomic,
        now, expiresAt);
    for (const auto &value : m_records) {
        const auto record = value.toObject();
        if (record.value("reservationId") != reservationId) continue;
        return record.value("state") == "reserved"
            && record.value("commitment") == QString::fromLatin1(commitment.toHex())
            ? evidence(record) : rejected;
    }
    if (m_records.size() >= maximumRecords
        || !capacityAvailable(buy.commitment(), buy.amountAtomic().toULongLong(),
                              requested, now)
        || !capacityAvailable(sell.commitment(), sell.amountAtomic().toULongLong(),
                              requested, now)) return rejected;
    QJsonObject record{{"reservationId", reservationId}, {"buy", encodeOffer(buy)},
        {"sell", encodeOffer(sell)},
        {"matchCommitment", QString::fromLatin1(crossed.commitment().toHex())},
        {"amountAtomic", amountAtomic}, {"createdAt", QString::number(now)},
        {"expiresAt", QString::number(expiresAt)}, {"state", "reserved"},
        {"quoteCommitment", QString()},
        {"outcomeCommitment", QString()},
        {"commitment", QString::fromLatin1(commitment.toHex())},
        {"executable", false}};
    QJsonArray next = m_records;
    next.append(record);
    return save(next) ? evidence(record) : rejected;
}

/* Terminal transitions live in OfferFillSettlement.cpp. */
#if 0
bool OfferFillReservationStore::finalizeSettled(
    const QString &reservationId, const QString &swapId,
    const AcceptedQuote &quote, const SettlementJournal &journal,
    const MonzeroSuccessTransactionEvidence::Evidence &xmzEvidence,
    const BitcoinRecoveryConfirmationTracker::Tracker &bitcoinTracker,
    const QByteArray &bitcoinBroadcastCommitment)
{
    const SettlementOutcome::Evidence outcome = journal.settledEvidence(swapId,
        quote, xmzEvidence, bitcoinTracker, bitcoinBroadcastCommitment);
    if (!ready() || !outcome.accepted() || outcome.outcome() != "settled")
        return false;
    QJsonArray next = m_records;
    for (int i = 0; i < next.size(); ++i) {
        QJsonObject record = next.at(i).toObject();
        if (record.value("reservationId") != reservationId) continue;
        if (record.value("state") == "settled")
            return record.value("outcomeCommitment")
                == QString::fromLatin1(outcome.commitment().toHex());
        if (record.value("state") != "committed"
            || record.value("commitment")
                != QString::fromLatin1(outcome.fillReservationCommitment().toHex())
            || record.value("quoteCommitment")
                != QString::fromLatin1(outcome.quoteCommitment().toHex()))
            return false;
        record["state"] = "settled";
        record["outcomeCommitment"] =
            QString::fromLatin1(outcome.commitment().toHex());
        next[i] = record;
        return save(next);
    }
    return false;
}

bool OfferFillReservationStore::finalizeRefunded(
    const QString &reservationId, const QString &swapId,
    const AcceptedQuote &quote, const SettlementJournal &journal,
    const MonzeroRefundTransactionEvidence::Evidence &principalRefund,
    const MonzeroRefundTransactionEvidence::Evidence &feeRefund,
    const BitcoinRecoveryConfirmationTracker::Tracker &bitcoinTracker,
    const QByteArray &bitcoinBroadcastCommitment)
{
    const SettlementOutcome::Evidence outcome = journal.refundedEvidence(swapId,
        quote, principalRefund, feeRefund, bitcoinTracker,
        bitcoinBroadcastCommitment);
    if (!ready() || !outcome.accepted() || outcome.outcome() != "refunded")
        return false;
    QJsonArray next = m_records;
    for (int i = 0; i < next.size(); ++i) {
        QJsonObject record = next.at(i).toObject();
        if (record.value("reservationId") != reservationId) continue;
        if (record.value("state") == "refunded")
            return record.value("outcomeCommitment")
                == QString::fromLatin1(outcome.commitment().toHex());
        if (record.value("state") != "committed"
            || record.value("commitment")
                != QString::fromLatin1(outcome.fillReservationCommitment().toHex())
            || record.value("quoteCommitment")
                != QString::fromLatin1(outcome.quoteCommitment().toHex()))
            return false;
        record["state"] = "refunded";
        record["outcomeCommitment"] =
            QString::fromLatin1(outcome.commitment().toHex());
        next[i] = record;
        return save(next);
    }
    return false;
}
#endif

bool OfferFillReservationStore::commit(const QString &reservationId,
                                        const AcceptedQuote &quote, qint64 now)
{
    if (!ready() || now < 0 || !quote.mutuallyAuthorized()) return false;
    QJsonArray next = m_records;
    for (int i = 0; i < next.size(); ++i) {
        auto record = next.at(i).toObject();
        if (record.value("reservationId") != reservationId) continue;
        if (record.value("state") == "committed")
            return record.value("quoteCommitment")
                == QString::fromLatin1(quote.commitment().toHex());
        qint64 expiry = 0;
        if (record.value("state") != "reserved"
            || !timeValue(record.value("expiresAt"), expiry) || now >= expiry)
            return false;
        SignedOffer::Evidence buy, sell;
        if (!decodeOffer(record.value("buy").toObject(), buy)
            || !decodeOffer(record.value("sell").toObject(), sell)) return false;
        const auto crossed = m_offerBook.cross(buy.offerId(), sell.offerId(), now);
        if (!crossed.accepted()
            || crossed.commitment().toHex() != record.value("matchCommitment").toString().toLatin1())
            return false;
        const auto terms = quote.terms();
        if (quote.fillReservationCommitment().toHex()
                != record.value("commitment").toString().toLatin1()
            || terms.quoteExpiry <= now
            || !((terms.makerKey == buy.makerPublicKey()
                    && terms.takerKey == sell.makerPublicKey())
                 || (terms.makerKey == sell.makerPublicKey()
                    && terms.takerKey == buy.makerPublicKey())))
            return false;
        const QString baseAsset = crossed.pair().section('/', 0, 0);
        if (!((terms.payAsset == baseAsset && terms.receiveAsset == "XMZ")
              || (terms.payAsset == "XMZ" && terms.receiveAsset == baseAsset)))
            return false;
        // The maker/taker identity check above only proves the two signers are
        // the crossed offers' two makers, in either order. It does not say
        // which one is funding the pay leg. Pin that down explicitly here so a
        // future signer can trust terms.payerKey alone and never has to
        // re-derive funding authority from asset-direction labels.
        const QByteArray expectedPayerKey = terms.payAsset == baseAsset
            ? sell.makerPublicKey() : buy.makerPublicKey();
        if (terms.payerKey != expectedPayerKey) return false;
        const QString baseAmount = terms.payAsset == baseAsset
            ? terms.payAmountAtomic : terms.receiveAmountAtomic;
        const QString xmzAmount = terms.payAsset == "XMZ"
            ? terms.payAmountAtomic : terms.receiveAmountAtomic;
        quint64 base = 0, xmz = 0, minimumPrice = 0, maximumPrice = 0;
        if (!atomic(baseAmount, base) || !atomic(xmzAmount, xmz)
            || !atomic(crossed.minimumPriceAtomic(), minimumPrice)
            || !atomic(crossed.maximumPriceAtomic(), maximumPrice)
            || baseAmount != record.value("amountAtomic").toString())
            return false;
        constexpr quint64 baseScale = 100000000;
        // Compare exact rational prices: flooring the seller's minimum would
        // admit a quote below the signed ask by a fractional atomic unit.
        const unsigned __int128 quotedGross =
            static_cast<unsigned __int128>(xmz) * baseScale;
        if (quotedGross < static_cast<unsigned __int128>(base) * minimumPrice
            || quotedGross > static_cast<unsigned __int128>(base) * maximumPrice)
            return false;
        record["state"] = "committed";
        record["quoteCommitment"] = QString::fromLatin1(quote.commitment().toHex());
        next[i] = record;
        return save(next);
    }
    return false;
}

OfferFillReservation::Evidence OfferFillReservationStore::recoverCommitted(
    const QString &reservationId, const RecoveredMutualQuote &quote, qint64 now) const
{
    if (!ready() || !quote.valid() || now < quote.acceptedAt()) return {};
    const auto terms = quote.terms();
    const auto rebuilt = SettlementTerms::build(terms, quote.acceptedAt());
    if (!rebuilt.valid || rebuilt.commitment != quote.commitment()) return {};
    for (const auto &value : m_records) {
        const auto record = value.toObject();
        if (record.value("reservationId") != reservationId) continue;
        if (record.value("state") != "committed"
            || record.value("quoteCommitment") != QString::fromLatin1(quote.commitment().toHex())
            || record.value("commitment")
                != QString::fromLatin1(terms.offerFillReservationCommitment.toHex())) return {};
        auto result = evidence(record);
        result.m_details["recoveryOnly"] = true;
        return result;
    }
    return {};
}

bool OfferFillReservationStore::release(const QString &reservationId)
{
    if (!ready()) return false;
    QJsonArray next = m_records;
    for (int i = 0; i < next.size(); ++i) {
        auto record = next.at(i).toObject();
        if (record.value("reservationId") != reservationId) continue;
        if (record.value("state") == "released") return true;
        if (record.value("state") != "reserved") return false;
        record["state"] = "released";
        next[i] = record;
        return save(next);
    }
    return false;
}
