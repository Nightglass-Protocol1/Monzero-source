#ifndef MONZERO_NATIVE_TRADE_QUOTE_H
#define MONZERO_NATIVE_TRADE_QUOTE_H
#include <QByteArray>
#include <QJsonObject>
#include <QVariantMap>

// Commercial terms only, not accepted evidence or transaction authority.
// Address decoding, pinned deployment policy, durable replay
// protection and negotiated engine transcript validation remain separate.
namespace NativeTradeQuote {
// Provisioned by native startup from the selected engine deployment, not a
// peer quote. Exact matching does not itself prove safe cross-chain timing.
struct DeploymentPolicy {
    QString btcGenesis, xmzGenesis, btcNetwork, xmzNetwork;
    quint32 btcConfirmations = 0, xmzConfirmations = 0;
    quint32 cancelBlocks = 0, punishBlocks = 0;
    quint64 maximumBtcPrincipal = 0, maximumXmzPrincipal = 0;
    quint64 maximumBtcNativeFee = 0, maximumXmzNativeFee = 0;
};
QByteArray signatureDomain();
QByteArray encode(const QJsonObject &terms, qint64 now);
bool decode(const QByteArray &payload, qint64 now, QJsonObject &output);
QByteArray commitment(const QJsonObject &terms, qint64 now);
// Presentation only; exact signed amounts remain authoritative.
QVariantMap displayAmounts(const QJsonObject &terms, qint64 now);
// Expected identities come from trusted peer selection, never from the message.
// Success proves fresh mutually signed terms only, not durable acceptance.
bool verifySignatures(const QByteArray &payload, qint64 now,
    const QByteArray &btcSignature, const QByteArray &xmzSignature,
    const QByteArray &expectedBtcFunder, const QByteArray &expectedXmzFunder,
    QJsonObject &output);
QByteArray btcSignatureDomain();
QByteArray xmzSignatureDomain();
bool verifyForDeployment(const QByteArray &payload, qint64 now,
    const QByteArray &btcSignature, const QByteArray &xmzSignature,
    const QByteArray &expectedBtcFunder, const QByteArray &expectedXmzFunder,
    const DeploymentPolicy &policy, QJsonObject &output);
}
#endif
