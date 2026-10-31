#ifndef MONZERO_LITECOIN_LIGHT_FUNDING_CONTROLLER_H
#define MONZERO_LITECOIN_LIGHT_FUNDING_CONTROLLER_H
#include "LitecoinLightFundingJob.h"
// Trade-facing operations. The caller supplies the authenticated negotiation's
// identities and connects prepared PSBTs to its existing peer recovery session.
class LitecoinLightFundingController final : public QObject {
    Q_OBJECT
public:
    explicit LitecoinLightFundingController(QObject *parent=nullptr);
    bool configure(const QString &path,LitecoinLightJob *wallet,const QString &helper,const QString &digest,
        const QString &trade,const QString &quote);
    bool setObservationLimits(quint32 gap,quint32 addresses,quint32 entries,quint32 headers);
    bool queryTerms();
    bool prepare(const QString &feeAtomic,quint32 minimumConfirmations);
    bool reopen();
    bool reviewSignature();
    bool sign(const QString &commitment,const QByteArray &password);
    bool reviewSend();
    bool send(const QString &commitment);
    bool observe();
    bool reviewRetry();
    bool retry(const QString &commitment);
    void cancel();
    QVariantMap status() const;
    QJsonObject response() const {return m_failed?QJsonObject{}:m_job.response();}
signals:
    void changed();
    void prepared(const QByteArray &psbt);
private:
    bool run(const QString &operation,const QJsonObject &payment=QJsonObject{},const QString &approval=QString{},const QByteArray &password=QByteArray{});
    bool approve(const QString &review,const QString &operation,const QString &commitment,const QByteArray &password=QByteArray{});
    void update();
    bool quoteMatches(const QJsonObject &review) const;
    LitecoinLightFundingJob m_job;
    QString m_trade,m_quote,m_operation,m_reviewOperation,m_seen;
    QJsonObject m_terms,m_review,m_result;
    bool m_failed=false,m_rejected=false;
    quint32 m_gap=20,m_addresses=100,m_entries=1000,m_headers=10000;
};
#endif
