#ifndef MONZERO_OFFER_BOOK_STORE_H
#define MONZERO_OFFER_BOOK_STORE_H

#include "SignedOffer.h"
#include "SignedOfferCancellation.h"
#include "OfferMatcher.h"

#include <QJsonArray>
#include <QLockFile>
#include <QString>
#include <QVariantMap>
#include <memory>

namespace RelayIngress { class Coordinator; }
class OfferBookStore final
{
public:
    OfferBookStore(QString path, QString expectedProtocol, QString expectedNetwork);
    bool ready() const;
    QString error() const;
    bool insert(const SignedOffer::Evidence &evidence, qint64 now);
    bool cancel(const SignedOfferCancellation::Evidence &evidence, qint64 now);
    bool pruneExpired(qint64 now);
    OfferMatcher::Evidence cross(const QString &buyOfferId,
                                 const QString &sellOfferId, qint64 now) const;
    QJsonArray active(qint64 now) const;
    QJsonArray records() const;
    QJsonArray cancellations() const;
    static QVariantMap readSnapshot(const QString &path, const QString &protocol,
                                    const QString &network, qint64 now);

private:
    friend class RelayIngress::Coordinator;
    struct SnapshotTag {};
    OfferBookStore(QString path, QString protocol, QString network, SnapshotTag);
    bool load(bool allowMissing = true);
    bool save(const QJsonArray &nextRecords, const QJsonArray &nextCancellations);
    QJsonObject encode(const SignedOffer::Evidence &evidence) const;
    bool decode(const QJsonObject &record, SignedOffer::Evidence &evidence) const;
    QJsonObject encodeCancellation(
        const SignedOfferCancellation::Evidence &evidence) const;
    bool decodeCancellation(const QJsonObject &record,
        SignedOfferCancellation::Evidence &evidence) const;

    QString m_path;
    QString m_expectedProtocol;
    QString m_expectedNetwork;
    QString m_error;
    QJsonArray m_records;
    QJsonArray m_cancellations;
    std::unique_ptr<QLockFile> m_lock;
};

#endif
