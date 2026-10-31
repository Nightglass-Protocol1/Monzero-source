#ifndef MONZERO_BCH_PREPARATION_JOB_H
#define MONZERO_BCH_PREPARATION_JOB_H
#include <QObject>
#include <QJsonObject>
#include <QProcess>
#include <QTimer>
#include <QVariantMap>

// Private preparation, advisory locks and explicitly approved signing. No relay.
class BchPreparationJob final : public QObject {
    Q_OBJECT
public:
    explicit BchPreparationJob(QObject *parent=nullptr);
    ~BchPreparationJob() override;
    bool configure(const QString &path);
    bool configureSigningRecovery(const QString &path);
    bool reviewSigningRecovery();
    bool importSigningRecovery(const QString &commitment);
    bool prepare();
    bool refresh();
    bool reviewLocks();
    bool checkLocks();
    bool lockInputs(const QString &commitment);
    bool reviewSigning();
    bool checkSigning();
    bool sign(const QString &commitment);
    QVariantMap status() const;
signals:
    void changed();
private:
    bool sourcesMatch() const;
    bool validRecoveryReview(const QJsonObject &review) const;
    bool validSigningReview(const QJsonObject &review) const;
    bool validSigningState(const QJsonObject &response) const;
    bool validReview(const QJsonObject &review) const;
    bool validLockReview(const QJsonObject &review) const;
    bool validLockIntent(const QJsonValue &intent,const QJsonObject &preparation) const;
    bool validLockObservation(const QJsonObject &observation) const;
    bool run(const QString &action,const QString &commitment={});
    void drain();
    void finish(bool success);
    QString m_path,m_fundingPath,m_admissionPath,m_quotePath,m_rpcPath,m_program,m_digest,m_trade,m_deployment,m_request;
    QString m_state="unconfigured",m_operation;
    QByteArray m_wrapperBytes,m_fundingBytes,m_admissionBytes,m_quoteBytes,m_rpcBytes,m_input,m_output;
    QJsonObject m_policy,m_terms,m_config,m_result,m_lockReview,m_approvedLock,m_lockIntent,m_lockObservation;
    QJsonObject m_signingReview,m_approvedSigning,m_signingIntent,m_signedFunding;
    bool m_signingKnown=false;
    QString m_recoveryPath,m_candidatePath,m_reviewedCandidate,m_pendingCandidate,m_pendingTxid;
    QByteArray m_recoveryBytes;
    QJsonObject m_recoveryReview,m_approvedRecovery;
    QProcess m_process;
    QTimer m_deadline;
    qint64 m_errors=0,m_startedAt=0;
    bool m_busy=false,m_valid=false,m_lockKnown=false;
};
#endif
