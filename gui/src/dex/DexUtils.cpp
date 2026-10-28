#include "DexUtils.h"
#include <QRegularExpression>
#include <limits>

namespace DexUtils {

bool parseAtomic(const QString &input, int decimals, quint64 &value)
{
    if (decimals < 1 || decimals > 19) return false;
    const QString text = input.trimmed();
    const QRegularExpression pattern(QStringLiteral("^(0|[1-9][0-9]*)(?:\\.([0-9]{1,%1}))?$").arg(decimals));
    const auto match = pattern.match(text);
    if (!match.hasMatch()) return false;

    QString fraction = match.captured(2);
    fraction += QString(decimals - fraction.size(), QLatin1Char('0'));
    bool wholeOk = false, fractionOk = false;
    const quint64 whole = match.captured(1).toULongLong(&wholeOk);
    const quint64 fractional = fraction.isEmpty() ? 0 : fraction.toULongLong(&fractionOk);
    if (!wholeOk || (!fraction.isEmpty() && !fractionOk)) return false;

    quint64 scale = 1;
    for (int i = 0; i < decimals; ++i) {
        if (scale > std::numeric_limits<quint64>::max() / 10) return false;
        scale *= 10;
    }

    if (whole > (std::numeric_limits<quint64>::max() - fractional) / scale) return false;
    value = whole * scale + fractional;
    return true;
}

QString formatAtomic(quint64 value, int decimals)
{
    if (decimals < 1 || decimals > 19) return QString();
    quint64 scale = 1;
    for (int i = 0; i < decimals; ++i) {
        if (scale > std::numeric_limits<quint64>::max() / 10) return QString::number(value);
        scale *= 10;
    }

    QString fraction = QString::number(value % scale).rightJustified(decimals, QLatin1Char('0'));
    while (fraction.endsWith(QLatin1Char('0'))) fraction.chop(1);
    return QString::number(value / scale) + (fraction.isEmpty() ? QString() : QStringLiteral(".") + fraction);
}

QVariantMap createErrorResponse(const QString &error)
{
    QVariantMap result;
    result["valid"] = false;
    result["error"] = error;
    return result;
}

QVariantMap createSuccessResponse(const QVariantMap &data)
{
    QVariantMap result = data;
    result["valid"] = true;
    return result;
}

}
