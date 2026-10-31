// Public test keys only. Never use these addresses for funds.
// Link the native encoder so the Rust adapter is checked across implementations.
#include "common/base58.h"
#include "cryptonote_config.h"
#include <array>
#include <iostream>
#include <string>

int main() {
    const std::array<std::array<uint64_t, 3>, 3> prefixes{{
        {config::CRYPTONOTE_PUBLIC_ADDRESS_BASE58_PREFIX,
         config::CRYPTONOTE_PUBLIC_INTEGRATED_ADDRESS_BASE58_PREFIX,
         config::CRYPTONOTE_PUBLIC_SUBADDRESS_BASE58_PREFIX},
        {config::testnet::CRYPTONOTE_PUBLIC_ADDRESS_BASE58_PREFIX,
         config::testnet::CRYPTONOTE_PUBLIC_INTEGRATED_ADDRESS_BASE58_PREFIX,
         config::testnet::CRYPTONOTE_PUBLIC_SUBADDRESS_BASE58_PREFIX},
        {config::stagenet::CRYPTONOTE_PUBLIC_ADDRESS_BASE58_PREFIX,
         config::stagenet::CRYPTONOTE_PUBLIC_INTEGRATED_ADDRESS_BASE58_PREFIX,
         config::stagenet::CRYPTONOTE_PUBLIC_SUBADDRESS_BASE58_PREFIX}
    }};
    // Ed25519 base point compressed (scalar one), used for both public keys.
    std::string key(32, '\x66');
    key[0] = '\x58';
    for (const auto& network : prefixes) {
        for (size_t kind = 0; kind < network.size(); ++kind) {
            std::string data = key + key;
            if (kind == 1) data += std::string(8, '\x07');
            std::cout << tools::base58::encode_addr(network[kind], data) << '\n';
        }
    }
}
