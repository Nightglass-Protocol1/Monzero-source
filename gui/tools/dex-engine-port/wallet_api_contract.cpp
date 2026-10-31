// Compile-only contract for the native XMZ wallet bridge investigation.
// No functions are invoked, linked, or given wallet/secret material.
#include "wallet/api/wallet2_api.h"
#include <type_traits>
#include <utility>

using Wallet = Monero::Wallet;
using Manager = Monero::WalletManager;
using String = std::string;

static_assert(std::is_same_v<decltype(std::declval<const Wallet&>().balance(0)), uint64_t>);
static_assert(std::is_same_v<decltype(std::declval<const Wallet&>().unlockedBalance(0)), uint64_t>);
static_assert(std::is_same_v<decltype(std::declval<Wallet&>().scanTransactions(
    std::declval<const std::vector<String>&>())), bool>);
static_assert(std::is_same_v<decltype(std::declval<Wallet&>().setRefreshFromBlockHeight(uint64_t{})), void>);
static_assert(std::is_same_v<decltype(std::declval<Wallet&>().refresh()), bool>);
static_assert(std::is_same_v<decltype(std::declval<Wallet&>().store(std::declval<const String&>())), bool>);
static_assert(std::is_same_v<decltype(std::declval<Wallet&>().checkTxKey(
    std::declval<const String&>(), std::declval<String>(), std::declval<const String&>(),
    std::declval<uint64_t&>(), std::declval<bool&>(), std::declval<uint64_t&>())), bool>);
static_assert(std::is_same_v<decltype(std::declval<Manager&>().createWalletFromKeys(
    std::declval<const String&>(), std::declval<const String&>(), std::declval<const String&>(),
    Monero::MAINNET, uint64_t{}, std::declval<const String&>(),
    std::declval<const String&>(), std::declval<const String&>(), uint64_t{1})), Wallet*>);
