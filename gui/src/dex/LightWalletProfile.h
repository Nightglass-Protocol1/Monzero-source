#ifndef MONZERO_LIGHT_WALLET_PROFILE_H
#define MONZERO_LIGHT_WALLET_PROFILE_H
#include <QString>
#include <QStringList>
#include <QRegularExpression>

// Public chain identity only. Never infer a chain from an address or fall back
// to Litecoin when a caller supplies an unknown coin. Persisted LTC names stay.
namespace LightWalletProfile {
inline bool supported(const QString &coin) {
    return QStringList{"ltc","btc","bch","doge"}.contains(coin);
}
inline bool networkAllowed(const QString &coin,const QString &network) {
    // Bitcoin's public test networks are testnet4 (BIP94) and signet; "test" names testnet3.
    if (coin=="btc" && (network=="testnet4" || network=="signet")) return true;
    if (!supported(coin) || !QStringList{"main","test","regtest"}.contains(network)) return false;
    return (coin!="bch" && coin!="doge") || network=="regtest";
}
inline QString schema(const QString &coin,const QString &kind,bool privateConfig=false) {
    return supported(coin)?QString("monzero-%1%2-%3-v1").arg(privateConfig?"private-":"",coin,kind):QString{};
}
inline QString witnessPrefix(const QString &coin,const QString &network) {
    if (!networkAllowed(coin,network)) return {};
    if (coin=="ltc") return network=="main"?"ltc":network=="test"?"tltc":"rltc";
    if (coin=="btc") return network=="main"?"bc":network=="regtest"?"bcrt":"tb";
    return {};
}
// UI shape check only; the pinned helper performs checksum, network and script
// validation before using any payment address. This is not a signing authority.
inline bool addressShape(const QString &coin,const QString &network,const QString &address,bool owned=false) {
    if (!networkAllowed(coin,network)) return false;
    const auto prefix=witnessPrefix(coin,network);
    if (!prefix.isEmpty()) return QRegularExpression("\\A"+prefix+"1q(?:[023456789acdefghjklmnpqrstuvwxyz]{38}"
        +(owned?QString{}:QString("|[023456789acdefghjklmnpqrstuvwxyz]{58}"))+")\\z").match(address).hasMatch();
    if (coin=="bch") return QRegularExpression("\\Abchreg:q[023456789acdefghjklmnpqrstuvwxyz]{41}\\z").match(address).hasMatch();
    return QRegularExpression("\\A[mn][1-9A-HJ-NP-Za-km-z]{25,34}\\z").match(address).hasMatch();
}
inline QString receivePath(const QString &coin,const QString &network,int account) {
    if (!networkAllowed(coin,network) || account<0) return {};
    const int purpose=(coin=="ltc" || coin=="btc")?84:44;
    const int type=network!="main"?1:coin=="ltc"?2:coin=="btc"?0:coin=="bch"?145:3;
    return QString("%1'/%2'/%3'/0/").arg(purpose).arg(type).arg(account);
}
}
#endif
