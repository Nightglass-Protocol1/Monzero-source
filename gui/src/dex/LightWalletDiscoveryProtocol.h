#ifndef MONZERO_LIGHT_WALLET_DISCOVERY_PROTOCOL_H
#define MONZERO_LIGHT_WALLET_DISCOVERY_PROTOCOL_H
#include "LightWalletProfile.h"
#include "LitecoinJournalProtocol.h"
#include <QJsonDocument>
#include <QJsonArray>
#include <QCryptographicHash>
#include <QRegularExpression>
#include <cmath>
namespace LightWalletDiscoveryProtocol {
inline QString digest(const QByteArray &bytes) {return QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex());}
inline bool hash(const QJsonValue &v) {return v.isString() && QRegularExpression("\\A[0-9a-f]{64}\\z").match(v.toString()).hasMatch();}
inline bool integer(const QJsonValue &v,double maximum) {return v.isDouble() && v.toDouble()>=0 && v.toDouble()<=maximum && std::floor(v.toDouble())==v.toDouble();}
inline bool pair(const QJsonValue &v) {return v.isArray() && v.toArray().size()==2 && integer(v.toArray()[0],10000) && integer(v.toArray()[1],10000);}
// Only public, bounded discovery evidence. No spendability or completeness claim.
inline bool validate(const QJsonObject &response,const QByteArray &raw,const QString &coin,const QString &request,
    const QJsonObject &scan,const QJsonArray &known,quint32 budget,bool verifyAllocation=true) {
    const auto config=QJsonDocument::fromJson(raw).object();const auto wallet=config.value("wallet").toObject();
    const auto review=response.value("review").toObject();const auto coverage=review.value("coverage").toObject();
    const auto sync=response.value("synchronization").toObject();const auto maximum=config.value("maximumHeight").toDouble();
    bool amountOk=false;const auto amount=review.value("observedConfirmedAtomic").toString();amount.toULongLong(&amountOk);
    if (response.size()!=6 || response.value("schema")!=LightWalletProfile::schema(coin,"light-wallet-import-discover-response")
        || response.value("requestId")!=request || response.value("executable")!=false || review.size()!=13
        || review.value("schema")!=LightWalletProfile::schema(coin,"light-import-discovery-review")
        || review.value("configurationDigest")!=digest(raw) || review.value("walletId")!=wallet.value("vault").toObject().value("walletId")
        || !review.value("endpoint").isObject() || review.value("endpoint")!=config.value("endpoint")
        || !amountOk || !QRegularExpression("\\A(0|[1-9][0-9]{0,19})\\z").match(amount).hasMatch()
        || review.value("balanceStatus")!="provisional" || review.value("historyCompleteness")!="unproven"
        || review.value("spendable")!=false || review.value("allocationsRetained")!=false
        || review.value("activationRequired")!=true || review.value("activeSwapRecovery")!=false
        || response.value("reviewDigest")!=digest(QJsonDocument(review).toJson(QJsonDocument::Compact))
        || review.value("coverageCommitment")!=digest(QJsonDocument(coverage).toJson(QJsonDocument::Compact))) return false;
    if (coverage.size()!=14 || coverage.value("schema")!=LightWalletProfile::schema(coin,"recovery-coverage")
        || coverage.value("network")!=config.value("network") || coverage.value("account")!=wallet.value("account")
        || !integer(coverage.value("height"),maximum) || !hash(coverage.value("tip")) || !hash(coverage.value("allocationDigest"))
        || coverage.value("historyCompleteness")!="unproven" || coverage.value("activeSwapRecovery")!=false
        || coverage.value("backupRefreshRequired")!=true || coverage.value("knownMinimum")!=known
        || coverage.value("reportedGap")!=scan.value("gap") || !pair(coverage.value("allocatedBefore"))
        || !pair(coverage.value("allocatedAfter")) || !pair(coverage.value("scanAllocatedMinimum"))) return false;
    const auto before=coverage.value("allocatedBefore").toArray(),after=coverage.value("allocatedAfter").toArray(),minimum=coverage.value("scanAllocatedMinimum").toArray();
    if (known.size()!=2 || after[0].toInt()+after[1].toInt()>10000) return false;
    for (int branch=0;branch<2;++branch) {
        if (minimum[branch].toInt()!=qMax(before[branch].toInt(),known[branch].toInt())
            || after[branch].toInt()<minimum[branch].toInt()+scan.value("gap").toInt()
            || after[branch].toInt()>scan.value("addressesPerBranch").toInt()) return false;
    }
    if (verifyAllocation) {
        const auto record=LitecoinJournalProtocol::privateRecord(wallet.value("addressBookDirectory").toString()+"/litecoin-address-book.json",2000000);
        if (record.isEmpty() || coverage.value("allocationDigest")!=digest(record)) return false;
    }
    if (sync.size()!=5 || !integer(sync.value("reportedHeight"),maximum) || !hash(sync.value("reportedTip"))
        || !integer(sync.value("downloadedHeaders"),budget) || !sync.value("reportedTipOnSelectedChain").isBool()) return false;
    if (sync.value("reportedHeight")==coverage.value("height")
        && sync.value("reportedTipOnSelectedChain").toBool()!=(sync.value("reportedTip")==coverage.value("tip"))) return false;
    if (sync.value("reportedHeight").toDouble()>coverage.value("height").toDouble() && sync.value("reportedTipOnSelectedChain").toBool()) return false;
    if (sync.value("change").isNull()) return true;
    const auto change=sync.value("change").toObject();
    return change.size()==5 && integer(change.value("commonAncestor"),maximum) && integer(change.value("oldHeight"),maximum)
        && hash(change.value("oldTip")) && change.value("newHeight")==coverage.value("height") && change.value("newTip")==coverage.value("tip")
        && change.value("commonAncestor").toDouble()<=change.value("oldHeight").toDouble()
        && change.value("commonAncestor").toDouble()<change.value("newHeight").toDouble();
}
}
#endif
