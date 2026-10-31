#ifndef MONZERO_BCH_LIGHT_FUNDING_CONTROLLER_H
#define MONZERO_BCH_LIGHT_FUNDING_CONTROLLER_H
#include "BchLightFundingJob.h"
// Trade-facing operations. The caller supplies the authenticated negotiation's
// identities. Authenticated BCH peer recovery is retained before send approval.
class BchLightFundingController final : public QObject {
    Q_OBJECT
public:
    explicit BchLightFundingController(QObject *parent=nullptr);
    bool configure(const QString &path,LitecoinLightJob *wallet,const QString &helper,const QString &digest,
        const QString &trade,const QString &quote);
    bool setObservationLimits(quint32 gap,quint32 addresses,quint32 entries,quint32 headers);
    bool queryTerms();
    // Retain the selected local wallet in the engine before preparing funds.
    bool selectWalletSource();
    bool prepare(const QString &feeAtomic,quint32 minimumConfirmations);
    bool reopen();
    bool reviewSignature();
    bool sign(const QString &commitment,const QByteArray &password);
    bool reviewRecovery();
    bool retainRecovery(const QString &commitment);
    bool reconcileAcknowledgement();
    bool reviewSend();
    bool send(const QString &commitment);
    bool observe();
    bool reviewRetry();
    bool retry(const QString &commitment);
    void cancel();
    bool busy() const {return m_job.busy();}
    QVariantMap status() const;
    QJsonObject response() const {return (m_failed || m_rejected)?QJsonObject{}:m_job.response();}
signals:
    void changed();
private:
    bool run(const QString &operation,const QJsonObject &payment=QJsonObject{},const QString &approval=QString{},const QByteArray &password=QByteArray{});
    bool approve(const QString &review,const QString &operation,const QString &commitment,const QByteArray &password=QByteArray{});
    void update();
    bool quoteMatches(const QJsonObject &review) const;
    BchLightFundingJob m_job;
    QString m_trade,m_quote,m_operation,m_reviewOperation,m_seen,m_expected;
    quint64 m_expires=0;
    QJsonObject m_terms,m_review,m_result;
    bool m_failed=false,m_rejected=false,m_sourceSelected=false;
    quint32 m_gap=20,m_addresses=100,m_entries=1000,m_headers=10000;
};
#endif
