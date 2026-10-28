#include "DexEngine.h"
#include "DexUtils.h"
#include "OfferBookStore.h"

#include <QRegularExpression>
#include <QHash>
#include <QDateTime>
#include <QUuid>
#include <limits>
#include <QtConcurrent/QtConcurrentRun>
#include <QPointer>

namespace {
constexpr int base_decimals = 8;
constexpr int xmz_decimals = 11;
constexpr quint64 base_scale = 100000000ULL;
constexpr quint64 xmz_network_fee = 250000000ULL; // 0.0025 XMZ
constexpr quint64 minimum_net_xmz = 100000000000ULL; // 1 XMZ
constexpr quint64 protocol_fee_bps = 10; // 0.10%, success path only
}

DexEngine::DexEngine(QObject *parent) : QObject(parent),
    m_researchWatcher(new QFutureWatcher<QVariantMap>(this))
{
    connect(m_researchWatcher, &QFutureWatcher<QVariantMap>::finished, this, [this] {
        const auto snapshot = m_researchWatcher->result();
        QPointer<DexEngine> guard(this);
        emit researchMarketReady(snapshot);
        if (!guard) return;
        m_researchBusy = false;
        emit researchMarketBusyChanged();
    });
}

bool DexEngine::refreshResearchMarket(const QString &path)
{
    if (m_researchBusy) return false;
    m_researchBusy = true;
    // The worker owns only a path copy, never this engine or a live writer.
    m_researchWatcher->setFuture(QtConcurrent::run([path] {
        return OfferBookStore::readSnapshot(path, "monzero-dex-offer-v1",
            "isolated-btc-xmz", QDateTime::currentSecsSinceEpoch());
    }));
    emit researchMarketBusyChanged();
    return true;
}

QVariantMap DexEngine::readResearchMarket(const QString &path) const
{
    return OfferBookStore::readSnapshot(path, "monzero-dex-offer-v1",
        "isolated-btc-xmz", QDateTime::currentSecsSinceEpoch());
}

bool DexEngine::parseAtomic(const QString &input, int decimals, quint64 &value)
{
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
    for (int i = 0; i < decimals; ++i) scale *= 10;
    if (whole > (std::numeric_limits<quint64>::max() - fractional) / scale) return false;
    value = whole * scale + fractional;
    return value != 0;
}

QString DexEngine::formatAtomic(quint64 value, int decimals)
{
    quint64 scale = 1;
    for (int i = 0; i < decimals; ++i) scale *= 10;
    QString fraction = QString::number(value % scale).rightJustified(decimals, QLatin1Char('0'));
    while (fraction.endsWith(QLatin1Char('0'))) fraction.chop(1);
    return QString::number(value / scale) + (fraction.isEmpty() ? QString() : QStringLiteral(".") + fraction);
}

QStringList DexEngine::supportedPairs() const
{
    return {QStringLiteral("BTC / XMZ"), QStringLiteral("LTC / XMZ"), QStringLiteral("BCH / XMZ")};
}

QVariantMap DexEngine::previewOrder(const QString &pair, const QString &side,
                                    const QString &amount, const QString &price,
                                    const QString &externalMinerFee) const
{
    QVariantMap result{{QStringLiteral("valid"), false}};
    if (!supportedPairs().contains(pair)) { result["error"] = QStringLiteral("Unsupported or disabled market"); return result; }
    if (side != QLatin1String("BUY") && side != QLatin1String("SELL")) { result["error"] = QStringLiteral("Invalid order side"); return result; }

    quint64 baseAtomic = 0, priceAtomic = 0;
    if (!parseAtomic(amount, base_decimals, baseAtomic)) { result["error"] = QStringLiteral("Amount must be positive with at most 8 decimals"); return result; }
    if (!parseAtomic(price, xmz_decimals, priceAtomic)) { result["error"] = QStringLiteral("Price must be positive with at most 11 decimals"); return result; }

    const unsigned __int128 product = static_cast<unsigned __int128>(baseAtomic) * priceAtomic;
    const unsigned __int128 grossWide = product / base_scale;
    if (grossWide > std::numeric_limits<quint64>::max()) { result["error"] = QStringLiteral("Order total is too large"); return result; }
    const quint64 gross = static_cast<quint64>(grossWide);
    const quint64 protocolFee = static_cast<quint64>((static_cast<unsigned __int128>(gross) * protocol_fee_bps + 9999) / 10000);
    if (gross <= protocolFee + xmz_network_fee || gross - protocolFee - xmz_network_fee < minimum_net_xmz) {
        result["error"] = QStringLiteral("Trade is below the 1 XMZ minimum after fees"); return result;
    }

    if (side == "BUY" && gross > std::numeric_limits<quint64>::max() - protocolFee - xmz_network_fee) {
        result["error"] = "Order debit is too large"; return result;
    }
    result["valid"] = true;
    result["executable"] = false;
    result["settlementLabel"] = side == "BUY" ? "XMZ debit" : "XMZ proceeds";
    result["settlementXmz"] = formatAtomic(side == "BUY" ? gross + protocolFee + xmz_network_fee
        : gross - protocolFee - xmz_network_fee, xmz_decimals);
    result["pair"] = pair;
    result["side"] = side;
    result["amount"] = formatAtomic(baseAtomic, base_decimals);
    result["grossXmz"] = formatAtomic(gross, xmz_decimals);
    result["protocolFeeXmz"] = formatAtomic(protocolFee, xmz_decimals);
    result["networkFeeXmz"] = formatAtomic(xmz_network_fee, xmz_decimals);
    result["netXmz"] = formatAtomic(gross - protocolFee - xmz_network_fee, xmz_decimals);
    result["externalMinerFee"] = externalMinerFee;
    result["state"] = QStringLiteral("draft");
    return result;
}

