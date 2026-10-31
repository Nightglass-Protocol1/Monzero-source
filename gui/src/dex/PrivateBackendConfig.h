#ifndef MONZERO_PRIVATE_BACKEND_CONFIG_H
#define MONZERO_PRIVATE_BACKEND_CONFIG_H
#include <QString>

// Explicit local configuration for a fresh, separate experimental RPC service.
// No wallet file, password, seed, peer endpoint or trading activation is accepted.
struct PrivateBackendConfig {
    QString directory, helper, helperDigest, rpc, rpcDigest, genesis;
    quint16 daemonPort=0, walletPort=0;
    static bool load(const QString &path, PrivateBackendConfig &result);
};
#endif
