#ifndef MONZERO_DEX_TRADE_KEY_STORE_H
#define MONZERO_DEX_TRADE_KEY_STORE_H
#include <QByteArray>
#include <QString>

// A self-contained store for the DEX trade keys (XMZ<->ETH/BNB/SOL), used where no bundled light
// wallet holds them (Windows): the two 32-byte secrets the swap helper needs — the swap seed and
// the EVM account key — are generated here and kept encrypted with the trading password (Argon2id
// + XSalsa20-Poly1305, libsodium). The EVM address and Solana account are derived by the helper
// from these keys, so this store needs no elliptic-curve code of its own.
//
// On Linux the keys come from the Bitcoin light wallet's vault instead; this store is the path for
// builds that do not bundle that helper. The file never leaves this device and holds no address,
// only the password-encrypted secrets.
class DexTradeKeyStore {
public:
    struct Keys { QByteArray swapSeedHex, evmKeyHex; bool ok=false; };
    explicit DexTradeKeyStore(const QString &path) : m_path(path) {}
    bool exists() const;
    // Create the store (fresh random keys) encrypted with `password`; fails if one already exists.
    // Returns the keys on success so the caller can unlock in the same step.
    Keys create(const QByteArray &password);
    // Decrypt the stored keys with `password`; ok=false on a wrong password or a damaged file.
    Keys unlock(const QByteArray &password) const;
private:
    QString m_path;
};
#endif
