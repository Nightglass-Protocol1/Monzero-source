#ifndef MONZERO_EVM_TRADE_JOB_H
#define MONZERO_EVM_TRADE_JOB_H
#include <QByteArray>
#include <QJsonObject>
#include <QObject>
#include <QProcess>
#include <QTimer>
#include <QVariantMap>
// One XMZ<->BNB/ETH trade through the pinned monzero-dex-evm-helper (one JSON request per run).
// status: journal view, no secrets. approve: only the commitment the last status showed, for the
// trade this job is pinned to. tick: one engine step; the wallet seed comes from native vault
// code only (never QML) and is wiped after it is written to the helper.
class EvmTradeJob final : public QObject {
    Q_OBJECT
public:
    explicit EvmTradeJob(QObject *parent=nullptr);
    ~EvmTradeJob() override;
    bool configure(const QString &path);
    bool refresh();
    bool approve(const QString &commitment);
    bool tick(QByteArray &walletSeed);
    // After the trade: send what its account holds to the user's own wallet (e.g. MetaMask)
    bool payout(QByteArray &walletSeed,const QString &address);
    QVariantMap status() const;
signals:
    void changed();
private:
    bool run(const QJsonObject &request);
    bool tickConfig(QByteArray &walletSeed,QJsonObject *out) const;
    bool sourceMatches() const;
    void drain();
    void finish(bool success);
    QString m_path,m_program,m_digest,m_tradeDir,m_trade,m_commitment,m_command,m_state="unconfigured",m_error,m_pendingApproval,m_stepError;
    QByteArray m_source,m_input,m_output;
    QJsonObject m_config,m_snapshot;
    QVariantMap m_funding,m_payout;
    QProcess m_process;
    QTimer m_deadline;
    qint64 m_errors=0;
    bool m_busy=false,m_awaitingApproval=false;
};
#endif
