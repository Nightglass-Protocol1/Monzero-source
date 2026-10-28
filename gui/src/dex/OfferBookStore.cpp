#include "OfferBookStore.h"

#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>

namespace {
constexpr int maximumRecords = 10000;
constexpr qint64 maximumFileSize = 8 * 1024 * 1024;
const QRegularExpression hex32("^[0-9a-f]{64}$");
const QRegularExpression hex64("^[0-9a-f]{128}$");

bool canonicalTime(const QJsonValue &value, qint64 &result)
{
    if (!value.isString()) return false;
    bool ok = false;
    result = value.toString().toLongLong(&ok);
    return ok && result >= 0 && value.toString() == QString::number(result);
}
}

OfferBookStore::OfferBookStore(QString path, QString expectedProtocol,
                               QString expectedNetwork)
    : m_path(std::move(path)), m_expectedProtocol(std::move(expectedProtocol)),
      m_expectedNetwork(std::move(expectedNetwork))
{
    m_lock.reset(new QLockFile(m_path + ".lock"));
    m_lock->setStaleLockTime(0);
    if (!m_lock->tryLock(0)) m_error = "Offer book is already locked";
    else load();
}

bool OfferBookStore::ready() const { return m_error.isEmpty(); }
OfferBookStore::OfferBookStore(QString path, QString protocol, QString network, SnapshotTag)
    : m_path(std::move(path)), m_expectedProtocol(std::move(protocol)),
      m_expectedNetwork(std::move(network))
{
    const QFileInfo info(m_path);
    if (!info.isFile() || info.isSymLink()) m_error = "Snapshot must be an existing regular file";
    else load(false);
}

QVariantMap OfferBookStore::readSnapshot(const QString &path, const QString &protocol,
                                       const QString &network, qint64 now)
{
    QVariantMap result{{"valid", false}, {"executable", false},
        {"liquidityVerified", false}, {"offers", QVariantList{}}};
    if (now < 0 || protocol.isEmpty() || network.isEmpty()) return result;
    OfferBookStore snapshot(path, protocol, network, SnapshotTag{});
    if (!snapshot.ready()) { result["error"] = snapshot.error(); return result; }
    const auto offers = snapshot.active(now);
    QVariantList displayed;
    for (int i = 0; i < offers.size() && i < 200; ++i)
        displayed.append(offers.at(i).toObject().toVariantMap());
    result["valid"] = true;
    result["signaturesVerified"] = true;
    result["network"] = network;
    result["observedAt"] = QString::number(now);
    result["activeCount"] = offers.size();
    result["truncated"] = offers.size() > displayed.size();
    result["offers"] = displayed;
    return result;
}
QString OfferBookStore::error() const { return m_error; }
QJsonArray OfferBookStore::records() const { return m_records; }
QJsonArray OfferBookStore::cancellations() const { return m_cancellations; }

QJsonObject OfferBookStore::encode(const SignedOffer::Evidence &evidence) const
{
    const auto terms = evidence.terms();
    return {{"protocolId", terms.protocolId}, {"network", terms.network},
        {"offerId", terms.offerId}, {"pair", terms.pair}, {"side", terms.side},
        {"amountAtomic", terms.amountAtomic}, {"priceAtomic", terms.priceAtomic},
        {"createdAt", QString::number(terms.createdAt)},
        {"expiresAt", QString::number(terms.expiresAt)},
        {"makerPublicKey", QString::fromLatin1(evidence.makerPublicKey().toHex())},
        {"signature", QString::fromLatin1(evidence.signature().toHex())},
        {"commitment", QString::fromLatin1(evidence.commitment().toHex())}};
}

bool OfferBookStore::decode(const QJsonObject &record,
                            SignedOffer::Evidence &evidence) const
{
    if (record.size() != 12) return false;
    SignedOffer::Terms terms;
    terms.protocolId = record.value("protocolId").toString();
    terms.network = record.value("network").toString();
    terms.offerId = record.value("offerId").toString();
    terms.pair = record.value("pair").toString();
    terms.side = record.value("side").toString();
    terms.amountAtomic = record.value("amountAtomic").toString();
    terms.priceAtomic = record.value("priceAtomic").toString();
    if (!canonicalTime(record.value("createdAt"), terms.createdAt)
        || !canonicalTime(record.value("expiresAt"), terms.expiresAt)) return false;

    const QString publicHex = record.value("makerPublicKey").toString();
    const QString signatureHex = record.value("signature").toString();
    const QString commitmentHex = record.value("commitment").toString();
    if (!hex32.match(publicHex).hasMatch()
        || !hex64.match(signatureHex).hasMatch()
        || !hex32.match(commitmentHex).hasMatch()) return false;

    evidence = SignedOffer::verify(terms, QByteArray::fromHex(publicHex.toLatin1()),
        QByteArray::fromHex(signatureHex.toLatin1()), m_expectedProtocol,
        m_expectedNetwork, terms.createdAt);
    return evidence.accepted()
        && evidence.commitment().toHex() == commitmentHex.toLatin1();
}

