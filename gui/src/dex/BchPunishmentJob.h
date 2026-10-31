#ifndef MONZERO_BCH_PUNISHMENT_JOB_H
#define MONZERO_BCH_PUNISHMENT_JOB_H
#include <QObject>
#include <QJsonObject>
#include <QProcess>
#include <QTimer>
#include <QVariantMap>
// Retained review, separately approved one-shot dispatch and chain confirmation.
class BchPunishmentJob final : public QObject {
    Q_OBJECT
public:
    explicit BchPunishmentJob(QObject *parent=nullptr);
    ~BchPunishmentJob() override;
    bool configure(const QString &path);
    bool configureObservation(const QString &path);
    bool configureSubmission(const QString &path);
    bool submit(const QString &commitment);
    bool observe();
    bool review();
    bool refresh();
    QVariantMap status() const;
signals:
    void changed();
private:
    bool sourcesMatch() const;
    bool observationSourcesMatch() const;
    bool submissionSourcesMatch() const;
    bool run(const QString &operation);
    void drain();
    void notifyChanged();
    void finish(bool success);
    QString m_path,m_admissionPath,m_quotePath,m_program,m_digest,m_trade,m_deployment,m_request,m_operation;
    QString m_submissionPath,m_nativeRpcPath;
    QByteArray m_submissionBytes,m_nativeRpcBytes;
    QJsonObject m_approvedReview;
    bool m_submissionValid=false,m_submitAttempted=false;
    QString m_observationPath,m_rpcPath;
    QByteArray m_observationBytes,m_rpcBytes;
    QJsonObject m_observation;
    qint64 m_startedAt=0;
    bool m_observationValid=false;
    QString m_state="unconfigured";
    QByteArray m_wrapperBytes,m_admissionBytes,m_quoteBytes,m_input,m_output;
    QJsonObject m_review,m_result,m_retainedReview;
    QString m_retainedRelayState;
    QProcess m_process;
    QTimer m_deadline;
    qint64 m_errors=0;
    bool m_busy=false,m_sourcesValid=false;
};
#endif
