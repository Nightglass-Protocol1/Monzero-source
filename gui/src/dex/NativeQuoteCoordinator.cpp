#include "NativeQuoteCoordinator.h"
#include "NativeDestinationJob.h"
#include "NativeQuoteReplay.h"
#include <QDateTime>

NativeQuoteCoordinator::NativeQuoteCoordinator(QString helper, QString digest, QString directory,
    NativeTradeQuote::DeploymentPolicy policy, QByteArray btcPeer, QByteArray xmzPeer, QObject *parent)
    : QObject(parent), m_helper(std::move(helper)), m_digest(std::move(digest)),
      m_directory(std::move(directory)), m_policy(std::move(policy)),
      m_btcPeer(std::move(btcPeer)), m_xmzPeer(std::move(xmzPeer)) {
    m_expiry.setSingleShot(true);
    connect(&m_expiry, &QTimer::timeout, this, [this] {
        if (m_state == Ready) { m_state = Failed; emit changed(); }
    });
}
bool NativeQuoteCoordinator::verify(QJsonObject &terms) const {
    return NativeTradeQuote::verifyForDeployment(m_payload, QDateTime::currentSecsSinceEpoch(),
        m_btcSignature, m_xmzSignature, m_btcPeer, m_xmzPeer, m_policy, terms);
}
NativeQuoteCoordinator::State NativeQuoteCoordinator::state() const {
    QJsonObject verified;
    if (m_state == Ready && !verify(verified)) return Failed;
    return m_state;
}
QVariantMap NativeQuoteCoordinator::review() const {
    if (state() != Ready) return {};
    return {{"terms", m_terms.toVariantMap()}, {"commitment", QString::fromLatin1(m_commitment.toHex())},
        {"amounts", NativeTradeQuote::displayAmounts(m_terms, QDateTime::currentSecsSinceEpoch())},
        {"executable", false}, {"stage", "commercial-terms-only"}};
}
QVariantMap NativeQuoteCoordinator::status() const {
    QString stage;
    switch (state()) {
    case Idle: stage = "idle"; break;
    case Checking: stage = "checking"; break;
    case Ready: stage = "review"; break;
    case Reserved: stage = "reserved"; break;
    case Failed: stage = "failed"; break;
    case Cancelled: stage = "cancelled"; break;
    }
    return {{"state", stage}, {"executable", false}};
}
bool NativeQuoteCoordinator::begin(QByteArray payload, QByteArray btcSignature, QByteArray xmzSignature) {
    if (m_state != Idle) return false;
    m_payload = std::move(payload); m_btcSignature = std::move(btcSignature); m_xmzSignature = std::move(xmzSignature);
    if (!verify(m_terms)) { m_state = Failed; emit changed(); return false; }
    const QMap<QString,QString> bitcoin{{"btc-mainnet","mainnet"},{"btc-testnet","testnet"},{"btc-regtest","regtest"}};
    const QMap<QString,QString> xmz{{"xmz-mainnet","mainnet"},{"xmz-fakechain","mainnet"},
        {"xmz-testnet","testnet"},{"xmz-stagenet","stagenet"}};
    if (!bitcoin.contains(m_policy.btcNetwork) || !xmz.contains(m_policy.xmzNetwork)) {
        m_state = Failed; emit changed(); return false;
    }
    m_commitment = NativeTradeQuote::commitment(m_terms, QDateTime::currentSecsSinceEpoch());
    m_job = new NativeDestinationJob(this);
    connect(m_job, &NativeDestinationJob::completed, this, [this](bool valid) {
        if (m_state != Checking) return;
        QJsonObject verified;
        if (!valid || !verify(verified) || verified != m_terms) m_state = Failed;
        else {
            m_state = Ready;
            const auto remaining = m_terms.value("expiresAt").toString().toLongLong() - QDateTime::currentSecsSinceEpoch();
            if (remaining <= 0 || remaining > 3600) m_state = Failed;
            else m_expiry.start(static_cast<int>(remaining * 1000));
        }
        emit changed();
    });
    m_state = Checking;
    if (!m_job->start(m_helper, m_digest, m_terms, QDateTime::currentSecsSinceEpoch(),
        bitcoin.value(m_policy.btcNetwork), xmz.value(m_policy.xmzNetwork))) {
        m_state = Failed; emit changed(); return false;
    }
    emit changed();
    return true;
}
bool NativeQuoteCoordinator::approve(const QByteArray &reviewedCommitment) {
    QJsonObject verified;
    if (state() != Ready || reviewedCommitment != m_commitment || m_commitment.isEmpty()
        || !verify(verified) || verified != m_terms) return false;
    m_expiry.stop();
    const bool saved = NativeQuoteReplay::reserve(m_directory, m_payload, QDateTime::currentSecsSinceEpoch(),
        m_btcSignature, m_xmzSignature, m_btcPeer, m_xmzPeer);
    m_state = saved ? Reserved : Failed;
    emit changed();
    return saved;
}
void NativeQuoteCoordinator::cancel() {
    if (m_state == Reserved || m_state == Failed || m_state == Cancelled) return;
    m_state = Cancelled; m_expiry.stop();
    if (m_job) m_job->cancel();
    emit changed();
}
