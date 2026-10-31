#ifndef MONZERO_LITECOIN_RESERVATION_JOB_H
#define MONZERO_LITECOIN_RESERVATION_JOB_H
#include <QObject>
#include <QJsonObject>
#include <QMap>
#include <QProcess>
#include <QTimer>
#include <QVariantMap>

// Role-bound recovery plus Litecoin-funder reservation and approved funding.
class LitecoinReservationJob final : public QObject {
    Q_OBJECT
public:
    explicit LitecoinReservationJob(QObject *parent=nullptr);
    ~LitecoinReservationJob() override;
    bool configure(const QString &path,const QString &journalPath);
    bool prepare();
    bool reviewRecovery(const QString &operation);
    bool checkRecovery(const QString &operation);
    bool submitRecovery(const QString &commitment);
    bool reviewFunding();
    bool submitFunding(const QString &commitment);
    QVariantMap status() const;
signals:
    void changed();
    void prepared(const QByteArray &psbt);
private:
    bool sourcesMatch() const;
    bool recoveryAllowed(const QString &operation) const;
    bool run(const QString &operation,const QString &commitment={});
    bool recoveryReviewValid(const QJsonObject &review,const QString &kind) const;
    bool fundingReviewValid(const QJsonObject &review) const;
    void drain();
    void finish(bool success);
    QString m_role="litecoin-funder";
    QString m_path,m_program,m_digest,m_trade,m_quoteCommitment,m_request,m_state="unconfigured";
    QMap<QString,QByteArray> m_sources;
    QJsonObject m_terms,m_rpc;
    QVariantMap m_review,m_fundingReview,m_fundingSubmission,m_pendingFunding;
    QVariantMap m_recoveryReview,m_recoverySubmission,m_pendingRecovery,m_recoveryObservation;
    QString m_recoveryState="not-reviewed",m_recoveryCheckState="not-checked";
    quint64 m_requiredConfirmations=0;
    QString m_operation,m_fundingState="not-reviewed";
    QProcess m_process;
    QTimer m_deadline;
    QByteArray m_output,m_input;
    qint64 m_errors=0;
    bool m_busy=false;
};
#endif
