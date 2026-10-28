#ifndef MONZERO_BITCOIN_RAW_TRANSACTION_H
#define MONZERO_BITCOIN_RAW_TRANSACTION_H

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace BitcoinRawTransaction {
struct Result {
    bool valid = false;
    QJsonObject transaction;
    QStringList witness;
    QString txid, wtxid;
    quint64 virtualSize = 0;
    QString error;
};

// Strict parser for the single-input/single-output SegWit v0 recovery shape.
// It rejects non-minimal CompactSize encodings and trailing bytes, reconstructs
// the unsigned-template JSON, and derives txid/wtxid from serialized bytes.
Result parseRecovery(const QByteArray &rawTransactionHex);
}

#endif
