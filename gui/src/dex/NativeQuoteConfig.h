#ifndef MONZERO_NATIVE_QUOTE_CONFIG_H
#define MONZERO_NATIVE_QUOTE_CONFIG_H
#include "NativeTradeQuote.h"
struct NativeQuoteConfig {
    QString helper, helperDigest, directory;
    QByteArray btcPeer, xmzPeer, payload, btcSignature, xmzSignature;
    NativeTradeQuote::DeploymentPolicy policy;
    // Explicit private local configuration only; never a downloaded peer file.
    static bool load(const QString &path, qint64 now, NativeQuoteConfig &output);
};
#endif
