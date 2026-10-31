#ifndef MONZERO_LIGHT_WALLET_BACKUP_RESTORE_H
#define MONZERO_LIGHT_WALLET_BACKUP_RESTORE_H
#include "LightWalletDiscoveryProtocol.h"
#include <QDir>
#include <QUuid>
namespace LightWalletBackupRestore {
using LightWalletDiscoveryProtocol::digest;
inline bool settings(const QJsonObject &s,const QString &coin) {
    const QStringList fields{"schema","network","backupDirectory","setupDirectory","vaultDirectory","addressBookDirectory","cacheDirectory","spendDirectory","maximumHeight","endpoint"};
    if ((s.size()!=9 && s.size()!=10) || s.value("schema")!=LightWalletProfile::schema(coin,"backup-setup",true)
        || !LightWalletProfile::networkAllowed(coin,s.value("network").toString())
        || !LightWalletDiscoveryProtocol::integer(s.value("maximumHeight"),10000000)) return false;
    for(const auto &key:s.keys())if(!fields.contains(key))return false;
    QStringList paths;
    for(const auto key:{"setupDirectory","vaultDirectory","addressBookDirectory","cacheDirectory","spendDirectory"}) {
        const auto path=s.value(key).toString();const QFileInfo info(path);
        if(!info.isAbsolute() || !info.isDir() || info.canonicalFilePath()!=path)return false;
        for(const auto &other:paths)if(path==other || path.startsWith(other+"/") || other.startsWith(path+"/"))return false;
        paths.append(path);
    }
    const auto source=s.value("backupDirectory").toString();
    if(!QFileInfo(source).isAbsolute() || QDir::cleanPath(source)!=source)return false;
    if(s.contains("endpoint") && !s.value("endpoint").isNull()) {
        const auto endpoint=s.value("endpoint").toObject();
        if(endpoint.size()!=2 || endpoint.value("host").toString().isEmpty() || endpoint.value("host").toString().size()>253
            || !LightWalletDiscoveryProtocol::integer(endpoint.value("port"),65535) || endpoint.value("port").toInt()==0)return false;
    }
    return true;
}
inline bool review(const QJsonObject &r,const QJsonObject &s,const QString &coin,bool currentBackup) {
    const auto config=r.value("configuration").toObject(),wallet=config.value("wallet").toObject();
    const auto id=r.value("walletId").toString();const QUuid uuid(id);
    if(r.size()!=14 || r.value("schema")!="monzero-light-backup-restore-review-v1" || r.value("coin")!=coin
        || r.value("network")!=s.value("network") || uuid.isNull() || uuid.toString(QUuid::WithoutBraces)!=id
        || !LightWalletDiscoveryProtocol::hash(r.value("backupDigest")) || !LightWalletDiscoveryProtocol::pair(r.value("allocated"))
        || r.value("allocated").toArray()[0].toInt()+r.value("allocated").toArray()[1].toInt()>10000
        || (r.value("seedEncoding")!="raw-bip32-seed-hex-v1" && r.value("seedEncoding")!="bip39-english-v1")
        || r.value("configurationFile")!=s.value("setupDirectory").toString()+"/litecoin-wallet-restored.json"
        || r.value("scope")!="seed-and-address-allocations" || r.value("allocationFreshness")!="unassessed"
        || r.value("historyCompleteness")!="unproven" || r.value("activeSwapRecovery")!=false || r.value("tradingEnabled")!=false)return false;
    QJsonObject expected{{"schema",LightWalletProfile::schema(coin,"light",true)},{"network",s.value("network")},
        {"cacheDirectory",s.value("cacheDirectory")},{"maximumHeight",s.value("maximumHeight")},
        {"wallet",QJsonObject{{"account",wallet.value("account")},{"addressBookDirectory",s.value("addressBookDirectory")},
            {"spendDirectory",s.value("spendDirectory")},{"vault",QJsonObject{{"directory",s.value("vaultDirectory")},
                {"walletId",id},{"seedEncoding",r.value("seedEncoding")}}}}}};
    if(s.value("endpoint").isObject())expected.insert("endpoint",s.value("endpoint"));
    if(config!=expected)return false;
    if(currentBackup) {
        const auto backup=LitecoinJournalProtocol::privateRecord(s.value("backupDirectory").toString()+"/litecoin-wallet.backup",2000000);
        if(backup.isEmpty() || digest(backup)!=r.value("backupDigest").toString())return false;
    }
    return true;
}
inline QJsonObject completion(const QJsonObject &review) {
    return {{"schema","monzero-light-backup-restore-complete-v1"},{"reviewDigest",digest(QJsonDocument(review).toJson(QJsonDocument::Compact))},
        {"backupDigest",review.value("backupDigest")},{"configurationFile",review.value("configurationFile")},
        {"configurationDigest",digest(QJsonDocument(review.value("configuration").toObject()).toJson(QJsonDocument::Compact))},
        {"walletId",review.value("walletId")},{"coin",review.value("coin")},{"network",review.value("network")},
        {"allocated",review.value("allocated")},{"restored",true},{"allocationFreshness","unassessed"},
        {"historyCompleteness","unproven"},{"activeSwapRecovery",false},{"tradingEnabled",false}};
}
}
#endif
