#ifndef MONZERO_DEX_CHAIN_ADAPTER_H
#define MONZERO_DEX_CHAIN_ADAPTER_H

#include <QString>
#include <QVariantMap>

// Strictly observational boundary between DEX verification and a chain node.
// Signing, transaction construction and broadcast deliberately do not belong on
// this interface. A caller must not infer execution authority from any result.
class ChainAdapter
{
public:
    virtual ~ChainAdapter() = default;

    // Basic info
    virtual QString symbol() const = 0;
    virtual QString network() const = 0;
    virtual int decimals() const = 0;

    // Each map must be normalized and validated by a chain-specific evidence
    // boundary before it is used by settlement logic.
    virtual QVariantMap health() const = 0;
    virtual QVariantMap estimateFee(int confirmationTarget = 6) const = 0;
    virtual QVariantMap transactionStatus(const QString &transactionId) const = 0;
    virtual QVariantMap balanceEvidence() const = 0;
    virtual void refresh() = 0;
};

#endif
