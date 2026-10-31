#include "DexRiskControl.h"
#include "BchAdmissionProtocol.h"
#include "LitecoinJournalProtocol.h"
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTimer>
#include <QUrl>
#include <QUuid>
#include <sodium.h>

namespace {
constexpr qint64 Day=86400;
constexpr qint64 LedgerRetention=7*Day;
constexpr qint64 MaxDocumentAge=30*Day;
qint64 nowOr(qint64 now) {return now>0?now:QDateTime::currentSecsSinceEpoch();}
bool atoms(const QJsonValue &value,quint64 *result) {
    static const QRegularExpression pattern("\\A(?:0|[1-9][0-9]{0,18})\\z");
    if (!value.isString() || !pattern.match(value.toString()).hasMatch()) return false;
    bool ok=false;*result=value.toString().toULongLong(&ok);return ok;
}
bool tradeId(const QString &value) {
    const QUuid id(value);return !id.isNull() && id.toString(QUuid::WithoutBraces)==value;
}
}

DexRiskControl::DexRiskControl(QObject *parent):QObject(parent) {}

bool DexRiskControl::setPublicKey(const QString &hex) {
    if (hex.isEmpty()) {m_publicKey.clear();return true;}
    if (!BchAdmissionProtocol::hex(hex,64)) return false;
    m_publicKey=QByteArray::fromHex(hex.toLatin1());return true;
}
bool DexRiskControl::setLedgerFile(const QString &path) {
    if (!QFileInfo(path).isAbsolute() || !m_ledgerPath.isEmpty()) return false;
    m_ledgerPath=path;return true;
}
void DexRiskControl::setNetwork(const QString &network) {
    if (network!=m_network) {m_network=network;m_control={};m_sequence=0;emit changed();}
}
bool DexRiskControl::loadControlFile(const QString &path) {
    const auto bytes=LitecoinJournalProtocol::privateConfig(path,4096);
    return !bytes.isEmpty() && acceptControlDocument(bytes);
}
bool DexRiskControl::acceptControlDocument(const QByteArray &bytes,qint64 now) {
    now=nowOr(now);
    auto reject=[this](const QString &reason){m_lastError=reason;emit changed();return false;};
    QJsonObject envelope,payload;
    if (m_publicKey.size()!=crypto_sign_PUBLICKEYBYTES) return reject("no control key configured");
    if (bytes.size()>4096 || !BchAdmissionProtocol::canonicalObject(bytes,&envelope) || envelope.size()!=2
        || !envelope.value("payload").isString() || !BchAdmissionProtocol::hex(envelope.value("signature").toString(),128))
        return reject("malformed control document");
    const auto body=envelope.value("payload").toString().toUtf8();
    QByteArray message("monzero-dex-control-v1");message.append('\0');message+=body;
    const auto signature=QByteArray::fromHex(envelope.value("signature").toString().toLatin1());
    if (crypto_sign_verify_detached(reinterpret_cast<const unsigned char *>(signature.constData()),
            reinterpret_cast<const unsigned char *>(message.constData()),quint64(message.size()),
            reinterpret_cast<const unsigned char *>(m_publicKey.constData()))!=0)
        return reject("control signature invalid");
    quint64 sequence=0,issued=0,expires=0,perTradeLtc=0,perTradeXmz=0,perDayLtc=0;
    if (!BchAdmissionProtocol::canonicalObject(body,&payload) || payload.size()!=9
        || payload.value("schema")!="monzero-dex-control-v1" || !payload.value("newTradesEnabled").isBool()
        || !BchAdmissionProtocol::integer(payload.value("sequence"),9007199254740991ULL,&sequence)
        || !BchAdmissionProtocol::integer(payload.value("issuedAt"),9007199254740991ULL,&issued)
        || !BchAdmissionProtocol::integer(payload.value("expiresAt"),9007199254740991ULL,&expires)
        || !atoms(payload.value("maxLtcAtomsPerTrade"),&perTradeLtc) || !atoms(payload.value("maxXmzAtomsPerTrade"),&perTradeXmz)
        || !atoms(payload.value("maxLtcAtomsPerDay"),&perDayLtc))
        return reject("malformed control payload");
    if (payload.value("network")!=m_network) return reject("control document is for another network");
    if (expires<=issued || qint64(expires)-qint64(issued)>MaxDocumentAge || qint64(issued)>now+300)
        return reject("control document validity window rejected");
    if (qint64(expires)<=now) return reject("control document expired");
    // Never roll back to an older document (replayed "enabled" after a stop).
    if (!m_control.isEmpty() && sequence<m_sequence) return reject("older control document ignored");
    m_control=payload;m_sequence=sequence;m_lastError.clear();emit changed();return true;
}
bool DexRiskControl::startPolling(const QString &httpsUrl,int intervalSeconds) {
    const QUrl url(httpsUrl);
    if (url.scheme()!="https" || url.host().isEmpty() || intervalSeconds<60 || m_poll) return false;
    m_url=httpsUrl;m_network_manager=new QNetworkAccessManager(this);m_poll=new QTimer(this);
    m_poll->setInterval(intervalSeconds*1000);
    connect(m_poll,&QTimer::timeout,this,&DexRiskControl::fetch);
    m_poll->start();QTimer::singleShot(0,this,&DexRiskControl::fetch);return true;
}
void DexRiskControl::fetch() {
    if (!m_network_manager) return;
    QNetworkRequest request{QUrl(m_url)};
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(30000);
    QPointer<QNetworkReply> reply=m_network_manager->get(request);
    connect(reply,&QNetworkReply::downloadProgress,this,[reply](qint64 received,qint64){if (reply && received>4096) reply->abort();});
    connect(reply,&QNetworkReply::finished,this,[this,reply]{
        if (!reply) return;
        reply->deleteLater();
        if (reply->error()!=QNetworkReply::NoError) {m_lastError="control document unreachable";emit changed();return;}
        acceptControlDocument(reply->read(4097));
    });
}

