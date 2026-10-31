#ifndef MONZERO_WALLET_ASSET_TRANSFER_H
#define MONZERO_WALLET_ASSET_TRANSFER_H
#include <cstddef>
#include <utility>

// Keep development asset controls usable with either wallet API generation.
// An absent capability must never be presented as an available transaction.
namespace WalletAssetTransfer {
template<typename Wallet>
constexpr auto available(int) -> decltype(&Wallet::createAssetTransferTransaction,bool()) {return true;}
template<typename Wallet>
constexpr bool available(long) {return false;}
template<typename Wallet,typename... Args>
auto create(int,Wallet *wallet,Args&&... args)
    -> decltype(wallet->createAssetTransferTransaction(std::forward<Args>(args)...)) {
    return wallet->createAssetTransferTransaction(std::forward<Args>(args)...);
}
template<typename Wallet,typename... Args>
std::nullptr_t create(long,Wallet *,Args&&...) {return nullptr;}
}
#endif
