#ifndef MONZERO_DEX_ENGINE_H
#define MONZERO_DEX_ENGINE_H

#include <QObject>
#include <QVariantMap>
#include <QVariantList>
#include <QStringList>
#include <functional>
#include <QFutureWatcher>

enum class OfferState { Draft, Quoted, Matched, XmzLocked, ExternalLocked, Redeemable, Settled, Refunding, Refunded, Expired, Cancelled };

class DexEngine : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool researchMarketBusy READ researchMarketBusy NOTIFY researchMarketBusyChanged)
public:
    explicit DexEngine(QObject *parent = nullptr);
    ~DexEngine();

    Q_INVOKABLE QVariantMap createLocalOffer(const QString &pair, const QString &side,
                                            const QString &amount, const QString &price,
                                            int lifetimeSeconds);
    Q_INVOKABLE QVariantList localOffers() const;
    QVariantMap readResearchMarket(const QString &path) const;
    Q_INVOKABLE bool refreshResearchMarket(const QString &path);
    bool researchMarketBusy() const { return m_researchBusy; }
    Q_INVOKABLE bool cancelLocalOffer(const QString &id);
    Q_INVOKABLE bool signLocalOffer(const QString &id);
    Q_INVOKABLE bool publishOffer(const QString &id);
    Q_INVOKABLE bool validateOffer(const QVariantMap& offerData) const;
    Q_INVOKABLE bool matchOffers(const QString& buyOfferId, const QString& sellOfferId);
    Q_INVOKABLE QVariantMap previewOrder(const QString &pair, const QString &side,
                                         const QString &amount, const QString &price,
                                         const QString &externalMinerFee) const;
    Q_INVOKABLE bool canTransition(OfferState from, OfferState to) const;
    Q_INVOKABLE bool canTransition(const QString &from, const QString &to) const;
    Q_INVOKABLE QStringList supportedPairs() const;
    Q_INVOKABLE QVariantMap fundingPlan(const QString &payCoin, const QString &receiveCoin,
                                       const QString &unlockedXmz, const QString &protocolFeeXmz,
                                       const QString &networkFeeXmz, const QString &xmzPrincipal) const;
    Q_INVOKABLE QVariantMap selfTest() const;
    Q_INVOKABLE QVariantMap activationReadiness(const QString &pair, const QVariantMap &evidence) const;

signals:
    void researchMarketReady(const QVariantMap &snapshot);
    void researchMarketBusyChanged();
    void offerCreated(const QString& id, const QString& pair);
    void offerCancelled(const QString& id);
    void offerMatched(const QString& buyId, const QString& sellId);
    void errorOccurred(const QString& error);

private:
    QFutureWatcher<QVariantMap> *m_researchWatcher;
    bool m_researchBusy = false;
    QVariantList m_localOffers;
    static bool parseAtomic(const QString &text, int decimals, quint64 &value);
    static QString formatAtomic(quint64 value, int decimals);
};
#endif
