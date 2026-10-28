#ifndef MONZERO_DEX_TYPES_H
#define MONZERO_DEX_TYPES_H

#include <QString>

namespace Dex {
    struct TradingPair {
        QString base;
        QString quote;
    };
}
#endif
