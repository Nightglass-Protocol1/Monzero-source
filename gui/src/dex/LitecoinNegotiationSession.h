#ifndef MONZERO_LITECOIN_NEGOTIATION_SESSION_H
#define MONZERO_LITECOIN_NEGOTIATION_SESSION_H
#include <QObject>
#include <QPointer>
#include <QJsonObject>
#include <QMap>
#include <QProcess>
#include <QTimer>
#include <QVariantMap>
#include <memory>
class OfferIdentityStore;
class PrivateQuoteChannel;

// Native-only coordinator for an already approved, retained isolated trade.
// Provision the TLS peer pin and quote participant together out of band. Never
// connect its private frames to the public offer relay or expose them to QML.
class LitecoinNegotiationSession final : public QObject {
    Q_OBJECT
public:
    explicit LitecoinNegotiationSession(QObject *parent=nullptr);
    ~LitecoinNegotiationSession() override;
    bool configure(const QString &journalConfig,const QString &identityFile,const QByteArray &passphrase);
    bool configureSettlement(const QString &path);
    bool startRedemption(const QString &approvedFundingTxid);
    // After a lost connection: the previous session in this app already finished
    // settlement for these transactions, so the rebuilt one reports it as done.
    bool restoreCompletedSettlement(const QString &fundingTxid,const QString &redemptionTxid);
    bool attach(PrivateQuoteChannel &channel);
    bool start();
    // Native reservation driver supplies the exact reserved PSBT; never a QML payload.
    bool startRecovery(const QByteArray &reservedPsbt);
    bool resumeRecovery();
    bool waitForConnection();
    void abort();
    QVariantMap status() const;
    QJsonObject lightFundingContext() const;
signals:
    void changed();
    void completed();
    void recoveryCompleted();
private:
    bool sourcesMatch() const;
    bool settlementSourcesMatch() const;
    bool settlementOperation() const;
    void receiveSettlement(const QJsonObject &message);
    void pumpSettlement();
    void finishSettlement(const QJsonObject &response);
    void checkSettlementComplete();
    bool redeemFrameMatches(const QByteArray &frame) const;
    bool boundSnapshot(const QJsonObject &snapshot) const;
    bool frameMatches(const QByteArray &bytes,bool local) const;
    void run(const QString &operation,const QJsonObject &fields={});
    void drain();
    void finish(bool success);
    void receive(const QByteArray &message);
    void pump();
    void fail();
    void checkComplete();
    void pumpRecovery();
    void checkRecoveryComplete();
    bool recoveryFrameMatches(const QByteArray &frame,bool local) const;
    void receiveRecovery(const QJsonObject &message);
    QByteArray wire(const QString &kind,const QJsonObject &fields) const;
    QString m_path,m_program,m_digest,m_quotePath,m_trade,m_role,m_quoteCommitment,m_request,m_operation;
    QString m_state="unconfigured",m_next;
    QByteArray m_config,m_quote,m_input,m_output,m_localFrame,m_localSignature,m_localDigest;
    QJsonObject m_terms,m_snapshot,m_incoming,m_pending;
    std::unique_ptr<OfferIdentityStore> m_identity;
    QPointer<PrivateQuoteChannel> m_channel;
    QProcess m_process;
    QTimer m_processDeadline,m_ackDeadline,m_recoveryDeadline,m_settlementDeadline;
    // Keeps the pinned peer connection (and any NAT mapping) open while a trade
    // waits for confirmations; the listening side closes channels idle for 5 min.
    QTimer m_keepalive;
    QString m_settlementPath,m_settlementProgram,m_settlementDigest,m_settlementState="not-started",m_fundingTxid,m_redemptionTxid;
    QMap<QString,QByteArray> m_settlementSources;
    QJsonObject m_settlementPending,m_settlementIncoming;
    QByteArray m_proofDigest,m_redeemDigest;
    bool m_settlementStarted=false,m_settlementComplete=false,m_proofSent=false,m_proofReceived=false,m_proofAcknowledged=false;
    bool m_redeemSent=false,m_redeemReceived=false,m_redeemAcknowledged=false,m_releaseApproved=false;
    QByteArray m_recoveryPsbt,m_recoveryDigest;
    QJsonObject m_recoveryPending,m_recoveryIncoming,m_recoveryPeer;
    QString m_recoveryState="not-started";
    bool m_recoveryStarted=false,m_recoverySent=false,m_recoveryAcknowledged=false,m_recoveryReceived=false,m_recoveryComplete=false;
    qint64 m_errors=0;
    bool m_transportLost=false;
    // Kept through fail(), which clears the live flags: what a rebuilt session
    // may repeat after a lost connection (the agreement, an approved release).
    bool m_recoveryAgreed=false,m_releaseWasApproved=false;
    QString m_failedDuring;
    bool m_started=false,m_busy=false,m_failed=false,m_sent=false,m_acknowledged=false,m_complete=false,m_retained=false;
};
#endif
