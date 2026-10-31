#ifndef MONZERO_BCH_SUCCESS_PREPARATION_JOB_H
#define MONZERO_BCH_SUCCESS_PREPARATION_JOB_H
#include <QObject>
#include <QJsonObject>
#include <QMap>
#include <QProcess>
#include <QTimer>
#include <QVariantMap>
// Explicitly approved preparation stays in the private helper journal.
// No adaptor bytes, signing or peer release pass through this worker.
class BchSuccessPreparationJob final : public QObject {
    Q_OBJECT
public:
    explicit BchSuccessPreparationJob(QObject *parent=nullptr);
    ~BchSuccessPreparationJob() override;
    bool configure(const QString &path);
    bool submit(const QJsonObject &releaseReview,const QJsonObject &proofReview,
        const QJsonObject &fundingContext,const QString &approved);
    bool refresh(const QJsonObject &proofReview,const QJsonObject &fundingContext);
    QVariantMap status() const;
    void invalidate();
signals:
    void changed();
private:
    bool sourcesMatch() const;
    bool rpcSourcesMatch() const;
    bool start(bool statusOnly);
    void drain();
    void finish(bool success);
    QString m_path,m_program,m_digest,m_trade,m_request,m_approved,m_state="unconfigured";
    QMap<QString,QByteArray> m_sources,m_rpcSources;
    QByteArray m_input,m_output;
    QJsonObject m_config,m_record,m_context,m_review,m_proofReview,m_source,m_preparation;
    QProcess m_process;
    QTimer m_deadline;
    qint64 m_errors=0;
    bool m_busy=false,m_sourcesValid=false,m_attempted=false,m_statusRequest=false,m_statusKnown=false,m_chainObserved=false;
};
#endif
