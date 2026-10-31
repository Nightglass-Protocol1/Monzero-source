#ifndef MONZERO_DEX_DEPLOYMENT_H
#define MONZERO_DEX_DEPLOYMENT_H
#include <QString>

// Process-wide UTXO-chain swap deployment (Litecoin or Bitcoin against XMZ).
// Exactly one network is accepted at a time: Litecoin regtest by default, any
// other stage only after an explicit startup opt-in. Mainnet is deliberately
// unrepresentable here until release gates (independent review, caps, kill
// switch) are satisfied. The "litecoin" names are historical: the Bitcoin
// stages run the same contract paths with Bitcoin chain identities.
namespace DexDeployment {
enum class LitecoinStage {Regtest,Testnet,BitcoinRegtest,BitcoinTestnet4,BitcoinSignet};
namespace detail {
inline LitecoinStage &stage() {static LitecoinStage value=LitecoinStage::Regtest;return value;}
inline bool &locked() {static bool value=false;return value;}
}
// Accepts "regtest", "testnet" (Litecoin), "btc-regtest", "btc-testnet4" or "btc-signet" once,
// before any DEX configuration is read.
inline bool selectLitecoinStage(const QString &name) {
    if (detail::locked()) return false;
    if (name=="regtest") detail::stage()=LitecoinStage::Regtest;
    else if (name=="testnet") detail::stage()=LitecoinStage::Testnet;
    else if (name=="btc-regtest") detail::stage()=LitecoinStage::BitcoinRegtest;
    else if (name=="btc-testnet4") detail::stage()=LitecoinStage::BitcoinTestnet4;
    else if (name=="btc-signet") detail::stage()=LitecoinStage::BitcoinSignet;
    else return false;
    detail::locked()=true;return true;
}
// Freezes the default so a later selection cannot change accepted configs.
inline void lockLitecoinStage() {detail::locked()=true;}
inline LitecoinStage litecoinStage() {return detail::stage();}
inline bool bitcoinStage() {
    return detail::stage()==LitecoinStage::BitcoinRegtest || detail::stage()==LitecoinStage::BitcoinTestnet4
        || detail::stage()==LitecoinStage::BitcoinSignet;
}
// A public test network, where peers may be other machines.
inline bool publicTestStage() {
    return detail::stage()==LitecoinStage::Testnet || detail::stage()==LitecoinStage::BitcoinTestnet4
        || detail::stage()==LitecoinStage::BitcoinSignet;
}
// Light-wallet chain tag ("ltc" or "btc") of the swap chain.
inline QString litecoinLightCoin() {return bitcoinStage()?"btc":"ltc";}
// Display name of the swap chain.
inline QString swapCoinName() {return bitcoinStage()?"Bitcoin":"Litecoin";}
// Quote/journal identifier, e.g. "ltc-regtest" or "btc-testnet4".
inline QString litecoinNetwork() {
    switch (detail::stage()) {
    case LitecoinStage::Testnet: return "ltc-testnet";
    case LitecoinStage::BitcoinRegtest: return "btc-regtest";
    case LitecoinStage::BitcoinTestnet4: return "btc-testnet4";
    case LitecoinStage::BitcoinSignet: return "btc-signet";
    default: return "ltc-regtest";
    }
}
// Light-wallet configuration identifier, as the light helper names networks
// ("main", "test", "testnet4", "signet", "regtest", following the chain's Core node).
inline QString litecoinLightNetwork() {
    switch (detail::stage()) {
    case LitecoinStage::Testnet: return "test";
    case LitecoinStage::BitcoinTestnet4: return "testnet4";
    case LitecoinStage::BitcoinSignet: return "signet";
    default: return "regtest";
    }
}
inline bool litecoinNetworkAllowed(const QString &quoted) {return quoted==litecoinNetwork();}
// Genesis hash of the selected swap chain, as the light helper reports it.
inline QString litecoinGenesis() {
    switch (detail::stage()) {
    case LitecoinStage::Testnet: return "4966625a4b2851d9fdee139e56211a0d88575f59ed816ff5e6a63deb4e3e29a0";
    case LitecoinStage::BitcoinRegtest: return "0f9188f13cb7b2c71f2a335e3a4fc328bf5beb436012afca590b1a11466e2206";
    case LitecoinStage::BitcoinTestnet4: return "00000000da84f2bafbbc53dee25a72ae507ff4914b867c565be350b0da8bf043";
    case LitecoinStage::BitcoinSignet: return "00000008819873e925422c1ff0f99f7cc9bbb232af63a077a480a3633bee1ef6";
    default: return "530827f38f93b43ed12af0b3ad25a288dc02ed74d6d7857862df51fc56c416f9";
    }
}
#ifdef MONZERO_DEX_TESTING
// Tests only: reset between cases.
inline void resetForTesting() {detail::stage()=LitecoinStage::Regtest;detail::locked()=false;}
#endif
}
#endif
