#ifndef MONZERO_DEX_CHAINS_H
#define MONZERO_DEX_CHAINS_H
#include <QRegularExpression>
#include <QString>

// The chains the XMZ swap helper trades on, and what differs between them for the app: the
// coin's name and decimals, and what an account address and a transaction id look like. The
// helper pins the contracts and programs; nothing here decides where funds go.
namespace DexChains {
const qint64 SolMainnet=501000101,SolDevnet=501000102,SolLocal=501000103;
inline bool solana(qint64 chainId) {return chainId==SolMainnet || chainId==SolDevnet || chainId==SolLocal;}
inline bool known(qint64 chainId) {
    return chainId==1 || chainId==56 || chainId==11155111 || chainId==97 || chainId==31337 || solana(chainId);
}
inline QString coin(qint64 chainId) {
    return solana(chainId) ? QStringLiteral("SOL") : (chainId==56 || chainId==97) ? QStringLiteral("BNB") : QStringLiteral("ETH");
}
// Wei has 18 decimals, a lamport 9
inline int decimals(qint64 chainId) {return solana(chainId) ? 9 : 18;}
inline bool evmAddress(const QString &text) {
    static const QRegularExpression format("\\A0x[0-9a-fA-F]{40}\\z");
    return format.match(text).hasMatch();
}
// A Solana address: 32 bytes in base58 (32 to 44 characters, no 0 O I l)
inline bool solanaAddress(const QString &text) {
    static const QRegularExpression format("\\A[1-9A-HJ-NP-Za-km-z]{32,44}\\z");
    return format.match(text).hasMatch();
}
inline bool address(qint64 chainId,const QString &text) {return solana(chainId) ? solanaAddress(text) : evmAddress(text);}
// A transaction id as the chain's explorers show it: a hash on EVM, a signature (base58) on Solana
inline bool transactionId(const QString &text) {
    static const QRegularExpression evm("\\A0x[0-9a-f]{64}\\z"),sol("\\A[1-9A-HJ-NP-Za-km-z]{64,88}\\z");
    return evm.match(text).hasMatch() || sol.match(text).hasMatch();
}
}
#endif
