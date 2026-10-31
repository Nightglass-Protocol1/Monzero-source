#ifndef MONZERO_DEX_RISK_CONTROL_H
#define MONZERO_DEX_RISK_CONTROL_H
#include <QByteArray>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QVariantMap>

class QNetworkAccessManager;
class QTimer;

// Operator controls for starting NEW trades: a signed kill switch plus
// per-trade and rolling 24 h caps. Never consulted for funding, redemption,
// refund or recovery of trades already started, so nothing is stranded.
//
// Control document (canonical JSON envelope, served over HTTPS or read from a
// private local file):
//   {"payload":"<canonical JSON>","signature":"<128 hex Ed25519 detached>"}
// signed over "monzero-dex-control-v1\0" + payload, with payload exactly:
//   schema, network, sequence, issuedAt, expiresAt, newTradesEnabled,
//   maxLtcAtomsPerTrade, maxXmzAtomsPerTrade, maxLtcAtomsPerDay
// Regtest needs no document (local testing). Any other network refuses new
// trades unless a valid, unexpired document for that network is held.
class DexRiskControl final:public QObject {
    Q_OBJECT
public:
    explicit DexRiskControl(QObject *parent=nullptr);
    // 64 hex chars; empty disables documents (then only regtest can start).
    bool setPublicKey(const QString &hex);
    // Private file the started-trade ledger lives in (created 0600).
    bool setLedgerFile(const QString &path);
    void setNetwork(const QString &network); // e.g. "ltc-testnet"
    bool loadControlFile(const QString &path);
    bool startPolling(const QString &httpsUrl,int intervalSeconds=600);
    bool acceptControlDocument(const QByteArray &bytes,qint64 now=0);
    // Checks kill switch and caps for a new trade; empty string means allowed.
    QString refusal(const QString &tradeId,quint64 ltcAtoms,quint64 xmzAtoms,qint64 now=0) const;
    // Records the start durably; call only after refusal() returned empty and
    // before the trade is submitted. Idempotent for the same trade.
    bool recordStart(const QString &tradeId,quint64 ltcAtoms,qint64 now=0);
    QVariantMap status() const;
signals:
    void changed();
private:
    void fetch();
    QJsonObject ledger() const;
    QString coinName() const {return m_network.startsWith("btc-")?QStringLiteral("Bitcoin"):QStringLiteral("Litecoin");}
    QString m_network="ltc-regtest",m_ledgerPath,m_url;
    QByteArray m_publicKey;
    QJsonObject m_control;
    quint64 m_sequence=0;
    QString m_lastError;
    QNetworkAccessManager *m_network_manager=nullptr;
    QTimer *m_poll=nullptr;
};
#endif
