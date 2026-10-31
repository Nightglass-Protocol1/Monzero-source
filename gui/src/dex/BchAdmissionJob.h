#ifndef MONZERO_BCH_ADMISSION_JOB_H
#define MONZERO_BCH_ADMISSION_JOB_H
#include <QObject>
#include <QJsonObject>
#include <QProcess>
#include <QTimer>
#include <QVariantMap>

// Admission of a retained negotiation only; no wallet or transaction commands.
class BchAdmissionJob final : public QObject {
    Q_OBJECT
public:
    explicit BchAdmissionJob(QObject *parent=nullptr);
    ~BchAdmissionJob() override;
    bool configure(const QString &path);
    bool review();
    bool submit(const QString &commitment);
    bool refresh();
    QVariantMap status() const;
    // Validated status only; no wallet selection, signing or relay authority.
    QJsonObject lightFundingContext() const;
signals:
    void changed();
private:
    bool sourcesMatch() const;
    bool run(const QString &operation,const QString &commitment={});
    void drain();
    void finish(bool success);
    QString m_path,m_quotePath,m_program,m_digest,m_trade,m_request,m_operation;
    QString m_state="unconfigured";
    QByteArray m_configBytes,m_quoteBytes,m_input,m_output;
    QJsonObject m_expectedReview,m_review,m_admission;
    QProcess m_process;
    QTimer m_deadline;
    qint64 m_errors=0;
    bool m_busy=false,m_submitAttempted=false,m_sourcesValid=false;
};
#endif
