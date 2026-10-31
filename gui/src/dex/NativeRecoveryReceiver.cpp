#include "NativeRecoveryReceiver.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QUuid>

namespace {
bool hash(const QString &s) {
    static const QRegularExpression re("\\A[0-9a-f]{64}\\z");
    return re.match(s).hasMatch();
}
bool positive(const QString &s, quint64 &n) {
    static const QRegularExpression re("\\A[1-9][0-9]{0,19}\\z");
    bool ok = false;
    n = s.toULongLong(&ok);
    return ok && re.match(s).hasMatch();
}
QVariantMap unresolved() {
    return {{"valid", false}, {"status", "unresolved"}, {"retryAllowed", false},
        {"executable", false}, {"transportAuthenticated", false}};
}
}

void NativeRecoveryReceiver::cancel() { m_pending = false; m_id.clear(); m_current.clear(); }

QString NativeRecoveryReceiver::begin(const Expected &expected, int timeoutMs) {
    cancel();
    quint64 minimum = 0;
    static const QRegularExpression address("\\A[1-9A-HJ-NP-Za-km-z]{90,110}\\z");
    if (timeoutMs <= 0 || timeoutMs > 30000 || !hash(expected.genesis)
        || !hash(expected.lockTxid) || !hash(expected.txid)
        || !address.match(expected.wallet).hasMatch()
        || !positive(expected.minimumConfirmations, minimum)) return {};
    m_expected = expected;
    m_id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_deadline = QDeadlineTimer(timeoutMs, Qt::PreciseTimer);
    m_pending = true;
    return m_id;
}

QVariantMap NativeRecoveryReceiver::current() const {
    return m_current.isEmpty() || m_deadline.hasExpired() ? unresolved() : m_current;
}

QVariantMap NativeRecoveryReceiver::accept(const QByteArray &json) {
    const bool pending = m_pending;
    m_pending = false;
    m_current.clear();
    if (!pending || m_deadline.hasExpired() || json.size() > 2048) return unresolved();
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(json, &error);
    // Wire format is sorted-key compact ASCII JSON, without a trailing newline.
    // Exact reserialization also rejects duplicate keys and alternate spellings.
    if (error.error != QJsonParseError::NoError || !document.isObject()
        || document.toJson(QJsonDocument::Compact) != json) return unresolved();
    const auto object = document.object();
    if (object.size() != 11) return unresolved();
    for (const auto field : {"schema", "requestId", "genesis", "wallet", "lockTxid",
            "txid", "confirmations", "status", "source"})
        if (!object.value(field).isString()) return unresolved();
    for (const auto field : {"executable", "retryAllowed"})
        if (!object.value(field).isBool() || object.value(field).toBool()) return unresolved();
    const auto value = [&](const char *field) { return object.value(field).toString(); };
    quint64 confirmations = 0, minimum = 0;
    if (value("schema") != "monzero-native-recovery-v1" || value("requestId") != m_id
        || value("source") != "local-wallet-reconciliation" || value("status") != "confirmed"
        || value("genesis") != m_expected.genesis || value("wallet") != m_expected.wallet
        || value("lockTxid") != m_expected.lockTxid || value("txid") != m_expected.txid
        || !positive(value("confirmations"), confirmations)
        || !positive(m_expected.minimumConfirmations, minimum) || confirmations < minimum
        || m_deadline.hasExpired()) return unresolved();
    m_current = unresolved();
    m_current["valid"] = true;
    m_current["status"] = "confirmed";
    m_current["txid"] = m_expected.txid;
    m_current["confirmations"] = value("confirmations");
    m_current["label"] = "Confirmed by local wallet (display only; transport not authenticated)";
    return m_current;
}