bool DexEngine::canTransition(const QString &from, const QString &to) const
{
    static const QHash<QString, QStringList> transitions{
        {"draft", {"quoted", "cancelled"}}, {"quoted", {"matched", "expired", "cancelled"}},
        {"matched", {"xmz_locked", "expired", "refunding"}}, {"xmz_locked", {"external_locked", "refunding"}},
        {"external_locked", {"redeemable", "refunding"}}, {"redeemable", {"settled", "refunding"}},
        {"refunding", {"refunded"}}
    };
    return transitions.value(from).contains(to);
}

QVariantMap DexEngine::selfTest() const
{
    QStringList failures;
    int checks = 0;
    const auto expect = [&](bool condition, const QString &message) {
        ++checks;
        if (!condition) failures.append(message);
    };

    const QVariantMap valid = previewOrder("BTC / XMZ", "BUY", "0.001", "1172500", "0.000020 BTC");
    expect(valid.value("valid").toBool(), "valid BTC order rejected");
    expect(valid.value("grossXmz").toString() == "1172.5", "gross atomic calculation mismatch");
    expect(valid.value("protocolFeeXmz").toString() == "1.1725", "protocol fee mismatch");
    expect(valid.value("networkFeeXmz").toString() == "0.0025", "network fee mismatch");
    expect(valid.value("netXmz").toString() == "1171.325", "net proceeds mismatch");
    expect(!previewOrder("ETH / XMZ", "BUY", "1", "1", "0 ETH").value("valid").toBool(), "unsupported pair accepted");
    expect(!previewOrder("BTC / XMZ", "HOLD", "1", "1", "0 BTC").value("valid").toBool(), "invalid side accepted");
    expect(!previewOrder("BTC / XMZ", "BUY", "0.000000001", "1", "0 BTC").value("valid").toBool(), "over-precision amount accepted");
    expect(!previewOrder("BTC / XMZ", "BUY", "0.00000001", "1", "0 BTC").value("valid").toBool(), "below-minimum order accepted");
    expect(canTransition("draft", "quoted"), "valid draft transition rejected");
    expect(canTransition("redeemable", "settled"), "valid settlement transition rejected");
    expect(canTransition("refunding", "refunded"), "valid refund transition rejected");
    expect(!canTransition("draft", "settled"), "unsafe state jump accepted");
    expect(!canTransition("refunded", "settled"), "terminal state transition accepted");

    return {{"passed", failures.isEmpty()}, {"checks", checks}, {"failures", failures}};
}

DexEngine::~DexEngine() = default;

