#ifndef MONZERO_BCH_NATIVE_RELAY_JOB_H
#define MONZERO_BCH_NATIVE_RELAY_JOB_H
#include <QObject>
#include <QJsonObject>
#include <QMap>
#include <QProcess>
#include <QTimer>
#include <QVariantMap>
// Explicit broadcast of the retained native payment; uncertain attempts stay blocked.
class BchNativeRelayJob final : public QObject {
    Q_OBJECT
public:
    explicit BchNativeRelayJob(QObject *parent=nullptr);
    ~BchNativeRelayJob() override;
    bool configure(const QString &path);
    bool review();
    bool submit(const QString &commitment);
    bool refresh();
    QVariantMap status() const;
signals:
    void changed();
private:
    bool sourcesMatch() const;
    bool run(const QString &operation,const QString &commitment={});
    void drain();
    void finish(bool success);
    QString m_path,m_program,m_digest,m_trade,m_quote,m_peer,m_request,m_operation,m_state="unconfigured";
    QMap<QString,QByteArray> m_sources;
    QByteArray m_input,m_output;
    QJsonObject m_config,m_record,m_source,m_review,m_preparation,m_preparedReview,m_relay,m_approved;
    quint64 m_admitted=0,m_expires=0;
    QProcess m_process;
    QTimer m_deadline;
    qint64 m_errors=0;
    bool m_busy=false,m_submitAttempted=false,m_sourcesValid=false,m_checked=false;
};
#endif
