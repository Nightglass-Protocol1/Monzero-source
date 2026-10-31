#pragma once
#include "../BchCancellationProtocol.h"
#include "BchAdmissionTestFixture.h"
namespace {
QJsonArray pinBytes(int byte) {QJsonArray a;for (int i=0;i<32;++i) a.append(byte);return a;}
QJsonObject bound(QJsonObject details) {
    QByteArray bytes=details.value("schema").toString().toUtf8();bytes.append('\0');bytes+=QJsonDocument(details).toJson(QJsonDocument::Compact);
    return {{"details",details},{"commitment",QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex())}};
}
struct Context:Fixture {
    QJsonObject record,details;
    QString deployment=QString(64,'7');
    quint64 now=quint64(QDateTime::currentSecsSinceEpoch());
    bool create(bool native=false) {
        if (!Fixture::create()) return false;
        record=QJsonDocument::fromJson(read(config.value("quoteFile").toString())).object();
        auto terms=record.value("terms").toObject();terms["bchAtoms"]="100000";terms["bchLockFeeAtoms"]="1000";
        terms["cancelBlocks"]="20";terms["bchConfirmations"]="2";record["terms"]=terms;
        QByteArray bytes="monzero-native-bch-xmz-quote-v1";bytes.append('\0');bytes+=QJsonDocument(terms).toJson(QJsonDocument::Compact);
        const auto digest=QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex());
        record["commitment"]=digest;config["quoteCommitment"]=digest;config["role"]=native?"native-funder":"bch-funder";
        auto policy=config.value("policy").toObject();policy["cancel_blocks"]=20;policy["bch_confirmations"]=2;config["policy"]=policy;
        QJsonArray quote;for (const auto c:QByteArray::fromHex(digest.toLatin1())) quote.append(int(static_cast<unsigned char>(c)));
        details={{"schema",native?"monzero-bch-native-cancel-relay-review-v1":"monzero-bch-cancel-relay-review-v1"},
            {"action","broadcast-bch-cancellation"},{"swapId",config.value("tradeId")},{"quote",quote},
            {"admission",pinBytes(1)},{"bchDeployment",pinBytes(0x77)},{"bchNetwork",terms.value("bchNetwork")},
            {"bchGenesis",terms.value("bchGenesis")},{"fundingOutpoint",QString(64,'a')+":0"},
            {"txid",QString(64,'b')},{"recoveryScript","51"},{"recoveryAtoms","99000"},{"feeAtoms","1000"},
            {"cancelDelayBlocks",20},{"minimumBchConfirmations",2},{"preparedAt",config.value("admittedAt")}};
        if (native) {details["fundingReceipt"]=pinBytes(2);details["nativePayment"]=pinBytes(3);}
        else details["privateRecovery"]=pinBytes(2);
        now=quint64(QDateTime::currentSecsSinceEpoch());return true;
    }
    bool valid() const {return BchCancellationProtocol::review(bound(details),config,record,deployment,now);}
};
}
