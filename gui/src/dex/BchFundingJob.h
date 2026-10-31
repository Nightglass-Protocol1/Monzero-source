#ifndef MONZERO_BCH_FUNDING_JOB_H
#define MONZERO_BCH_FUNDING_JOB_H
#include <QObject>
#include <QJsonObject>
#include <QProcess>
#include <QTimer>
#include <QVariantMap>

// Private, explicitly approved relay of retained funding; status never sends.
class BchFundingJob final : public QObject {
    Q_OBJECT
public:
    explicit BchFundingJob(QObject *parent=nullptr);
    ~BchFundingJob() override;
    bool configure(const QString &path);
    bool configureSubmission(const QString &path);
    bool review();
    bool submit(const QString &commitment);
    bool refresh();
    bool observe();
    QVariantMap status() const;
signals:
    void changed();
private:
    bool sourcesMatch() const;
    bool submissionSourcesMatch() const;
    bool validObservation(const QJsonObject &observation,const QJsonObject &review) const;
    bool validReview(const QJsonObject &review,bool fresh) const;
    bool run(const QString &operation,const QString &commitment={});
    void drain();
    void finish(bool success);
    QString m_path,m_admissionPath,m_quotePath,m_program,m_digest,m_trade,m_deployment,m_request;
    QString m_submissionPath,m_rpcPath,m_operation;
    QString m_state="unconfigured";
    QByteArray m_wrapperBytes,m_admissionBytes,m_quoteBytes,m_input,m_output;
    QByteArray m_submissionBytes,m_rpcBytes;
    QJsonObject m_expectedAdmission,m_review,m_savedReview,m_approvedReview,m_result,m_observation;
    QString m_relayState;
    QProcess m_process;
    QTimer m_deadline;
    qint64 m_errors=0,m_startedAt=0;
    bool m_busy=false,m_sourcesValid=false,m_submissionSourcesValid=false;
    bool m_submitAttempted=false,m_statusKnown=false,m_chainSeen=false;
};
#endif
