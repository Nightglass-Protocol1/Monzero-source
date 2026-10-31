#include "NativeFeePreviewJob.h"
#include "SwapWalletRpcProcess.h"
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTimer>

NativeFeePreviewJob::NativeFeePreviewJob(NativeFeePreviewController *controller, QObject *parent)
    : QObject(parent), m_controller(controller), m_process(new QProcess(this)), m_timeout(new QTimer(this)) {
    m_timeout->setSingleShot(true);
    m_timeout->setTimerType(Qt::PreciseTimer);
    connect(m_timeout,&QTimer::timeout,this,[this] { finish(false); });
    connect(m_process,&QProcess::started,this,[this] {
        if (m_process->write(m_input) != m_input.size()) { finish(false); return; }
        m_process->closeWriteChannel();
    });
    connect(m_process,&QProcess::readyReadStandardOutput,this,&NativeFeePreviewJob::drain);
    connect(m_process,&QProcess::readyReadStandardError,this,&NativeFeePreviewJob::drain);
    connect(m_process,&QProcess::errorOccurred,this,[this](QProcess::ProcessError) { finish(false); });
    connect(m_process,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,
        [this](int code,QProcess::ExitStatus status) {
            QPointer<NativeFeePreviewJob> guard(this);
            drain();
            if (guard) finish(code == 0 && status == QProcess::NormalExit);
        });
}

NativeFeePreviewJob::~NativeFeePreviewJob() {
    m_done = false; // Reject reentrant starts while cancellation notifies observers.
    disconnect(m_process,nullptr,this,nullptr);
    if (m_process->state() != QProcess::NotRunning) {
        m_process->kill(); // Only the child created by this job.
        m_process->waitForFinished(1000);
    }
    // Clear only this job's request/preview. A newer job may own the controller.
    if (m_owner && !m_operationToken.isEmpty()) m_owner->completeWalletOperation(m_operationToken,false);
    if (m_controller) m_controller->cancelRequest(m_id);
}

bool NativeFeePreviewJob::start(const QString &program, const NativeFeePreviewReceiver::Expected &expected, int timeoutMs) {
    return startImpl(program,{},expected,timeoutMs);
}

bool NativeFeePreviewJob::startWithSession(const QString &program, const QString &sessionFile,
    const NativeFeePreviewReceiver::Expected &expected, int timeoutMs) {
    const QFileInfo session(sessionFile);
    if (sessionFile.isEmpty() || !session.isAbsolute() || !session.isFile() || session.isSymLink()) return false;
    return startImpl(program,sessionFile,expected,timeoutMs);
}

bool NativeFeePreviewJob::startWithOwner(const QString &program, SwapWalletRpcProcess *owner,
    const NativeFeePreviewReceiver::Expected &expected, int timeoutMs) {
    if (!owner || !m_done) return false;
    const auto session = owner->provisionHelperSession(expected.genesis);
    if (session.isEmpty() || !owner->ownsHelperSession(session)) return false;
    return startImpl(program,session,expected,timeoutMs,owner);
}

bool NativeFeePreviewJob::startImpl(const QString &program, const QString &sessionFile,
    const NativeFeePreviewReceiver::Expected &expected, int timeoutMs, SwapWalletRpcProcess *owner) {
    const QFileInfo file(program);
    if (!m_done || m_process->state() != QProcess::NotRunning || !m_controller
        || !file.isAbsolute() || !file.isFile() || !file.isExecutable() || file.isSymLink()) return false;
    QPointer<NativeFeePreviewJob> guard(this);
    QPointer<SwapWalletRpcProcess> ownerGuard(owner);
    m_done = false; // Reserve before begin() emits reentrant UI notifications.
    m_cancelRequested = false;
    m_id.clear();
    const auto id = m_controller->begin(expected,timeoutMs);
    if (!guard) return false;
    if (!m_controller || id.isEmpty() || m_done) {
        m_done = true;
        if (m_controller && !id.isEmpty()) m_controller->cancelRequest(id);
        return false;
    }
    m_id = id;
    m_owner = ownerGuard;
    m_operationToken.clear();
    if (owner) {
        if (m_owner) m_operationToken = m_owner->beginWalletOperation();
        if (m_operationToken.isEmpty()) { finish(false); return false; }
    }
    m_input = QJsonDocument(QJsonObject{{"schema","monzero-native-fee-request-v1"},{"requestId",id},
        {"wallet",expected.wallet},{"destination",expected.destination},{"amountAtoms",expected.amountAtoms},
        {"account",expected.account},{"priority",expected.priority},{"genesis",expected.genesis}})
        .toJson(QJsonDocument::Compact);
    m_output.clear();
    m_stderrBytes = 0;
    m_done = false;
    QProcessEnvironment environment;
    environment.insert("LANG","C");
    m_process->setProcessEnvironment(environment);
    m_process->setProcessChannelMode(QProcess::SeparateChannels);
    const int remaining = m_controller->pendingTimeMs(m_id);
    if (remaining <= 0) { finish(false); return false; }
    m_timeout->start(remaining);
    QStringList arguments{"--fee-preview-stdio-v1"};
    if (!sessionFile.isEmpty()) arguments << "--session-file" << sessionFile;
    m_process->start(program,arguments);
    return true;
}

void NativeFeePreviewJob::drain() {
    if (m_done) return;
    m_process->setReadChannel(QProcess::StandardOutput);
    m_output += m_process->read(2049-m_output.size());
    m_process->setReadChannel(QProcess::StandardError);
    m_stderrBytes += m_process->read(4097-m_stderrBytes).size();
    if (m_output.size() > 2048 || m_stderrBytes > 4096) finish(false);
}

void NativeFeePreviewJob::finish(bool success) {
    if (m_done || m_finishing) return;
    m_finishing = true; // Keep starts/cancellation blocked during controller signals.
    m_timeout->stop();
    if (m_process->state() != QProcess::NotRunning) m_process->kill();
    QPointer<NativeFeePreviewJob> guard(this);
    bool accepted = success && m_controller && m_controller->accept(m_id,m_output).value("valid").toBool();
    if (!guard) return;
    // accept() emits UI notifications. A listener can cancel/replace the request
    // before it returns; that stale result must not complete the owner's operation.
    const auto current = m_controller ? m_controller->preview() : QVariantMap();
    accepted = accepted && !m_cancelRequested && current.value("valid").toBool()
        && current.value("requestId").toString() == m_id;
    if (!m_operationToken.isEmpty()) {
        accepted = m_owner && m_owner->completeWalletOperation(m_operationToken,accepted) && accepted;
        m_operationToken.clear();
    }
    if (!accepted && m_controller) m_controller->cancelRequest(m_id);
    if (!guard) return;
    m_finishing = false;
    m_done = true;
    emit completed(accepted);
}

void NativeFeePreviewJob::cancel() {
    if (m_finishing) { m_cancelRequested = true; return; }
    finish(false);
}
