#ifndef MONZERO_LIGHT_WALLET_IMPORT_ACTIVATION_H
#define MONZERO_LIGHT_WALLET_IMPORT_ACTIVATION_H
#include "LightWalletDiscoveryProtocol.h"
namespace LightWalletImportActivation {
// Historical activation evidence only. Do not authenticate or claim a current
// backup here: later ordinary address allocations may legitimately advance it.
inline bool load(const QString &root,const QByteArray &pending,const QString &coin,QByteArray &active,QJsonObject &receipt) {
    const auto bytes=LitecoinJournalProtocol::privateConfig(root+"/litecoin-wallet-activated.json");
    const auto document=QJsonDocument::fromJson(bytes);const auto proof=document.object();
    const auto config=QJsonDocument::fromJson(pending).object();const auto wallet=config.value("wallet").toObject();
    if (bytes.isEmpty() || !document.isObject() || document.toJson(QJsonDocument::Compact)!=bytes || proof.size()!=16
        || proof.value("schema")!="monzero-light-wallet-activation-complete-v1"
        || proof.value("pendingConfigurationDigest")!=LightWalletDiscoveryProtocol::digest(pending)
        || proof.value("coin")!=coin || proof.value("network")!=config.value("network")
        || proof.value("walletId")!=wallet.value("vault").toObject().value("walletId")
        || proof.value("configurationFile")!=root+"/litecoin-wallet-active.json"
        || proof.value("walletActivated")!=true || proof.value("tradingEnabled")!=false
        || proof.value("historyCompleteness")!="unproven" || proof.value("activeSwapRecovery")!=false
        || !LightWalletDiscoveryProtocol::hash(proof.value("recoveryDigest")) || !LightWalletDiscoveryProtocol::hash(proof.value("coverageCommitment"))
        || !LightWalletDiscoveryProtocol::hash(proof.value("backupDigest"))
        || !proof.value("backupDirectory").isString() || !QFileInfo(proof.value("backupDirectory").toString()).isAbsolute()) return false;
    auto expected=config;expected.remove("recovery");expected.insert("schema",LightWalletProfile::schema(coin,"light",true));
    const auto raw=LitecoinJournalProtocol::privateConfig(proof.value("configurationFile").toString());
    if (raw!=QJsonDocument(expected).toJson(QJsonDocument::Compact) || proof.value("configurationDigest")!=LightWalletDiscoveryProtocol::digest(raw)) return false;
    auto intent=proof;intent.insert("schema","monzero-light-wallet-activation-intent-v1");
    if (LitecoinJournalProtocol::privateConfig(root+"/litecoin-wallet-activation.json")!=QJsonDocument(intent).toJson(QJsonDocument::Compact)) return false;
    const auto directory=proof.value("recoveryDirectory").toString();
    const auto recoveryBytes=LitecoinJournalProtocol::privateConfig(directory+"/light-recovery-backup-complete.json");
    const auto recoveryDocument=QJsonDocument::fromJson(recoveryBytes);const auto recovery=recoveryDocument.object();
    const auto contextBytes=LitecoinJournalProtocol::privateConfig(directory+"/light-recovery-backup-context.json");
    const auto contextDocument=QJsonDocument::fromJson(contextBytes);const auto context=contextDocument.object();
    if (recoveryBytes.isEmpty() || recoveryDocument.toJson(QJsonDocument::Compact)!=recoveryBytes || recovery.size()!=15
        || recovery.value("schema")!="monzero-light-recovery-backup-complete-v1"
        || proof.value("recoveryDigest")!=LightWalletDiscoveryProtocol::digest(recoveryBytes)
        || recovery.value("configurationDigest")!=proof.value("pendingConfigurationDigest")
        || recovery.value("coverageCommitment")!=proof.value("coverageCommitment")
        || recovery.value("walletId")!=proof.value("walletId") || recovery.value("backupDigest")!=proof.value("backupDigest")
        || recovery.value("backupDirectory")!=proof.value("backupDirectory") || recovery.value("allocationsRetained")!=true
        || recovery.value("backupVerified")!=true || recovery.value("activated")!=false
        || recovery.value("historyCompleteness")!="unproven" || recovery.value("activeSwapRecovery")!=false
        || !LightWalletDiscoveryProtocol::pair(recovery.value("allocated")) || !LightWalletDiscoveryProtocol::hash(recovery.value("allocationDigest"))
        || !LightWalletDiscoveryProtocol::hash(recovery.value("vaultDigest")) || contextBytes.isEmpty()
        || contextDocument.toJson(QJsonDocument::Compact)!=contextBytes || context.size()!=6
        || context.value("schema")!="monzero-light-recovery-backup-context-v1"
        || context.value("coverageCommitment")!=proof.value("coverageCommitment")
        || context.value("configurationDigest")!=proof.value("pendingConfigurationDigest")
        || context.value("vaultDigest")!=recovery.value("vaultDigest") || context.value("account")!=wallet.value("account")
        || context.value("backupDirectory")!=proof.value("backupDirectory")
        || recovery.value("contextDigest")!=LightWalletDiscoveryProtocol::digest(contextBytes)) return false;
    active=raw;receipt=proof;return true;
}
}
#endif
