#include "NativeFeePreviewReceiver.h"
#include "DexUtils.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>
#include <QUuid>
#include <limits>

namespace {
bool integer(const QString &text, quint64 &value) {
    static const QRegularExpression digits("^(0|[1-9][0-9]{0,19})$");
    if (!digits.match(text).hasMatch()) return false;
    bool ok = false;
    value = text.toULongLong(&ok);
    return ok;
}
bool hash(const QString &text) {
    static const QRegularExpression pattern("^[0-9a-f]{64}$");
    return pattern.match(text).hasMatch();
}
bool addressShape(const QString &text) {
    static const QRegularExpression pattern("^[1-9A-HJ-NP-Za-km-z]{90,110}$");
    return pattern.match(text).hasMatch();
}
// QJsonDocument keeps the last duplicate. Scan validated JSON tokens to reject it,
// including escaped spellings of the same key.
bool uniqueKeys(const QByteArray &json) {
    QSet<QString> keys;
    for (int i = 0; i < json.size(); ++i) {
        if (json.at(i) != '"') continue;
        const int start = i++;
        while (i < json.size() && json.at(i) != '"') {
            if (json.at(i) == '\\') ++i;
            ++i;
        }
        if (i >= json.size()) return false;
        int after = i + 1;
        while (after < json.size() && (json.at(after) == ' ' || json.at(after) == '\n'
            || json.at(after) == '\r' || json.at(after) == '\t')) ++after;
        if (after >= json.size() || json.at(after) != ':') continue;
        const auto key = QJsonDocument::fromJson("[" + json.mid(start, i-start+1) + "]").array().at(0).toString();
        if (keys.contains(key)) return false;
        keys.insert(key);
    }
    return true;
}
}

void NativeFeePreviewReceiver::cancel() { m_pending = false; m_requestId.clear(); m_current.clear(); }

QVariantMap NativeFeePreviewReceiver::current() const {
    if (m_current.isEmpty() || m_deadline.hasExpired())
        return {{"valid",false},{"executable",false},{"transportAuthenticated",false},
            {"error","No current fee preview"}};
    return m_current;
}

bool NativeFeePreviewReceiver::validExpected(const Expected &expected, int timeoutMs) {
    quint64 amount = 0, account = 0, priority = 0;
    if (timeoutMs <= 0 || timeoutMs > 30000 || !addressShape(expected.wallet)
        || !addressShape(expected.destination) || !hash(expected.genesis)
        || !integer(expected.amountAtoms, amount) || amount == 0
        || !integer(expected.account, account) || account > std::numeric_limits<quint32>::max()
        || !integer(expected.priority, priority) || priority < 1 || priority > 4) return false;
    return true;
}

QString NativeFeePreviewReceiver::begin(const Expected &expected, int timeoutMs) {
    cancel();
    if (!validExpected(expected,timeoutMs)) return {};
    m_expected = expected;
    m_requestId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_deadline = QDeadlineTimer(timeoutMs, Qt::PreciseTimer);
    m_pending = true;
    return m_requestId;
}

QVariantMap NativeFeePreviewReceiver::accept(const QByteArray &json) {
    QVariantMap result{{"valid", false}, {"executable", false}, {"transportAuthenticated", false},
        {"error", "Fee preview does not match the current request"}};
    const bool pending = m_pending;
    m_current.clear();
    m_pending = false; // One response consumes the request, including malformed replies.
    if (!pending || m_deadline.hasExpired()) { result["error"] = "No current fee preview request"; return result; }
    if (json.size() > 2048) { result["error"] = "Oversized fee preview"; return result; }
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(json, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject() || !uniqueKeys(json)) {
        result["error"] = "Invalid fee preview JSON"; return result;
    }
    const auto object = document.object();
    const QStringList strings{"schema","requestId","source","wallet","destination","amountAtoms",
        "feeAtoms","weight","account","priority","genesis","tipHash","tipHeight"};
    if (object.size() != 14 || !object.value("executable").isBool() || object.value("executable").toBool()) return result;
    for (const auto &field : strings) if (!object.value(field).isString()) return result;
    const auto value = [&](const char *field) { return object.value(field).toString(); };
    if (value("schema") != "monzero-native-fee-preview-v1" || value("source") != "native-nonrelayed-construction"
        || value("requestId") != m_requestId || value("wallet") != m_expected.wallet
        || value("destination") != m_expected.destination || value("amountAtoms") != m_expected.amountAtoms
        || value("account") != m_expected.account || value("priority") != m_expected.priority
        || value("genesis") != m_expected.genesis || !hash(value("tipHash"))) return result;
    quint64 fee=0, amount=0, weight=0, height=0;
    if (!integer(value("feeAtoms"),fee) || fee == 0 || !integer(value("amountAtoms"),amount)
        || fee > std::numeric_limits<quint64>::max()-amount
        || !integer(value("weight"),weight) || weight == 0 || !integer(value("tipHeight"),height)
        || m_deadline.hasExpired()) return result;
    result["valid"] = true;
    result.remove("error");
    result["requestId"] = m_requestId;
    result["feeAtoms"] = value("feeAtoms");
    result["feeXmz"] = DexUtils::formatAtomic(fee,11);
    result["amountAtoms"] = value("amountAtoms");
    result["amountXmz"] = DexUtils::formatAtomic(amount,11);
    result["destination"] = m_expected.destination;
    result["label"] = "Fee estimate (display only; transport not authenticated)";
    m_current = result;
    return result;
}