QJsonObject DexRiskControl::ledger() const {
    if (m_ledgerPath.isEmpty() || !QFileInfo::exists(m_ledgerPath)) return {};
    QJsonObject object;
    if (!BchAdmissionProtocol::canonicalObject(LitecoinJournalProtocol::privateRecord(m_ledgerPath,1048576),&object)
        || object.value("schema")!="monzero-dex-started-trades-v1") return {{"invalid",true}};
    return object;
}
QString DexRiskControl::refusal(const QString &trade,quint64 ltcAtoms,quint64 xmzAtoms,qint64 now) const {
    now=nowOr(now);
    if (!tradeId(trade)) return tr("Trade identifier is invalid.");
    // Local regtest without a document enforces nothing, so it needs no ledger.
    if (m_network.endsWith("-regtest") && m_control.isEmpty() && m_ledgerPath.isEmpty()) return {};
    if (m_ledgerPath.isEmpty()) return tr("Trade limits are not configured.");
    const auto record=ledger();
    if (record.value("invalid").toBool()) return tr("The local trade-limit record is unreadable; new trades are paused.");
    const auto trades=record.value("trades").toObject();
    if (trades.contains(trade)) return {}; // Already counted; resuming the same trade.
    if (m_network.endsWith("-regtest") && m_control.isEmpty()) return {};
    if (m_control.isEmpty()) return m_lastError.isEmpty()?tr("No valid operator control document; new trades are paused.")
        :tr("New trades are paused: %1.").arg(m_lastError);
    if (qint64(m_control.value("expiresAt").toDouble())<=now) return tr("The operator control document expired; new trades are paused.");
    if (!m_control.value("newTradesEnabled").toBool()) return tr("New trades are paused by the operator.");
    if (ltcAtoms>m_control.value("maxLtcAtomsPerTrade").toString().toULongLong()) return tr("%1 amount exceeds the per-trade limit.").arg(coinName());
    if (xmzAtoms>m_control.value("maxXmzAtomsPerTrade").toString().toULongLong()) return tr("XMZ amount exceeds the per-trade limit.");
    quint64 day=0;
    for (auto it=trades.begin();it!=trades.end();++it) {
        const auto entry=it.value().toObject();
        if (now-qint64(entry.value("startedAt").toDouble())<Day) day+=entry.value("ltcAtoms").toString().toULongLong();
    }
    if (day+ltcAtoms<day || day+ltcAtoms>m_control.value("maxLtcAtomsPerDay").toString().toULongLong())
        return tr("This trade would exceed the 24-hour %1 limit.").arg(coinName());
    return {};
}
bool DexRiskControl::recordStart(const QString &trade,quint64 ltcAtoms,qint64 now) {
    now=nowOr(now);
    if (!tradeId(trade)) return false;
    if (m_ledgerPath.isEmpty()) return m_network.endsWith("-regtest") && m_control.isEmpty();
    auto record=ledger();
    if (record.value("invalid").toBool()) return false;
    auto trades=record.value("trades").toObject();
    if (trades.contains(trade)) return true;
    for (const auto &key:trades.keys())
        if (now-qint64(trades.value(key).toObject().value("startedAt").toDouble())>LedgerRetention) trades.remove(key);
    trades.insert(trade,QJsonObject{{"ltcAtoms",QString::number(ltcAtoms)},{"startedAt",double(now)}});
    const auto bytes=QJsonDocument(QJsonObject{{"schema","monzero-dex-started-trades-v1"},{"trades",trades}}).toJson(QJsonDocument::Compact);
    QSaveFile file(m_ledgerPath);
    if (!file.open(QIODevice::WriteOnly) || !file.setPermissions(QFile::ReadOwner | QFile::WriteOwner)
        || file.write(bytes)!=bytes.size() || !file.commit()) return false;
    emit changed();return true;
}
QVariantMap DexRiskControl::status() const {
    const auto now=QDateTime::currentSecsSinceEpoch();
    const bool valid=!m_control.isEmpty() && qint64(m_control.value("expiresAt").toDouble())>now;
    return {{"network",m_network},{"controlRequired",!m_network.endsWith("-regtest")},{"controlValid",valid},
        {"newTradesEnabled",m_network.endsWith("-regtest") && m_control.isEmpty() ? true : valid && m_control.value("newTradesEnabled").toBool()},
        {"maxLtcAtomsPerTrade",m_control.value("maxLtcAtomsPerTrade").toString()},
        {"maxXmzAtomsPerTrade",m_control.value("maxXmzAtomsPerTrade").toString()},
        {"maxLtcAtomsPerDay",m_control.value("maxLtcAtomsPerDay").toString()},
        {"sequence",double(m_sequence)},{"expiresAt",m_control.value("expiresAt").toDouble()},{"error",m_lastError}};
}
