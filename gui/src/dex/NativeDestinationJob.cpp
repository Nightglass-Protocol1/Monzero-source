#include "NativeDestinationJob.h"
#include "NativeTradeQuote.h"
#include "BackendExecutable.h"
#include <QJsonDocument>
#include <QProcess>
#include <QProcessEnvironment>
#include <QPointer>
#include <QTimer>
#include <QUuid>

NativeDestinationJob::NativeDestinationJob(QObject *parent)
    : QObject(parent), m_process(new QProcess(this)), m_timer(new QTimer(this)) {
    m_timer->setSingleShot(true);
    m_timer->setTimerType(Qt::PreciseTimer);
    connect(m_timer, &QTimer::timeout, this, [this] { finish(false); });
    connect(m_process, &QProcess::started, this, [this] {
        if (m_done) { m_process->kill(); return; }
        if (m_process->write(m_input) != m_input.size()) { finish(false); return; }
        m_process->closeWriteChannel();
    });
    connect(m_process, &QProcess::readyReadStandardOutput, this, &NativeDestinationJob::drain);
    connect(m_process, &QProcess::readyReadStandardError, this, &NativeDestinationJob::drain);
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) { finish(false); });
    connect(m_process, qOverload<int,QProcess::ExitStatus>(&QProcess::finished), this,
        [this](int code, QProcess::ExitStatus status) {
            QPointer<NativeDestinationJob> guard(this);
            drain();
            if (guard) finish(code == 0 && status == QProcess::NormalExit);
        });
}
NativeDestinationJob::~NativeDestinationJob() {
    disconnect(m_process, nullptr, this, nullptr);
    if (m_process->state() != QProcess::NotRunning) {
        m_process->kill();
        m_process->waitForFinished(1000);
    }
}
bool NativeDestinationJob::start(const QString &helper, const QString &digest, const QJsonObject &terms,
    qint64 now, const QString &btc, const QString &xmz, int timeoutMs) {
    if (m_started || timeoutMs < 1 || timeoutMs > 10000
        || !QStringList{"mainnet", "testnet", "regtest"}.contains(btc)
        || !QStringList{"mainnet", "testnet", "stagenet"}.contains(xmz)) return false;
    const auto commitment = NativeTradeQuote::commitment(terms, now);
    if (commitment.isEmpty() || !BackendExecutable::matches(helper, digest)) return false;
    QJsonObject request{{"schema", "monzero-native-destinations-request-v1"},
        {"requestId", QUuid::createUuid().toString(QUuid::WithoutBraces)},
        {"quoteCommitment", QString::fromLatin1(commitment.toHex())}};
    for (const auto key : {"btcPayout", "btcRefund", "xmzPayout", "xmzRefund"}) request[key] = terms.value(key);
    m_input = QJsonDocument(request).toJson(QJsonDocument::Compact);
    if (m_input.size() > 2048) return false;
    request["schema"] = "monzero-native-destinations-v1";
    request["valid"] = true;
    request["executable"] = false;
    m_expected = QJsonDocument(request).toJson(QJsonDocument::Compact);
    m_started = true; m_done = false; m_timeout = timeoutMs;
    QProcessEnvironment environment;
    environment.insert("LANG", "C");
    m_process->setProcessEnvironment(environment);
    m_process->setProcessChannelMode(QProcess::SeparateChannels);
    m_elapsed.start(); m_timer->start(timeoutMs);
    m_process->start(helper, {"--quote-destinations-stdio-v1", "--bitcoin-network", btc, "--xmz-network", xmz});
    return true;
}
void NativeDestinationJob::drain() {
    if (m_done) return;
    m_process->setReadChannel(QProcess::StandardOutput);
    m_output += m_process->read(2049 - m_output.size());
    m_process->setReadChannel(QProcess::StandardError);
    m_errors += m_process->read(4097 - m_errors).size();
    if (m_output.size() > 2048 || m_errors > 4096) finish(false);
}
void NativeDestinationJob::finish(bool success) {
    if (m_done) return;
    m_done = true; m_timer->stop();
    if (m_process->state() != QProcess::NotRunning) m_process->kill();
    const bool valid = success && m_errors == 0 && m_elapsed.elapsed() < m_timeout && m_output == m_expected;
    if (valid) m_result = QJsonDocument::fromJson(m_output).object();
    emit completed(valid);
}
void NativeDestinationJob::cancel() { m_result = {}; finish(false); }
