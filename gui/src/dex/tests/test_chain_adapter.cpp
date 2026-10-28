#include "../ChainAdapter.h"

#include <QtTest>

class ReadOnlyTestAdapter final : public ChainAdapter
{
public:
    QString symbol() const override { return "BTC"; }
    QString network() const override { return "btc-regtest"; }
    int decimals() const override { return 8; }
    QVariantMap health() const override
    {
        return {{"validated", true}, {"executable", false}};
    }
    QVariantMap estimateFee(int = 6) const override
    {
        return {{"validated", false}, {"executable", false}};
    }
    QVariantMap transactionStatus(const QString &) const override
    {
        return {{"validated", false}, {"executable", false}};
    }
    QVariantMap balanceEvidence() const override
    {
        return {{"validated", false}, {"executable", false}};
    }
    void refresh() override {}
};

class ChainAdapterTest : public QObject
{
    Q_OBJECT
private slots:
    void remainsObservational()
    {
        ReadOnlyTestAdapter adapter;
        QCOMPARE(adapter.symbol(), QString("BTC"));
        QCOMPARE(adapter.network(), QString("btc-regtest"));
        QCOMPARE(adapter.decimals(), 8);
        QVERIFY(adapter.health().value("validated").toBool());
        QVERIFY(!adapter.health().value("executable").toBool());
        QVERIFY(!adapter.estimateFee().value("executable").toBool());
        QVERIFY(!adapter.transactionStatus(QString(64, 'a')).value("executable").toBool());
        QVERIFY(!adapter.balanceEvidence().value("executable").toBool());
    }
};

QTEST_GUILESS_MAIN(ChainAdapterTest)
#include "test_chain_adapter.moc"
