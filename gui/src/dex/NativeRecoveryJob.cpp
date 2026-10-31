#include "NativeRecoveryJob.h"
#include "BackendExecutable.h"
#include "SwapWalletRpcProcess.h"
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTimer>

NativeRecoveryJob::NativeRecoveryJob(QObject *parent)
    : QObject(parent), m_process(new QProcess(this)), m_timer(new QTimer(this)) {
    m_timer->setSingleShot(true);
    m_timer->setTimerType(Qt::PreciseTimer);
    connect(m_timer,&QTimer::timeout,this,[this] { finish(false); });
    connect(m_process,&QProcess::started,this,[this] {
        if (m_process->write(m_input) != m_input.size()) { finish(false); return; }
        m_process->closeWriteChannel();
    });
    connect(m_process,&QProcess::readyReadStandardOutput,this,&NativeRecoveryJob::drain);
    connect(m_process,&QProcess::readyReadStandardError,this,&NativeRecoveryJob::drain);
    connect(m_process,&QProcess::errorOccurred,this,[this](QProcess::ProcessError) { finish(false); });
    connect(m_process,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,
        [this](int code,QProcess::ExitStatus status) {
            QPointer<NativeRecoveryJob> guard(this);
            drain();
            if (guard) finish(code == 0 && status == QProcess::NormalExit && m_stderrBytes == 0);
        });
}

NativeRecoveryJob::~NativeRecoveryJob() {
    if (m_owner) disconnect(m_owner,nullptr,this,nullptr);
    disconnect(m_process,nullptr,this,nullptr);
    if (m_process->state() != QProcess::NotRunning) {
        m_process->kill();
        m_process->waitForFinished(1000);
    }
    if (m_owner && !m_token.isEmpty()) m_owner->completeWalletOperation(m_token,false);
}

bool NativeRecoveryJob::start(const QString &program,const QString &digest,const QString &directory,
    SwapWalletRpcProcess *owner,const NativeRecoveryReceiver::Expected &expected,int timeoutMs) {
    if (!m_done || m_process->state() != QProcess::NotRunning || !owner) return false;
    m_receiver.cancel();
    const QFileInfo journal(directory);
    if (!journal.isAbsolute() || !journal.isDir() || journal.isSymLink()
        || journal.canonicalFilePath() != directory || !BackendExecutable::matches(program,digest)) return false;
    const auto id = m_receiver.begin(expected,timeoutMs);
    if (id.isEmpty()) return false;
    const auto session = owner->provisionHelperSession(expected.genesis);
    if (session.isEmpty() || !owner->ownsHelperSession(session)) { m_receiver.cancel(); return false; }
    m_token = owner->beginWalletOperation();
    if (m_token.isEmpty()) { m_receiver.cancel(); return false; }
    if (m_owner) disconnect(m_owner,nullptr,this,nullptr);
    m_owner = owner;
    const auto invalidate = [this] {
        m_receiver.cancel();
        finish(false);
    };
    connect(owner,&SwapWalletRpcProcess::stopped,this,invalidate);
    connect(owner,&QObject::destroyed,this,invalidate);
    m_done = false;
    m_input = QJsonDocument(QJsonObject{{"schema","monzero-native-recovery-request-v1"},
        {"requestId",id},{"genesis",expected.genesis},{"wallet",expected.wallet},
        {"lockTxid",expected.lockTxid},{"txid",expected.txid},
        {"minimumConfirmations",expected.minimumConfirmations}}).toJson(QJsonDocument::Compact);
    m_output.clear();
    m_stderrBytes = 0;
    QProcessEnvironment environment;
    environment.insert("LANG","C");
    m_process->setProcessEnvironment(environment);
    m_process->setProcessChannelMode(QProcess::SeparateChannels);
    m_timer->start(timeoutMs);
    m_process->start(program,{"--recovery-stdio-v1","--session-file",session,"--journal-directory",directory});
    return true;
}

void NativeRecoveryJob::drain() {
    if (m_done) return;
    m_process->setReadChannel(QProcess::StandardOutput);
    m_output += m_process->read(2049-m_output.size());
    m_process->setReadChannel(QProcess::StandardError);
    m_stderrBytes += m_process->read(4097-m_stderrBytes).size();
    if (m_output.size() > 2048 || m_stderrBytes > 4096) finish(false);
}

void NativeRecoveryJob::finish(bool success) {
    if (m_done) return;
    m_done = true;
    m_timer->stop();
    if (m_process->state() != QProcess::NotRunning) m_process->kill();
    bool accepted = success && m_receiver.accept(m_output).value("valid").toBool();
    const auto token = m_token;
    m_token.clear();
    QPointer<NativeRecoveryJob> guard(this);
    accepted = m_owner && m_owner->completeWalletOperation(token,accepted) && accepted;
    if (!guard) return;
    if (!accepted) m_receiver.cancel();
    emit completed(accepted);
}

void NativeRecoveryJob::cancel() { finish(false); }