QVariantMap DexEngine::createLocalOffer(const QString &pair, const QString &side,
    const QString &amount, const QString &price, int lifetimeSeconds)
{
    if (lifetimeSeconds < 60 || lifetimeSeconds > 86400)
        return {{"valid", false}, {"error", "Lifetime must be between 60 and 86400 seconds"}};
    auto result = previewOrder(pair, side, amount, price, "Not estimated");
    if (!result.value("valid").toBool()) return result;
    m_localOffers = localOffers();
    if (m_localOffers.size() >= 100)
        return {{"valid", false}, {"error", "Local offer limit reached"}};
    result["id"] = QUuid::createUuid().toString(QUuid::WithoutBraces);
    result["price"] = price;
    result["expiresAt"] = QDateTime::currentSecsSinceEpoch() + lifetimeSeconds;
    result["state"] = "local_unsigned";
    result["executable"] = false;
    m_localOffers.append(result);
    emit offerCreated(result.value("id").toString(), pair);
    return result;
}
QVariantList DexEngine::localOffers() const
{
    QVariantList result;
    for (const auto &offer : m_localOffers)
        if (offer.toMap().value("expiresAt").toLongLong() > QDateTime::currentSecsSinceEpoch())
            result.append(offer);
    return result;
}
bool DexEngine::cancelLocalOffer(const QString &id)
{
    m_localOffers = localOffers();
    for (int i = 0; i < m_localOffers.size(); ++i)
        if (m_localOffers[i].toMap().value("id").toString() == id) {
            m_localOffers.removeAt(i);
            emit offerCancelled(id);
            return true;
        }
    return false;
}
// Identity primitives alone do not implement validated network offers or settlement.
bool DexEngine::signLocalOffer(const QString &) { return false; }
bool DexEngine::publishOffer(const QString &) { return false; }
bool DexEngine::validateOffer(const QVariantMap &) const { return false; }
bool DexEngine::matchOffers(const QString &, const QString &) { return false; }
bool DexEngine::canTransition(OfferState from, OfferState to) const
{
    const QStringList names{"draft", "quoted", "matched", "xmz_locked", "external_locked",
        "redeemable", "settled", "refunding", "refunded", "expired", "cancelled"};
    return canTransition(names.value(static_cast<int>(from)), names.value(static_cast<int>(to)));
}
QVariantMap DexEngine::activationReadiness(const QString &, const QVariantMap &) const
{
    return {{"ready", false}, {"checksPassed", 0}, {"checksRequired", 2},
        {"feeEnforcementVerified", false},
        {"blockers", QStringList{"Live settlement, adapters and recovery are not verified",
            "Success-only XMZ fee enforcement against modified counterparties is not proved"}}};
}
QVariantMap DexEngine::fundingPlan(const QString &payCoin, const QString &receiveCoin,
    const QString &unlockedXmz, const QString &protocolFeeXmz,
    const QString &networkFeeXmz, const QString &xmzPrincipal) const
{
    QVariantMap result{{"valid", false}, {"funded", false}, {"executable", false},
        {"balanceVerified", false}, {"feeCurrency", "XMZ"}};
    const auto reject = [&](const QString &reason) {
        result["error"] = reason;
        return result;
    };
    const QStringList assets{"XMZ", "BTC", "LTC", "BCH"};
    if (!assets.contains(payCoin) || !assets.contains(receiveCoin) || payCoin == receiveCoin)
        return reject("Unsupported or identical assets");
    quint64 balance = 0, protocolFee = 0, networkFee = 0, principal = 0;
    if (!DexUtils::parseAtomic(unlockedXmz, 11, balance)
        || !DexUtils::parseAtomic(protocolFeeXmz, 11, protocolFee)
        || !DexUtils::parseAtomic(networkFeeXmz, 11, networkFee)
        || !DexUtils::parseAtomic(xmzPrincipal, 11, principal))
        return reject("Invalid XMZ amount or precision");
    if (!protocolFee || !networkFee)
        return reject("A positive quoted protocol fee and network fee are required");
    const bool acquiring = receiveCoin == "XMZ";
    const bool spending = payCoin == "XMZ";
    if ((acquiring || spending) != (principal > 0))
        return reject("XMZ principal must be positive exactly when trading XMZ");
    const quint64 maximum = std::numeric_limits<quint64>::max();
    if (protocolFee > maximum - networkFee) return reject("Fee total overflow");
    const quint64 fees = protocolFee + networkFee;
    quint64 required = fees, net = 0;
    if (acquiring) {
        if (principal <= fees || principal - fees < minimum_net_xmz)
            return reject("Acquisition must leave at least 1 XMZ after fees");
        net = principal - fees;
        required = 0;
    } else if (spending) {
        if (principal > maximum - fees) return reject("Funding requirement overflow");
        required += principal;
    }
    result["valid"] = true;
    // This is arithmetic over caller-supplied values, never proof of wallet funds.
    result["funded"] = balance >= required;
    result["requiredUnlockedXmz"] = formatAtomic(required, 11);
    result["shortfallXmz"] = formatAtomic(balance >= required ? 0 : required - balance, 11);
    result["netReceiveXmz"] = formatAtomic(net, 11);
    result["feeSource"] = acquiring ? "deduct_from_received_xmz" : "unlocked_xmz";
    result["nextAction"] = balance >= required ? "verify_quote_and_wallet_funds" : "buy_xmz_first";
    result["executionBlocker"] = "Unauthenticated quote and balance; settlement and recovery unavailable";
    return result;
}
