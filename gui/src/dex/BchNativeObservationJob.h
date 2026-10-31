#ifndef MONZERO_BCH_NATIVE_OBSERVATION_JOB_H
#define MONZERO_BCH_NATIVE_OBSERVATION_JOB_H
#include <QObject>
#include <QJsonObject>
#include <QMap>
#include <QProcess>
#include <QTimer>
#include <QVariantMap>
// Explicit fresh observation only; no retained confirmation or spending action.
class BchNativeObservationJob final : public QObject {
    Q_OBJECT
public:
    explicit BchNativeObservationJob(QObject *parent=nullptr);
    ~BchNativeObservationJob() override;
    bool configure(const QString &path);
    bool check(const QJsonObject &fundingContext);
    QVariantMap status() const;
    void invalidate();
signals:
    void changed();
private:
    bool sourcesMatch() const;
    void drain();
    void finish(bool success);
    QString m_path,m_program,m_digest,m_trade,m_request,m_state="unconfigured";
    QMap<QString,QByteArray> m_sources;
    QByteArray m_input,m_output;
    QJsonObject m_config,m_record,m_context,m_review,m_observation;
    QProcess m_process;
    QTimer m_deadline;
    quint64 m_started=0;
    qint64 m_errors=0;
    bool m_busy=false,m_sourcesValid=false;
};
#endif
