#ifndef MONZERO_BCH_REFUND_WALLET_JOB_H
#define MONZERO_BCH_REFUND_WALLET_JOB_H
#include <QObject>
#include <QJsonObject>
#include <QProcess>
#include <QTimer>
#include <QVariantMap>
#include <QPointer>
#include <QTemporaryFile>
#include <memory>
#include "SwapWalletRpcProcess.h"
// Saved status, fresh reviews and separately approved owned-service operations. No payout route.
class BchRefundWalletJob final:public QObject {
    Q_OBJECT
public:
    explicit BchRefundWalletJob(QObject *parent=nullptr);
    ~BchRefundWalletJob() override;
    bool configure(const QString &path);
    bool refresh();
    bool configureReview(const QString &path);
    bool provisionOwnedReview(SwapWalletRpcProcess *owner,const QString &bchRpcFile,const QString &observerRpcFile);
    void discardProvisionedReview();
    bool requestReview(const QString &action);
    // Consumes/clears the supplied buffer on every path. Callers must avoid
    // retaining shared aliases; Qt's process transport has its own internal copy.
    bool submit(SwapWalletRpcProcess *owner,const QString &commitment,QByteArray &&password);
    bool ownsConfiguredService(SwapWalletRpcProcess *owner) const;
    bool canSubmit(SwapWalletRpcProcess *owner) const;
    QVariantMap status() const;
signals:
    void changed();
private:
    bool sourcesMatch() const;
    bool start(const QString &action);
    void drain();
    void finish(bool success);
    void notifyChanged();
    QString m_path,m_admissionPath,m_quotePath,m_program,m_digest,m_trade,m_deployment,m_refund,m_request;
    QString m_state="unconfigured";
    QByteArray m_wrapperBytes,m_admissionBytes,m_quoteBytes,m_input,m_output;
    QJsonObject m_wallet,m_savedWallet,m_result,m_review,m_backend;
    std::unique_ptr<QTemporaryFile> m_provisionedReview;
    QString m_reviewPath,m_bchPath,m_nativePath,m_observerPath,m_action;
    QByteArray m_reviewBytes,m_bchBytes,m_nativeBytes,m_observerBytes;
    quint64 m_startedAt=0;
    QTimer m_reviewExpiry;
    QProcess m_process;
    QTimer m_deadline;
    qint64 m_errors=0;
    bool m_busy=false,m_sourcesValid=false,m_submitting=false,m_submissionUncertain=false;
    QPointer<SwapWalletRpcProcess> m_submissionOwner;
    QString m_operationToken;
    QJsonObject m_approved;
    QMetaObject::Connection m_ownerStopped,m_ownerDestroyed;
};
#endif
