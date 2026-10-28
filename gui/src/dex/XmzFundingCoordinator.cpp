#include "XmzFundingCoordinator.h"
#include "NodeRpcValidator.h"
#include <QJsonValue>
#include <cmath>

namespace {
bool safeInteger(const QJsonValue &value, quint64 &number)
{
    if (!value.isDouble()) return false;
    const double candidate = value.toDouble();
    if (!std::isfinite(candidate) || candidate < 0 || candidate != std::floor(candidate)
        || candidate > 9007199254740991.0) return false;
    number = static_cast<quint64>(candidate);
    return true;
}

QVariantMap rejected(const QString &error)
{
    return {{"valid", false}, {"reserved", false}, {"balanceVerified", false},
            {"executable", false}, {"error", error}};
}
}

XmzFundingCoordinator::XmzFundingCoordinator(const QString &reservationPath)
    : m_store(reservationPath) {}

BalanceReservationStore &XmzFundingCoordinator::store() { return m_store; }

QVariantMap XmzFundingCoordinator::reserve(const QString &offerId, const QString &requiredAtomic,
                                           const QJsonObject &walletBalance,
                                           const QJsonObject &walletHeight,
                                           const QJsonObject &nodeInfo,
                                           const QString &actualGenesis,
                                           const QString &expectedGenesis)
{
    if (!m_store.ready()) return rejected(m_store.error());
    const QVariantMap node = NodeRpcValidator::monzero(nodeInfo, actualGenesis, expectedGenesis);
    if (!node.value("ready").toBool()) return rejected("Monzero node evidence is not ready");
    quint64 balance = 0, unlocked = 0, walletBlock = 0, nodeBlock = 0;
    if (!safeInteger(walletBalance.value("balance"), balance)
        || !safeInteger(walletBalance.value("unlocked_balance"), unlocked)
        || !safeInteger(walletHeight.value("height"), walletBlock)
        || !safeInteger(nodeInfo.value("height"), nodeBlock))
        return rejected("Malformed or unsafe wallet balance evidence");
    if (walletBalance.value("multisig_import_needed") != QJsonValue(false) || unlocked > balance)
        return rejected("Wallet balance state is not spendable");
    if (walletBlock != nodeBlock) return rejected("Wallet is not synchronized to the validated node");
    if (!m_store.reserve(offerId, requiredAtomic, QString::number(unlocked)))
        return rejected("Insufficient unreserved XMZ or invalid reservation");
    return {{"valid", true}, {"reserved", true}, {"balanceVerified", true},
            {"unlockedAtomic", QString::number(unlocked)}, {"heldAtomic", m_store.heldAtomic()},
            {"executable", false},
            {"executionBlocker", "Settlement and refund protocol is not verified"}};
}