QJsonObject OfferBookStore::encodeCancellation(
    const SignedOfferCancellation::Evidence &evidence) const
{
    const auto terms = evidence.terms();
    return {{"protocolId", terms.protocolId}, {"network", terms.network},
        {"offerId", terms.offerId},
        {"offerCommitment", QString::fromLatin1(terms.offerCommitment.toHex())},
        {"cancelledAt", QString::number(terms.cancelledAt)},
        {"makerPublicKey", QString::fromLatin1(evidence.makerPublicKey().toHex())},
        {"signature", QString::fromLatin1(evidence.signature().toHex())},
        {"commitment", QString::fromLatin1(evidence.commitment().toHex())}};
}

bool OfferBookStore::decodeCancellation(const QJsonObject &record,
    SignedOfferCancellation::Evidence &evidence) const
{
    if (record.size() != 8) return false;
    SignedOfferCancellation::Terms terms;
    terms.protocolId = record.value("protocolId").toString();
    terms.network = record.value("network").toString();
    terms.offerId = record.value("offerId").toString();
    const QString offerCommitmentHex = record.value("offerCommitment").toString();
    const QString publicHex = record.value("makerPublicKey").toString();
    const QString signatureHex = record.value("signature").toString();
    const QString commitmentHex = record.value("commitment").toString();
    if (!canonicalTime(record.value("cancelledAt"), terms.cancelledAt)
        || !hex32.match(offerCommitmentHex).hasMatch()
        || !hex32.match(publicHex).hasMatch()
        || !hex64.match(signatureHex).hasMatch()
        || !hex32.match(commitmentHex).hasMatch()) return false;
    terms.offerCommitment = QByteArray::fromHex(offerCommitmentHex.toLatin1());
    evidence = SignedOfferCancellation::verify(terms,
        QByteArray::fromHex(publicHex.toLatin1()),
        QByteArray::fromHex(signatureHex.toLatin1()), m_expectedProtocol,
        m_expectedNetwork, terms.cancelledAt);
    return evidence.accepted()
        && evidence.commitment().toHex() == commitmentHex.toLatin1();
}

bool OfferBookStore::load(bool allowMissing)
{
    QFile file(m_path);
    if (!file.exists()) {
        if (allowMissing) return true;
        m_error = "Offer snapshot no longer exists";
        return false;
    }
    if (!file.open(QIODevice::ReadOnly) || file.size() > maximumFileSize) {
        m_error = "Cannot read offer book";
        return false;
    }
    QJsonParseError parse;
    const auto bytes = file.read(maximumFileSize + 1);
    if (bytes.size() > maximumFileSize || !file.atEnd()) {
        m_error = "Offer book exceeds file limit";
        return false;
    }
    const auto document = QJsonDocument::fromJson(bytes, &parse);
    if (parse.error != QJsonParseError::NoError || !document.isObject()
        || document.object().size() != 3
        || document.object().value("version").toInt(-1) != 2
        || !document.object().value("records").isArray()
        || !document.object().value("cancellations").isArray()) {
        m_error = "Invalid offer book";
        return false;
    }
    const QJsonArray records = document.object().value("records").toArray();
    const QJsonArray cancellations = document.object().value("cancellations").toArray();
    if (records.size() > maximumRecords || cancellations.size() > maximumRecords) {
        m_error = "Offer book exceeds record limit";
        return false;
    }
    QSet<QString> ids;
    QHash<QString, SignedOffer::Evidence> offers;
    for (const auto &value : records) {
        if (!value.isObject()) { m_error = "Invalid offer record"; return false; }
        SignedOffer::Evidence evidence;
        if (!decode(value.toObject(), evidence) || ids.contains(evidence.offerId())) {
            m_error = "Invalid offer record";
            return false;
        }
        ids.insert(evidence.offerId());
        offers.insert(evidence.offerId(), evidence);
    }
    QSet<QString> cancelledIds;
    for (const auto &value : cancellations) {
        if (!value.isObject()) { m_error = "Invalid cancellation record"; return false; }
        SignedOfferCancellation::Evidence cancellation;
        if (!decodeCancellation(value.toObject(), cancellation)
            || cancelledIds.contains(cancellation.offerId())
            || !offers.contains(cancellation.offerId())) {
            m_error = "Invalid cancellation record";
            return false;
        }
        const auto offer = offers.value(cancellation.offerId());
        if (cancellation.offerCommitment() != offer.commitment()
            || cancellation.makerPublicKey() != offer.makerPublicKey()
            || cancellation.cancelledAt() < offer.createdAt()
            || cancellation.cancelledAt() >= offer.expiresAt()) {
            m_error = "Cancellation does not match its offer";
            return false;
        }
        cancelledIds.insert(cancellation.offerId());
    }
    m_records = records;
    m_cancellations = cancellations;
    return true;
}

