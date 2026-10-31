#include "DexTradeFolders.h"
#include "BchAdmissionProtocol.h"
#include "LitecoinJournalProtocol.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>
#include <QRegularExpression>
#include <algorithm>

namespace {
bool uuid(const QString &value) {const QUuid id(value);return !id.isNull() && id.toString(QUuid::WithoutBraces)==value;}
bool privateDirectory(const QString &path) {
    const QFileInfo info(path);
    return info.isDir() && !info.isSymLink() && !(info.permissions() &
        (QFile::ReadGroup | QFile::WriteGroup | QFile::ExeGroup | QFile::ReadOther | QFile::WriteOther | QFile::ExeOther));
}
QString resolve(const QString &folder,const QString &config) {
    if (QFileInfo(config).isAbsolute()) return QDir::cleanPath(config)==config?config:QString();
    if (config.isEmpty() || config.contains('/') || config.contains('\\') || config.startsWith('.')) return {};
    return QDir(folder).filePath(config);
}
bool parse(const QByteArray &bytes,const QString &folderName,DexTradeFolders::Trade &trade) {
    QJsonObject m;
    if (!BchAdmissionProtocol::canonicalObject(bytes,&m)) return false;
    const bool bound=m.value("schema")=="monzero-dex-trade-v2";
    if (m.size()!=(bound?7:6) || (!bound && m.value("schema")!="monzero-dex-trade-v1")) return false;
    trade.setupDigest=bound?m.value("setupDigest").toString():QString();
    if (bound && !QRegularExpression("\\A[0-9a-f]{64}\\z").match(trade.setupDigest).hasMatch()) return false;
    trade.tradeId=m.value("tradeId").toString();trade.pair=m.value("pair").toString();trade.network=m.value("network").toString();
    trade.state=m.value("state").toString();trade.appConfig=m.value("appConfig").toString();
    return uuid(trade.tradeId) && trade.tradeId==folderName && trade.pair=="LTC/XMZ"
        && !LitecoinJournalProtocol::bech32Prefix(trade.network).isEmpty()
        && (trade.state=="active" || trade.state=="closed") && !trade.appConfig.isEmpty();
}
QByteArray manifest(const DexTradeFolders::Trade &t) {
    QJsonObject value{{"schema",t.setupDigest.isEmpty()?"monzero-dex-trade-v1":"monzero-dex-trade-v2"},{"tradeId",t.tradeId},{"pair",t.pair},
        {"network",t.network},{"appConfig",t.appConfig},{"state",t.state}};
    if (!t.setupDigest.isEmpty()) value.insert("setupDigest",t.setupDigest);
    return QJsonDocument(value).toJson(QJsonDocument::Compact);
}
bool write(const QString &path,const QByteArray &bytes) {
    QSaveFile file(path);
    return file.open(QIODevice::WriteOnly) && file.setPermissions(QFile::ReadOwner | QFile::WriteOwner)
        && file.write(bytes)==bytes.size() && file.commit();
}
}

QString DexTradeFolders::defaultRoot() {
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath("dex/trades");
}
QList<DexTradeFolders::Trade> DexTradeFolders::scan(const QString &root) {
    QList<Trade> trades;
    if (!privateDirectory(root)) return trades;
    for (const auto &name:QDir(root).entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden,QDir::Name)) {
        Trade trade;trade.folder=QDir(root).filePath(name);
        const auto bytes=LitecoinJournalProtocol::privateConfig(QDir(trade.folder).filePath("trade.json"),4096);
        if (!privateDirectory(trade.folder) || !parse(bytes,name,trade)) {
            trade=Trade{name,{},{},{},"invalid",QDir(root).filePath(name)};
        } else {
            trade.appConfig=resolve(trade.folder,trade.appConfig);
            if (trade.appConfig.isEmpty()) trade.state="invalid";
        }
        trades.append(trade);
    }
    return trades;
}
QVariantList DexTradeFolders::describe(const QList<Trade> &trades,const QString &loaded) {
    QVariantList out;
    for (const auto &t:trades) out.append(QVariantMap{{"tradeId",t.tradeId},{"pair",t.pair},{"network",t.network},
        {"state",t.state},{"loaded",!loaded.isEmpty() && t.tradeId==loaded},{"folder",t.folder}});
    return out;
}
bool DexTradeFolders::create(const QString &root,const QString &tradeId,const QString &pair,const QString &network,const QString &appConfig,const QString &setupDigest) {
    Trade t{tradeId,pair,network,appConfig,"active",QDir(root).filePath(tradeId),setupDigest};
    Trade check;
    if (!parse(manifest(t),tradeId,check) || resolve(t.folder,appConfig).isEmpty()) return false;
    if (!QFileInfo::exists(root)) {
        if (!QDir().mkpath(root) || !QFile::setPermissions(root,QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner)) return false;
    }
    if (!privateDirectory(root) || QFileInfo::exists(t.folder) || !QDir().mkdir(t.folder)
        || !QFile::setPermissions(t.folder,QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner)) return false;
    return write(QDir(t.folder).filePath("trade.json"),manifest(t));
}
bool DexTradeFolders::close(const QString &root,const QString &tradeId) {
    if (!uuid(tradeId)) return false;
    const auto folder=QDir(root).filePath(tradeId),path=QDir(folder).filePath("trade.json");
    Trade t;
    if (!privateDirectory(root) || !privateDirectory(folder)
        || !parse(LitecoinJournalProtocol::privateConfig(path,4096),tradeId,t) || t.state!="active") return false;
    t.state="closed";return write(path,manifest(t));
}
