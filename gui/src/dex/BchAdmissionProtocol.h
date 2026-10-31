#ifndef MONZERO_BCH_ADMISSION_PROTOCOL_H
#define MONZERO_BCH_ADMISSION_PROTOCOL_H
#include <QCryptographicHash>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>
#include <cmath>
namespace BchAdmissionProtocol {
inline bool hex(const QString &value,int length) {
    if (value.size()!=length) return false;
    for (const auto c:value) if (!QStringLiteral("0123456789abcdef").contains(c)) return false;
    return true;
}
inline bool integer(const QJsonValue &value,quint64 maximum,quint64 *result=nullptr) {
    if (!value.isDouble()) return false;
    const auto number=value.toDouble();
    if (!std::isfinite(number) || number<0 || number>9007199254740991.0
        || number>double(maximum) || std::floor(number)!=number) return false;
    if (result) *result=quint64(number);
    return true;
}
inline QString pin(const QJsonValue &value) {
    if (!value.isArray() || value.toArray().size()!=32) return {};
    QByteArray bytes;
    for (const auto byte:value.toArray()) {
        quint64 number=0;if (!integer(byte,255,&number)) return {};
        bytes.append(char(number));
    }
    return QString::fromLatin1(bytes.toHex());
}
inline bool canonicalObject(const QByteArray &bytes,QJsonObject *object) {
    const auto document=QJsonDocument::fromJson(bytes);
    if (!document.isObject() || document.toJson(QJsonDocument::Compact)!=bytes) return false;
    *object=document.object();return true;
}
inline bool expectedReview(const QJsonObject &config,const QJsonObject &record,QJsonObject *review) {
    if (config.size()!=13 || config.value("schema")!="monzero-private-bch-admission-v1"
        || record.size()!=6 || record.value("schema")!="monzero-bch-quote-record-v1") return false;
    const auto trade=config.value("tradeId").toString();
    const auto id=QUuid(trade);
    const auto role=config.value("role").toString();
    if (id.isNull() || id.toString(QUuid::WithoutBraces)!=trade
        || !QStringList{"bch-funder","native-funder"}.contains(role)
        || !integer(config.value("admittedAt"),9007199254740991ULL)
        || record.value("admittedAt")!=config.value("admittedAt")
        || record.value("commitment")!=config.value("quoteCommitment")
        || !hex(config.value("quoteCommitment").toString(),64)
        || !hex(record.value("bchSignature").toString(),128)
        || !hex(record.value("xmzSignature").toString(),128)) return false;
    for (const auto key:{"metadataDirectory","snapshotDirectory"}) {
        const auto path=config.value(key).toString();
        if (!QDir::isAbsolutePath(path) || QDir::cleanPath(path)!=path) return false;
    }
    if (config.value("metadataDirectory")==config.value("snapshotDirectory")) return false;
    const auto terms=record.value("terms").toObject();
    const QStringList fields{"schema","quoteId","swapId","bchFunder","xmzFunder","bchNetwork","bchGenesis",
        "xmzNetwork","xmzGenesis","bchAtoms","xmzAtoms","xmzPayoutAtoms","bchPayoutScript","bchRefundScript",
        "xmzPayout","xmzRefund","bchSigningKey","xmzSigningKey","bchFundingFeeMaxAtoms","bchLockFeeAtoms",
        "bchRecoveryFeeAtoms","xmzFundingFeeMaxAtoms","xmzClaimFeeMaxAtoms","xmzRefundFeeMaxAtoms",
        "bchConfirmations","xmzConfirmations","cancelBlocks","punishBlocks","bchMinimumOutputAtoms",
        "expiresAt","appFeeXmzAtoms"};
    if (terms.size()!=fields.size() || terms.value("swapId")!=trade
        || terms.value("schema")!="monzero-native-bch-xmz-quote-v1") return false;
    for (const auto &key:fields) {
        const auto value=terms.value(key);
        if (!value.isString() || value.toString().isEmpty() || value.toString().size()>256) return false;
        for (const auto c:value.toString()) if (c.unicode()<32 || c.unicode()>126) return false;
    }
    QByteArray binding("monzero-native-bch-xmz-quote-v1");binding.append('\0');
    binding+=QJsonDocument(terms).toJson(QJsonDocument::Compact);
    if (QString::fromLatin1(QCryptographicHash::hash(binding,QCryptographicHash::Sha256).toHex())
        !=config.value("quoteCommitment").toString()) return false;
    const auto identities=config.value("identityPins").toArray();
    if (identities.size()!=2 || pin(identities[0]).isEmpty() || pin(identities[1]).isEmpty()
        || pin(identities[0])!=terms.value("bchFunder").toString() || pin(identities[1])!=terms.value("xmzFunder").toString()) return false;
    const auto rawPins=config.value("negotiationPins").toObject();QJsonObject pins;
    if (rawPins.size()!=5) return false;
    for (const auto key:{"initial","selection","outgoing","received","agreement"}) {
        const auto value=pin(rawPins.value(key));if (value.isEmpty()) return false;pins[key]=value;
    }
    const auto rawPolicy=config.value("policy").toObject();QJsonObject policy;
    if (rawPolicy.size()!=15 || rawPolicy.value("bch_network")!="bch-regtest") return false;
    for (const auto &entry:QList<QPair<QString,QString>>{{"bch_network","bchNetwork"},{"bch_genesis","bchGenesis"},
        {"xmz_network","xmzNetwork"},{"xmz_genesis","xmzGenesis"}}) {
        if (!rawPolicy.value(entry.first).isString() || rawPolicy.value(entry.first)!=terms.value(entry.second)) return false;
        policy[entry.second]=rawPolicy.value(entry.first);
    }
    const QList<QPair<QString,QString>> counts{{"bch_confirmations","bchConfirmations"},
        {"xmz_confirmations","xmzConfirmations"},{"cancel_blocks","cancelBlocks"},{"punish_blocks","punishBlocks"}};
    const QList<QPair<QString,QString>> amounts{{"maximum_bch_atoms","maximumBchAtoms"},
        {"maximum_xmz_atoms","maximumXmzAtoms"},{"maximum_bch_funding_fee","maximumBchFundingFee"},
        {"maximum_xmz_fee","maximumXmzFee"},{"minimum_covenant_fee","minimumCovenantFee"},
        {"maximum_covenant_fee","maximumCovenantFee"},{"minimum_output_atoms","minimumOutputAtoms"}};
    for (const auto &entry:counts+amounts) {
        quint64 value=0;
        if (!integer(rawPolicy.value(entry.first),9007199254740991ULL,&value) || !value) return false;
        policy[entry.second]=counts.contains(entry)?rawPolicy.value(entry.first):QJsonValue(QString::number(value));
    }
    const QJsonObject details{{"schema","monzero-bch-execution-admission-review-v1"},
        {"action","admit-bch-execution"},{"role",role},{"quoteCommitment",config.value("quoteCommitment")},
        {"terms",terms},{"negotiationPins",pins},{"policy",policy},{"chainObserved",false},
        {"negotiationVerified",false},{"executable",false}};
    QByteArray consent("MONZERO-BCH-EXECUTION-ADMISSION-APPROVAL-V1");consent.append('\0');
    consent+=QJsonDocument(details).toJson(QJsonDocument::Compact);
    *review={{"details",details},{"commitment",QString::fromLatin1(QCryptographicHash::hash(consent,QCryptographicHash::Sha256).toHex())}};
    return true;
}
}

#endif