bool OfferBookStore::save(const QJsonArray &nextRecords,
                          const QJsonArray &nextCancellations)
{
    QSaveFile file(m_path);
    if (!file.open(QIODevice::WriteOnly)) {
        m_error = "Cannot write offer book";
        return false;
    }
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    const QByteArray bytes = QJsonDocument(QJsonObject{{"version", 2},
        {"records", nextRecords}, {"cancellations", nextCancellations}})
        .toJson(QJsonDocument::Compact);
    if (bytes.size() > maximumFileSize || file.write(bytes) != bytes.size()
        || !file.commit()) {
        m_error = "Cannot commit offer book";
        return false;
    }
    m_records = nextRecords;
    m_cancellations = nextCancellations;
    return true;
}

bool OfferBookStore::insert(const SignedOffer::Evidence &candidate, qint64 now)
{
    if (!ready() || now < 0 || !candidate.accepted()) return false;
    const auto verified = SignedOffer::verify(candidate.terms(),
        candidate.makerPublicKey(), candidate.signature(), m_expectedProtocol,
        m_expectedNetwork, now);
    if (!verified.accepted() || verified.commitment() != candidate.commitment())
        return false;
    const QString commitment = QString::fromLatin1(verified.commitment().toHex());
    for (const auto &value : m_records) {
        const auto record = value.toObject();
        if (record.value("offerId") != verified.offerId()) continue;
        return record.value("commitment") == commitment;
    }
    if (m_records.size() >= maximumRecords) return false;
    QJsonArray next = m_records;
    next.append(encode(verified));
    return save(next, m_cancellations);
}

bool OfferBookStore::cancel(const SignedOfferCancellation::Evidence &candidate,
                            qint64 now)
{
    if (!ready() || now < 0 || !candidate.accepted()) return false;
    const auto verified = SignedOfferCancellation::verify(candidate.terms(),
        candidate.makerPublicKey(), candidate.signature(), m_expectedProtocol,
        m_expectedNetwork, now);
    if (!verified.accepted() || verified.commitment() != candidate.commitment())
        return false;

    SignedOffer::Evidence offer;
    bool found = false;
    for (const auto &value : m_records) {
        SignedOffer::Evidence decoded;
        if (!decode(value.toObject(), decoded)) return false;
        if (decoded.offerId() == verified.offerId()) {
            offer = decoded;
            found = true;
            break;
        }
    }
    if (!found || verified.offerCommitment() != offer.commitment()
        || verified.makerPublicKey() != offer.makerPublicKey()
        || verified.cancelledAt() < offer.createdAt()
        || verified.cancelledAt() >= offer.expiresAt()) return false;

    const QString commitment = QString::fromLatin1(verified.commitment().toHex());
    for (const auto &value : m_cancellations) {
        const auto record = value.toObject();
        if (record.value("offerId") != verified.offerId()) continue;
        return record.value("commitment") == commitment;
    }
    QJsonArray next = m_cancellations;
    next.append(encodeCancellation(verified));
    return save(m_records, next);
}

bool OfferBookStore::pruneExpired(qint64 now)
{
    if (!ready() || now < 0) return false;
    QJsonArray next;
    QSet<QString> retainedIds;
    for (const auto &value : m_records) {
        qint64 expiry = 0;
        if (!canonicalTime(value.toObject().value("expiresAt"), expiry)) return false;
        if (expiry > now) {
            next.append(value);
            retainedIds.insert(value.toObject().value("offerId").toString());
        }
    }
    QJsonArray nextCancellations;
    for (const auto &value : m_cancellations)
        if (retainedIds.contains(value.toObject().value("offerId").toString()))
            nextCancellations.append(value);
    return (next.size() == m_records.size()
            && nextCancellations.size() == m_cancellations.size())
        || save(next, nextCancellations);
}

QJsonArray OfferBookStore::active(qint64 now) const
{
    QJsonArray result;
    if (!ready() || now < 0) return result;
    QSet<QString> cancelledIds;
    for (const auto &value : m_cancellations) {
        qint64 cancelledAt = 0;
        if (canonicalTime(value.toObject().value("cancelledAt"), cancelledAt)
            && cancelledAt <= now)
            cancelledIds.insert(value.toObject().value("offerId").toString());
    }
    for (const auto &value : m_records) {
        qint64 createdAt = 0, expiry = 0;
        if (canonicalTime(value.toObject().value("createdAt"), createdAt)
            && createdAt <= now
            && canonicalTime(value.toObject().value("expiresAt"), expiry)
            && expiry > now
            && !cancelledIds.contains(value.toObject().value("offerId").toString()))
            result.append(value);
    }
    return result;
}

OfferMatcher::Evidence OfferBookStore::cross(const QString &buyOfferId,
                                              const QString &sellOfferId,
                                              qint64 now) const
{
    SignedOffer::Evidence buy, sell;
    bool foundBuy = false, foundSell = false;
    for (const auto &value : active(now)) {
        SignedOffer::Evidence decoded;
        if (!decode(value.toObject(), decoded)) return {};
        if (decoded.offerId() == buyOfferId) { buy = decoded; foundBuy = true; }
        if (decoded.offerId() == sellOfferId) { sell = decoded; foundSell = true; }
    }
    if (!foundBuy || !foundSell) return {};
    return OfferMatcher::cross(buy, sell, m_expectedProtocol, m_expectedNetwork, now);
}
