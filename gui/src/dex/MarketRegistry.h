#ifndef MONZERO_DEX_MARKET_REGISTRY_H
#define MONZERO_DEX_MARKET_REGISTRY_H

#include <QString>
#include <array>

namespace Dex {
// Presentation capabilities, never authority to sign, lock or broadcast.
// Keep canonical offer/quote and settlement allowlists independent until each
// protocol has a versioned wire format and chain-backed recovery evidence.
struct Market {
    const char *symbol;
    int decimals;
    int atomicBits;
    const char *family;
    const char *status;
    const char *blocker;
};

inline const std::array<Market, 8> &markets()
{
    static const std::array<Market, 8> entries{{
        {"BTC", 8, 64, "bitcoin", "isolated-fixtures",
            "BTC settlement has isolated test fixtures; live trading remains unavailable."},
        {"LTC", 8, 64, "litecoin", "isolated-fixtures",
            "LTC/XMZ success and refund pass isolated app fixtures; light-wallet settlement and broader recovery verification remain unfinished."},
        {"BCH", 8, 64, "bitcoin-cash", "isolated-fixtures",
            "BCH/XMZ controller success passes isolated-chain tests with exact payouts and fees; app integration, peer transport and broader recovery remain unfinished. Live trading is unavailable."},
        {"XMR", 12, 64, "monero", "protocol-research",
            "XMR/XMZ requires a separate atomic-swap protocol; settlement is unavailable."},
        {"DOGE", 8, 64, "dogecoin", "protocol-research",
            "DOGE requires its own lock and refund protocol; settlement is unavailable."},
        {"ETH", 18, 256, "ethereum", "mainnet-tested",
            "ETH/XMZ passed swap, refund and stop-and-resume on Sepolia and a real-funds Ethereum mainnet swap (8 Nov 2026). Offers go through the open relays; trades are capped by the signed control document."},
        {"BNB", 18, 256, "evm", "mainnet-tested",
            "BNB/XMZ passed the BNB testnet runs and a real-funds mainnet swap (31 Oct 2026). Offers go through the open relays; trades are capped by the signed control document."},
        {"RUNE", 8, 64, "thorchain-native", "protocol-research",
            "Native THORChain RUNE settlement is not implemented."}
    }};
    return entries;
}

inline const Market *findMarket(const QString &pair)
{
    for (const auto &market : markets())
        if (pair == QString::fromLatin1(market.symbol) + " / XMZ") return &market;
    return nullptr;
}
}
#endif
