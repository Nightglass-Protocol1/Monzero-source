#ifndef MONZERO_DEX_UTILS_H
#define MONZERO_DEX_UTILS_H

#include <QString>
#include <QVariantMap>

namespace DexUtils {
    QString formatAtomic(quint64 value, int decimals);
    bool parseAtomic(const QString &text, int decimals, quint64 &value);
    QVariantMap createErrorResponse(const QString &error);
    QVariantMap createSuccessResponse(const QVariantMap &data);
}

#endif
