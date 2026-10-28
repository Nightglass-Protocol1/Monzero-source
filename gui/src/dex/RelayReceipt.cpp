#include "RelayReceipt.h"
#include <QJsonDocument>
#include <QJsonObject>

namespace {
QByteArray body(const RelayEnvelope::Evidence &message,
                const QByteArray &receiver, qint64 acceptedAt)
{
    return QJsonDocument(QJsonObject{
        {"version", 1}, {"protocol", message.terms().protocol},
        {"network", message.terms().network},
        {"relay", QString::fromLatin1(message.commitment().toHex())},
        {"sender", QString::fromLatin1(message.senderPublicKey().toHex())},
        {"receiver", QString::fromLatin1(receiver.toHex())},
        {"sequence", QString::number(message.terms().sequence)},
        {"acceptedAt", QString::number(acceptedAt)}}).toJson(QJsonDocument::Compact);
}
QByteArray payload(const QByteArray &body)
{
    return QByteArray("MONZERO-DEX-RECEIPT-V1\n") + body;
}
}

namespace RelayReceipt {
QByteArray create(const RelayIngress::Evidence &accepted,
                  const RelayEnvelope::Evidence &message,
                  const OfferIdentity &receiver, qint64 acceptedAt)
{
    if (!accepted.accepted() || !message.accepted()
        || accepted.relayCommitment() != message.commitment()
        || accepted.artifactCommitment() != message.terms().artifact
        || accepted.action() != message.terms().type
        || acceptedAt < message.terms().createdAt) return {};
    const auto canonical = body(message, receiver.publicKey(), acceptedAt);
    const auto signature = receiver.sign(payload(canonical), message.terms().network.toUtf8());
    if (signature.size() != 64) return {};
    return QJsonDocument(QJsonObject{
        {"body", QJsonDocument::fromJson(canonical).object()},
        {"signature", QString::fromLatin1(signature.toHex())}}).toJson(QJsonDocument::Compact);
}

bool verify(const QByteArray &receipt, const RelayEnvelope::Evidence &message,
            const QByteArray &expectedReceiver, qint64 now)
{
    if (!message.accepted() || expectedReceiver.size() != 32
        || expectedReceiver == QByteArray(32, '\0') || receipt.size() > 2048
        || now < message.terms().createdAt) return false;
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(receipt, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()
        || document.toJson(QJsonDocument::Compact) != receipt) return false;
    const auto root = document.object();
    const auto object = root.value("body").toObject();
    const auto timestamp = object.value("acceptedAt").toString();
    bool ok = false;
    const auto acceptedAt = timestamp.toLongLong(&ok);
    if (!ok || timestamp != QString::number(acceptedAt)
        || acceptedAt < message.terms().createdAt || acceptedAt > now) return false;
    const auto canonical = body(message, expectedReceiver, acceptedAt);
    const auto hex = root.value("signature").toString().toLatin1();
    const auto signature = QByteArray::fromHex(hex);
    return root.size() == 2 && signature.size() == 64 && signature.toHex() == hex
        && QJsonDocument(object).toJson(QJsonDocument::Compact) == canonical
        && OfferIdentity::verify(payload(canonical), message.terms().network.toUtf8(),
                                 expectedReceiver, signature);
}
}
