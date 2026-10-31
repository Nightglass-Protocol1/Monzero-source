#ifndef MONZERO_BCH_SUCCESS_RELEASE_REVIEW_JOB_H
#define MONZERO_BCH_SUCCESS_RELEASE_REVIEW_JOB_H
#include <QObject>
#include "BchSuccessReleaseProtocol.h"
#include <QJsonObject>
#include <QMap>
#include <QProcess>
#include <QTimer>
#include <QVariantMap>
// Explicit public review only. No adaptor construction, signing or release action.
class BchSuccessReleaseReviewJob final : public QObject {
    Q_OBJECT
public:
    explicit BchSuccessReleaseReviewJob(QObject *parent=nullptr);
    ~BchSuccessReleaseReviewJob() override;
    bool configure(const QString &path,BchSuccessReleaseProtocol::Action action=BchSuccessReleaseProtocol::Action::Prepare);
    bool review(const QJsonObject &proofReview,const QJsonObject &fundingContext);
    QVariantMap status() const;
    void invalidate();
signals:
    void changed();
private:
    bool sourcesMatch() const;
    void drain();
    void finish(bool success);
    QString m_path,m_program,m_digest,m_trade,m_request,m_state="unconfigured",m_operation="release";
    BchSuccessReleaseProtocol::Action m_action=BchSuccessReleaseProtocol::Action::Prepare;
    QMap<QString,QByteArray> m_sources;
    QByteArray m_input,m_output;
    QJsonObject m_config,m_record,m_context,m_review,m_proofReview,m_source;
    QProcess m_process;
    QTimer m_deadline;
    qint64 m_errors=0;
    bool m_busy=false,m_sourcesValid=false;
};
#endif
