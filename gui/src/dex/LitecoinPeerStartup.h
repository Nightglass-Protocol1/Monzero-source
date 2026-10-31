#ifndef MONZERO_LITECOIN_PEER_STARTUP_H
#define MONZERO_LITECOIN_PEER_STARTUP_H
#include "LitecoinNegotiationSession.h"
#include <QHostAddress>
#include <QMap>
#include <QSslConfiguration>
#include <QTimer>
#include <memory>
class DexEngine;
class PrivateQuoteServer;

// Owns native wallet startup provisioning. No endpoint, credential or helper
// path comes from QML. Network activity waits for the matching saved trade.
// Peers are local unless the testnet stage is selected (see addressAllowed).
// A lost connection rebuilds the session from the saved trade, exactly as an
// app restart would, and retries with growing delays; protocol failures do not.
class LitecoinPeerStartup final : public QObject {
    Q_OBJECT
public:
    explicit LitecoinPeerStartup(DexEngine &engine,QObject *parent=nullptr);
    ~LitecoinPeerStartup() override;
    bool configure(const QString &peerConfig,const QString &journalConfig,const QString &appPath={});
    int reconnects() const {return m_reconnects;}
    // Loopback always; another machine's address only on the testnet stage
    // (a listener may bind every interface, a connecting peer must name one).
    static bool addressAllowed(const QString &mode,const QHostAddress &address);
#ifdef MONZERO_DEX_TESTING
    // Tests only: drop the live connection as a network failure would.
    bool dropTransportForTesting();
#endif
private:
    bool sourcesMatch() const;
    bool buildSession();
    void sessionChanged();
    void reconnect();
    void startWhenSaved();
    QPointer<DexEngine> m_engine;
    std::unique_ptr<LitecoinNegotiationSession> m_session;
    std::unique_ptr<PrivateQuoteChannel> m_client;
    QPointer<PrivateQuoteChannel> m_accepted;
    QTimer m_retry;
    QString m_journalPath,m_identityFile,m_passphraseFile,m_settlementFile;
    std::unique_ptr<PrivateQuoteServer> m_server;
    QMap<QString,QByteArray> m_sources;
    QSslConfiguration m_tls;
    QByteArray m_peerPin;
    QHostAddress m_address;
    QString m_mode,m_peerName;
    quint16 m_port=0;
    int m_delay=0,m_reconnects=0;
    QString m_resumeRelease,m_settledFunding,m_settledRedemption;
    bool m_configured=false,m_attempted=false,m_connected=false,m_resumeRecovery=false,m_resumeProof=false;
};
#endif
