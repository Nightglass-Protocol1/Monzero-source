#ifndef MONZERO_NATIVE_QUOTE_REPLAY_H
#define MONZERO_NATIVE_QUOTE_REPLAY_H
#include <QByteArray>
#include <QString>
#include <QJsonObject>

namespace NativeQuoteReplay {
// Reserve fresh mutually signed commercial terms in trusted private storage.
// Not funding authority. Any false result forbids starting a trade; partial
// records deliberately remain blocked for reconciliation, never auto-deleted.
// Root and ancestors must remain exclusively controlled and must not be reset.
bool reserve(const QString &privateDirectory, const QByteArray &payload, qint64 now,
    const QByteArray &btcSignature, const QByteArray &xmzSignature,
    const QByteArray &expectedBtcFunder, const QByteArray &expectedXmzFunder);
// Read-only historical evidence; verifies signatures at recorded acceptance
// time, not as a fresh quote. Output changes only on success. Never permits retry.
bool recover(const QString &privateDirectory, const QString &quoteId, qint64 now,
    const QByteArray &expectedBtcFunder, const QByteArray &expectedXmzFunder,
    QJsonObject &output);
}
#endif
