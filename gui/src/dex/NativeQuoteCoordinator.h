#ifndef MONZERO_NATIVE_QUOTE_COORDINATOR_H
#define MONZERO_NATIVE_QUOTE_COORDINATOR_H
#include "NativeTradeQuote.h"
#include <QObject>
#include <QVariantMap>
#include <QTimer>
class NativeDestinationJob;

// Native caller owns trusted configuration and peer pins. Commercial approval
// only: even Reserved does not permit engine funding or enable trading.
class NativeQuoteCoordinator : public QObject {
    Q_OBJECT
public:
    enum State { Idle, Checking, Ready, Reserved, Failed, Cancelled };
    NativeQuoteCoordinator(QString helper, QString digest, QString replayDirectory,
        NativeTradeQuote::DeploymentPolicy policy, QByteArray btcPeer, QByteArray xmzPeer,
        QObject *parent = nullptr);
    bool begin(QByteArray payload, QByteArray btcSignature, QByteArray xmzSignature);
    bool approve(const QByteArray &reviewedCommitment);
    void cancel();
    State state() const;
    QVariantMap review() const;
    QVariantMap status() const;
signals:
    void changed();
private:
    bool verify(QJsonObject &terms) const;
    QString m_helper, m_digest, m_directory;
    NativeTradeQuote::DeploymentPolicy m_policy;
    QByteArray m_btcPeer, m_xmzPeer, m_payload, m_btcSignature, m_xmzSignature, m_commitment;
    QJsonObject m_terms;
    NativeDestinationJob *m_job = nullptr;
    QTimer m_expiry;
    State m_state = Idle;
};
#endif
