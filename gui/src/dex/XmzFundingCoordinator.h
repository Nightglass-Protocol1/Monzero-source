#ifndef MONZERO_XMZ_FUNDING_COORDINATOR_H
#define MONZERO_XMZ_FUNDING_COORDINATOR_H

#include "BalanceReservationStore.h"
#include <QJsonObject>
#include <QVariantMap>

class XmzFundingCoordinator final
{
public:
    explicit XmzFundingCoordinator(const QString &reservationPath);
    QVariantMap reserve(const QString &offerId, const QString &requiredAtomic,
                        const QJsonObject &walletBalance, const QJsonObject &walletHeight,
                        const QJsonObject &nodeInfo, const QString &actualGenesis,
                        const QString &expectedGenesis);
    BalanceReservationStore &store();

private:
    BalanceReservationStore m_store;
};

#endif
