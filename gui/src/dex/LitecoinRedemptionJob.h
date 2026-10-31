#ifndef MONZERO_LITECOIN_REDEMPTION_JOB_H
#define MONZERO_LITECOIN_REDEMPTION_JOB_H
#include <QObject>
#include <QJsonObject>
#include <QMap>
#include <QProcess>
#include <QTimer>
#include <QVariantMap>

// Separate approval and point-in-time confirmation of a retained redemption.
class LitecoinRedemptionJob final : public QObject {
    Q_OBJECT
public:
    explicit LitecoinRedemptionJob(QObject *parent=nullptr);
    ~LitecoinRedemptionJob() override;
    bool configure(const QString &path);
    bool review();
    bool submit(const QString &commitment);
    bool check();
    QVariantMap status() const;
signals:
    void changed();
private:
    bool sourcesMatch() const;
    bool run(const QString &operation,const QString &commitment={});
    bool reviewValid(const QJsonObject &review,const QJsonObject &snapshot) const;
    void drain();
    void finish(bool success);
    QString m_path,m_program,m_digest,m_trade,m_genesis,m_request,m_operation,m_state="unconfigured",m_checkState="not-checked";
    QMap<QString,QByteArray> m_sources;
    QVariantMap m_review,m_pending,m_submission,m_observation;
    quint64 m_minimum=0;
    QProcess m_process;
    QTimer m_deadline;
    QByteArray m_output,m_input;
    qint64 m_errors=0;
    bool m_busy=false;
};
#endif
