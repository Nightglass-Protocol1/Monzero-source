#ifndef MONZERO_RELAY_RECEIPT_H
#define MONZERO_RELAY_RECEIPT_H
#include "RelayIngress.h"

// A pinned peer's acknowledgement, never settlement or liquidity evidence.
namespace RelayReceipt {
QByteArray create(const RelayIngress::Evidence &accepted,
                  const RelayEnvelope::Evidence &message,
                  const OfferIdentity &receiver, qint64 acceptedAt);
bool verify(const QByteArray &receipt, const RelayEnvelope::Evidence &message,
            const QByteArray &expectedReceiver, qint64 now);
}
#endif
