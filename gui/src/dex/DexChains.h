#ifndef MONZERO_DEX_CHAINS_H
#define MONZERO_DEX_CHAINS_H
#include <QRegularExpression>
#include <QString>

// The chains the XMZ swap helper trades on, and what differs between them for the app: the
// coin's name and decimals, and what an account address and a transaction id look like. The
// helper pins the contracts and programs; nothing here decides where funds go.
namespace DexChains {
const qint64 SolMainnet=501000101,SolDevnet=501000102,SolLocal=501000103;
// USDT on Solana: the app's own market ids (the chain's id + 100). The helper trades them on
// the Solana chain with the pair "USDT/XMZ"; everywhere else in the app they are their own market
// (own limits document, decimals, offers folder, chart).
const qint64 SolUsdtMainnet=501000201,SolUsdtDevnet=501000202,SolUsdtLocal=501000203;
inline bool usdt(qint64 id) {return id==SolUsdtMainnet || id==SolUsdtDevnet || id==SolUsdtLocal;}
// The chain the helper trades a market on
inline qint64 helperChain(qint64 id) {return usdt(id) ? id-100 : id;}
// The market of a trade the helper describes: its chain, and a token asset (non-zero) for USDT
inline qint64 marketOf(qint64 chainId,const QString &asset) {
    static const QRegularExpression zero("\\A(0x)?0*\\z");
    const bool sol=chainId==SolMainnet || chainId==SolDevnet || chainId==SolLocal;
    return sol && !asset.isEmpty() && !zero.match(asset).hasMatch() ? chainId+100 : chainId;
}
// Solana addresses and transactions, for the chain's coin and its USDT alike
inline bool solana(qint64 id) {return id==SolMainnet || id==SolDevnet || id==SolLocal || usdt(id);}
inline bool known(qint64 chainId) {
    return chainId==1 || chainId==56 || chainId==11155111 || chainId==97 || chainId==31337 || solana(chainId);
}
inline QString coin(qint64 id) {
    return usdt(id) ? QStringLiteral("USDT") : solana(id) ? QStringLiteral("SOL") : (id==56 || id==97) ? QStringLiteral("BNB") : QStringLiteral("ETH");
}
inline QString pair(qint64 id) {return coin(id)+QStringLiteral("/XMZ");}
// Wei has 18 decimals, a lamport 9, USDT 6
inline int decimals(qint64 id) {return usdt(id) ? 6 : solana(id) ? 9 : 18;}
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
