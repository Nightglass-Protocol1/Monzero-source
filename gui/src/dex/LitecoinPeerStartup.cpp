#include "DexDeployment.h"
#include "LitecoinPeerStartup.h"
#include "DexEngine.h"
#include "LitecoinJournalProtocol.h"
#include "PrivateQuoteServer.h"
#include <QJsonDocument>
#include <QSslKey>
#include <sodium.h>

namespace {
constexpr int firstRetryMs=2000,lastRetryMs=60000;
}
LitecoinPeerStartup::LitecoinPeerStartup(DexEngine &engine,QObject *parent)
    :QObject(parent),m_engine(&engine) {
    m_retry.setSingleShot(true);
    connect(&m_retry,&QTimer::timeout,this,&LitecoinPeerStartup::reconnect);
}
LitecoinPeerStartup::~LitecoinPeerStartup() {
    if (m_server) m_server->stop();
    if (m_client) m_client->close();
}
bool LitecoinPeerStartup::configure(const QString &path,const QString &journalPath,const QString &appPath) {
    if (m_configured || !m_engine || !m_sources.isEmpty()) return false;
    const auto read=[this](const QString &file) {
        const auto bytes=LitecoinJournalProtocol::privateConfig(file);
        if (!bytes.isEmpty()) m_sources.insert(file,QCryptographicHash::hash(bytes,QCryptographicHash::Sha256));
        return bytes;
    };
    if (!appPath.isEmpty() && read(appPath).isEmpty()) return false;
    const auto bytes=read(path);const auto doc=QJsonDocument::fromJson(bytes);const auto c=doc.object();
    const QStringList strings{"schema","mode","address","peerName","peerCertificateSha256",
        "certificateFile","privateKeyFile","caFile","identityFile","passphraseFile"};
    const bool settlement=c.value("schema")=="monzero-private-ltc-peer-v2";
    if (!doc.isObject() || doc.toJson(QJsonDocument::Compact)!=bytes || c.size()!=(settlement?12:11)
        || (!settlement && c.value("schema")!="monzero-private-ltc-peer-v1")
        || (settlement && (!c.value("settlementConfigFile").isString() || c.value("settlementConfigFile").toString().isEmpty()))) return false;
    for (const auto &key:strings) if (!c.value(key).isString() || c.value(key).toString().isEmpty()) return false;
    m_mode=c.value("mode").toString();m_address=QHostAddress(c.value("address").toString());
    m_peerName=c.value("peerName").toString();const auto port=c.value("port").toDouble();
    const auto pin=c.value("peerCertificateSha256").toString();
    static const QRegularExpression pinPattern("\\A[0-9a-f]{64}\\z"),namePattern("\\A[A-Za-z0-9.-]{1,253}\\z");
    if (!QStringList{"listen","connect"}.contains(m_mode) || !addressAllowed(m_mode,m_address)
        || !c.value("port").isDouble() || port<1 || port>65535 || port!=static_cast<quint16>(port)
        || !pinPattern.match(pin).hasMatch() || !namePattern.match(m_peerName).hasMatch()) return false;
    m_peerPin=QByteArray::fromHex(pin.toLatin1());m_port=static_cast<quint16>(port);
    if (m_peerPin==QByteArray(32,'\0') || !QSslSocket::supportsSsl()) return false;
    const auto journal=QJsonDocument::fromJson(read(journalPath)).object();
    if (journal.value("schema")!="monzero-private-ltc-journal-v5"
        || read(journal.value("startQuoteFile").toString()).isEmpty()) return false;
    const auto certificates=QSslCertificate::fromData(read(c.value("certificateFile").toString()));
    const auto authorities=QSslCertificate::fromData(read(c.value("caFile").toString()));
    auto keyBytes=read(c.value("privateKeyFile").toString());
    QSslKey key(keyBytes,QSsl::Rsa);if (key.isNull()) key=QSslKey(keyBytes,QSsl::Ec);
    sodium_memzero(keyBytes.data(),keyBytes.size());keyBytes.clear();
    if (certificates.size()!=1 || authorities.isEmpty() || key.isNull()
        || read(c.value("identityFile").toString()).isEmpty()) return false;
    m_journalPath=journalPath;m_identityFile=c.value("identityFile").toString();
    m_passphraseFile=c.value("passphraseFile").toString();
    if (read(m_passphraseFile).isEmpty()) return false;
    if (settlement) {
        m_settlementFile=c.value("settlementConfigFile").toString();
        if (read(m_settlementFile).isEmpty()) return false;
    }
    if (!buildSession()) return false;
    if (settlement && journal.value("role")=="native-funder"
        && !m_engine->configureLitecoinRedemption(c.value("settlementConfigFile").toString())) return false;
    m_tls=QSslConfiguration::defaultConfiguration();m_tls.setCaCertificates(authorities);
    m_tls.setLocalCertificate(certificates.first());m_tls.setPrivateKey(key);
    if (!sourcesMatch() || !m_engine->attachLitecoinNegotiation(m_session.get())) return false;
    m_configured=true;
    connect(m_engine,&DexEngine::litecoinJournalChanged,this,&LitecoinPeerStartup::startWhenSaved);
    QTimer::singleShot(0,this,&LitecoinPeerStartup::startWhenSaved);
    return true;
}
bool LitecoinPeerStartup::addressAllowed(const QString &mode,const QHostAddress &address) {
    if (address.isNull() || address.isMulticast() || address.isBroadcast()) return false;
    if (address.isLoopback()) return true;
    // Another machine only on testnet, where both peers are our own machines;
    // the pinned TLS certificate, not the address, authenticates the peer.
    if (!DexDeployment::publicTestStage()) return false;
    const bool any=address==QHostAddress(QHostAddress::AnyIPv4) || address==QHostAddress(QHostAddress::AnyIPv6);
    return mode=="listen" || !any;
}
bool LitecoinPeerStartup::sourcesMatch() const {
    if (m_sources.isEmpty()) return false;
    for (auto it=m_sources.cbegin();it!=m_sources.cend();++it) {
        auto bytes=LitecoinJournalProtocol::privateConfig(it.key());
        const bool match=!bytes.isEmpty() && QCryptographicHash::hash(bytes,QCryptographicHash::Sha256)==it.value();
        sodium_memzero(bytes.data(),bytes.size());
        if (!match) return false;
    }
    return true;
}
bool LitecoinPeerStartup::buildSession() {
    auto session=std::make_unique<LitecoinNegotiationSession>(this);
    auto passphrase=LitecoinJournalProtocol::privateConfig(m_passphraseFile);
    const bool configured=!passphrase.isEmpty() && passphrase.size()<=1024
        && session->configure(m_journalPath,m_identityFile,passphrase);
    sodium_memzero(passphrase.data(),passphrase.size());passphrase.clear();
    if (!configured || (!m_settlementFile.isEmpty() && !session->configureSettlement(m_settlementFile))) return false;
    connect(session.get(),&LitecoinNegotiationSession::changed,this,&LitecoinPeerStartup::sessionChanged,Qt::QueuedConnection);
    m_session=std::move(session);return true;
}
void LitecoinPeerStartup::sessionChanged() {
    if (!m_session) return;
    const auto state=m_session->status();
    if (state.value("transportAuthenticated").toBool()) m_delay=0;
    // After a rebuild, re-send the already signed recovery agreement (no new
    // reservation or broadcast) so settlement can continue as before the drop.
    if (m_resumeRecovery && state.value("complete").toBool() && state.value("transportAuthenticated").toBool()
        && !state.value("busy").toBool() && !state.value("recoveryStarted").toBool()) {
        m_resumeRecovery=false;m_session->resumeRecovery();
    }
    // Then repeat settlement steps that were already under way before the drop:
    // the funding proof, and a release the user approved for this exact funding.
    // Both re-send the same saved data; no new approval is created.
    const bool ready=state.value("recoveryComplete").toBool() && state.value("transportAuthenticated").toBool()
        && !state.value("busy").toBool() && !state.value("settlementComplete").toBool();
    if (ready && !m_settledFunding.isEmpty() && !state.value("settlementStarted").toBool()) {
        const auto funding=m_settledFunding,redemption=m_settledRedemption;
        m_settledFunding.clear();m_settledRedemption.clear();
        m_session->restoreCompletedSettlement(funding,redemption);
    } else if (ready && m_resumeProof && !state.value("settlementStarted").toBool()) {
        m_resumeProof=false;m_session->startRedemption({});
    } else if (ready && !m_resumeRelease.isEmpty() && state.value("canReleaseRedemption").toBool()) {
        const auto txid=m_resumeRelease;m_resumeRelease.clear();
        if (state.value("nativeFundingTxid")==txid) m_session->startRedemption(txid);
    }
    if (state.value("transportLost").toBool() && !m_retry.isActive()) {
        m_delay=m_delay?qMin(2*m_delay,lastRetryMs):firstRetryMs;
        m_retry.start(m_delay);
    }
}
void LitecoinPeerStartup::reconnect() {
    if (!m_configured || !m_engine || !m_session || !sourcesMatch()) return;
    const auto previous=m_session->status();
    if (!previous.value("transportLost").toBool()) return;
    m_resumeRecovery=m_resumeRecovery || (previous.value("recoveryAgreed").toBool() && previous.value("role")=="litecoin-funder");
    const bool settling=!previous.value("settlementComplete").toBool();
    if (!settling) {
        m_settledFunding=previous.value("nativeFundingTxid").toString();
        m_settledRedemption=previous.value("redemptionTxid").toString();
    }
    m_resumeProof=m_resumeProof || (settling && previous.value("proofSent").toBool() && previous.value("role")=="native-funder");
    if (settling && previous.value("releaseWasApproved").toBool() && previous.value("role")=="litecoin-funder")
        m_resumeRelease=previous.value("nativeFundingTxid").toString();
    // Runs from the retry timer, never inside a session or channel signal, so the
    // old objects can go now; the engine must see the old session gone to attach the new one.
    m_session->abort();
    if (m_accepted) m_accepted->close();
    m_session.reset();m_client.reset();
    if (!buildSession() || !m_engine->attachLitecoinNegotiation(m_session.get())) {m_session.reset();return;}
    ++m_reconnects;m_attempted=false;m_connected=false;
    startWhenSaved();
}
#ifdef MONZERO_DEX_TESTING
bool LitecoinPeerStartup::dropTransportForTesting() {
    PrivateQuoteChannel *channel=m_client?m_client.get():m_accepted.data();
    if (!channel || !channel->ready()) return false;
    channel->close();return true;
}
#endif
void LitecoinPeerStartup::startWhenSaved() {
    if (!m_configured || m_attempted || !m_engine) return;
    const auto journal=m_engine->litecoinJournalStatus();const auto snapshot=journal.value("snapshot").toMap();
    if (!m_session) return;
    const auto expected=m_session->status();
    if (!journal.value("valid").toBool() || journal.value("busy").toBool()
        || !journal.value("startConfigured").toBool() || journal.value("startState")!="saved") return;
    m_attempted=true;
    if (snapshot.value("tradeId")!=expected.value("tradeId") || snapshot.value("role")!=expected.value("role")
        || snapshot.value("quoteCommitment")!=expected.value("quoteCommitment")
        || !DexDeployment::litecoinNetworkAllowed(snapshot.value("ltcNetwork").toString()) || !sourcesMatch()) {m_session->abort();return;}
    if (m_mode=="listen") {
        if (!m_server) {
            // One listener for the app's lifetime; each rebuilt session takes the next connection.
            m_server.reset(new PrivateQuoteServer(m_tls,m_peerPin,this,PrivateQuoteChannel::Profile::LitecoinNegotiation));
            connect(m_server.get(),&PrivateQuoteServer::authenticated,this,[this](PrivateQuoteChannel *channel) {
                if (m_connected || !m_session) {channel->close();return;}
                if (!sourcesMatch()) {channel->close();m_session->abort();m_server->stop();return;}
                m_connected=true;m_accepted=channel;
                if (!m_session->attach(*channel) || !m_session->start()) {channel->close();m_session->abort();}
            });
            if (!m_session->waitForConnection() || !m_server->start(m_address,m_port)) m_session->abort();
        } else if (!m_session->waitForConnection()) m_session->abort();
    } else {
        m_client.reset(new PrivateQuoteChannel(m_tls,m_peerPin,this,PrivateQuoteChannel::Profile::LitecoinNegotiation));
        if (!m_session->attach(*m_client) || !m_session->start()
            || !m_client->connectToPeer(m_address.toString(),m_port,m_peerName)) m_session->abort();
    }
}
