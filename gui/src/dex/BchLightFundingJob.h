#ifndef MONZERO_BCH_LIGHT_FUNDING_JOB_H
#define MONZERO_BCH_LIGHT_FUNDING_JOB_H
#include <QObject>
#include <QProcess>
#include <QTimer>
#include <QJsonObject>
#include <QVariantMap>
#include <QPointer>
#include "LitecoinLightJob.h"
#include "BchLightFundingContext.h"

// Local regtest funding transport. The caller owns selection, review UI and
// retained request context; this class never automatically signs or retries.
class BchLightFundingJob final : public QObject {
    Q_OBJECT
public:
    explicit BchLightFundingJob(QObject *parent=nullptr);
    ~BchLightFundingJob() override;
    bool configure(const QString &configuration,const QString &helper,const QString &trustedDigest);
    bool configureWallet(const QString &configuration,LitecoinLightJob *wallet,const QString &helper,const QString &trustedDigest);
    bool run(const QString &operation,const QJsonObject &request,const QByteArray &password=QByteArray{});
    void cancel();
    bool busy() const {return m_busy || m_finishing || m_starting;}
    QVariantMap status() const;
    QJsonObject response() const {return m_response;}
signals:
    void changed();
private:
    bool sourcesMatch() const;
    bool launch(const QString &operation,const QJsonObject &request,const QByteArray &password);
    bool validResponse(const QJsonObject &response) const;
    void drain();
    void finish(bool success);
    QString m_path,m_walletPath,m_quotePath,m_program,m_digest,m_context,m_trade,m_operation,m_requestId,m_state="unconfigured";
    QByteArray m_configuration,m_wallet,m_quoteBytes,m_input,m_output;
    QJsonObject m_request,m_response;
    QProcess m_process;
    QTimer m_deadline;
    qint64 m_errors=0;
    bool m_busy=false,m_walletBound=false,m_finishing=false,m_starting=false;
    QPointer<LitecoinLightJob> m_activeWallet;
    QString m_contextDirectory;
    BchLightFundingContext::Retained m_retained;
};
#endif
