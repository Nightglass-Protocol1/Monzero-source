#pragma once
#include "../BchRefundWalletProtocol.h"
#include "BchCancellationTestFixture.h"
namespace {
inline QJsonObject refundWalletStatusValue(const Context &c) {
        const auto t=c.record.value("terms").toObject();auto file=c.config.value("tradeId").toString();file.remove('-');file.prepend("swap-");
        QJsonObject v{{"schema","monzero-bch-native-refund-wallet-status-v1"},{"swapId",c.config.value("tradeId")},
            {"quote",c.details.value("quote")},{"admission",pinBytes(1)},{"walletFile",file},
            {"address",QString(95,'4')},{"nativePayment",QString(64,'a')},{"refundHint",QString(64,'b')},
            {"walletIntent",QJsonValue::Null},{"state","not-attempted"},{"bchDeployment",pinBytes(0x77)},
            {"bchNetwork",t.value("bchNetwork")},{"bchGenesis",t.value("bchGenesis")},{"xmzNetwork",t.value("xmzNetwork")},
            {"chainObserved",false},{"executable",false}};
        const QJsonObject binding{{"quote",v.value("quote")},{"admission",v.value("admission")},{"wallet",file},
            {"address",v.value("address")},{"nativePayment",v.value("nativePayment")},{"refund",v.value("refundHint")}};
        QByteArray bytes="monzero-bch-native-refund-wallet-intent-v1";bytes.append('\0');bytes+=QJsonDocument(binding).toJson(QJsonDocument::Compact);
        QJsonArray pin;for (const auto byte:QCryptographicHash::hash(bytes,QCryptographicHash::Sha256)) pin.append(int(static_cast<unsigned char>(byte)));
        v["walletBinding"]=pin;return v;
    }
}
