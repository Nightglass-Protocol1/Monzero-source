#ifndef MONZERO_BCH_FUNDING_RECEIPT_JOB_H
#define MONZERO_BCH_FUNDING_RECEIPT_JOB_H
#include <QObject>
#include <QJsonObject>
#include <QMap>
#include <QProcess>
#include <QTimer>
#include <QVariantMap>
// Accepts authenticated peer funding receipt only; no funding or transaction RPC.
class BchFundingReceiptJob final : public QObject {
    Q_OBJECT
public:
    explicit BchFundingReceiptJob(QObject *parent=nullptr);
    ~BchFundingReceiptJob() override;
    bool configure(const QString &path);
    bool review();
    bool submit(const QString &commitment);
    bool refresh();
    QVariantMap status() const;
signals:
    void changed();
private:
    bool sourcesMatch() const;
    bool readMessage();
    bool validReview(const QJsonObject &review) const;
    bool run(const QString &operation,const QString &commitment={});
    void drain();
    void finish(bool success);
    QString m_path,m_program,m_digest,m_trade,m_quote,m_peer,m_messageDigest,m_transaction,m_messagePath,m_request,m_operation,m_state="unconfigured";
    QMap<QString,QByteArray> m_sources;
    QByteArray m_message,m_input,m_output;
    QJsonObject m_config,m_record,m_review,m_receipt,m_approved;
    quint64 m_admitted=0,m_expires=0;
    QProcess m_process;
    QTimer m_deadline;
    qint64 m_errors=0;
    bool m_busy=false,m_submitAttempted=false,m_sourcesValid=false,m_checked=false;
};
#endif
