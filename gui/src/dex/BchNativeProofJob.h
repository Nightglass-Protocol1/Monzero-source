#ifndef MONZERO_BCH_NATIVE_PROOF_JOB_H
#define MONZERO_BCH_NATIVE_PROOF_JOB_H
#include <QObject>
#include <QJsonObject>
#include <QMap>
#include <QProcess>
#include <QTimer>
#include <QVariantMap>
// Private proof receipt only. The caller supplies independently restored public
// funding context, or the pinned helper restores it before status. This worker
// never opens the private proof message file.
class BchNativeProofJob final : public QObject {
    Q_OBJECT
public:
    explicit BchNativeProofJob(QObject *parent=nullptr);
    ~BchNativeProofJob() override;
    bool configure(const QString &path,const QJsonObject &fundingContext={});
    bool review();
    bool submit(const QString &commitment);
    bool refresh();
    QVariantMap status() const;
    QJsonObject retainedFundingContext() const;
signals:
    void changed();
private:
    bool sourcesMatch() const;
    bool run(const QString &operation,const QString &commitment={});
    void drain();
    void finish(bool success,const QString &reason={});
    QString m_path,m_program,m_digest,m_trade,m_quote,m_peer,m_request,m_operation,m_state="unconfigured",m_failureReason;
    QMap<QString,QByteArray> m_sources;
    QByteArray m_input,m_output;
    QJsonObject m_config,m_record,m_context,m_review,m_receipt,m_approved;
    QProcess m_process;
    QTimer m_deadline;
    qint64 m_errors=0;
    bool m_busy=false,m_submitAttempted=false,m_sourcesValid=false,m_checked=false;
};
#endif
