#ifndef MONZERO_BCH_PUBLICATION_REPAIR_JOB_H
#define MONZERO_BCH_PUBLICATION_REPAIR_JOB_H
#include <QObject>
#include <QJsonObject>
#include <QMap>
#include <QProcess>
#include <QTimer>
#include <QVariantMap>
class BchPublicationRepairJob final:public QObject {
    Q_OBJECT
public:
    explicit BchPublicationRepairJob(QObject *parent=nullptr);
    ~BchPublicationRepairJob() override;
    bool configure(const QString &path);
    bool review(const QString &stage);
    bool submit(const QString &approved);
    void invalidate();
    QVariantMap status() const;
signals:
    void changed();
private:
    bool sourcesMatch() const;
    bool start(bool submit);
    void drain();
    void finish(bool success);
    QString m_path,m_program,m_digest,m_request,m_stage,m_approved,m_state="unconfigured";
    QMap<QString,QByteArray> m_sources;
    QJsonObject m_config,m_record,m_source,m_review;
    QByteArray m_input,m_output;
    QProcess m_process;
    QTimer m_deadline;
    qint64 m_errors=0;
    bool m_busy=false,m_submit=false,m_sourcesValid=false,m_attempted=false;
};
#endif
