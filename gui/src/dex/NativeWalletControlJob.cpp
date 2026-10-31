#include "NativeWalletControlJob.h"
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTimer>
#include <QUuid>
#include <sodium.h>

namespace {
void clearPrivate(QByteArray &bytes) { if (!bytes.isEmpty()) sodium_memzero(bytes.data(),bytes.size()); bytes.clear(); }
void clearPrivate(QString &text) { if (!text.isEmpty()) sodium_memzero(text.data(),text.size()*sizeof(QChar)); text.clear(); }
bool canonicalId(const QString &text) {
    const QUuid id(text);
    return !id.isNull() && id.toString(QUuid::WithoutBraces) == text;
}
bool addressShape(const QString &text) {
    const QString alphabet = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";
    if (text.size()<90 || text.size()>110) return false;
    for (const auto ch : text) if (!alphabet.contains(ch)) return false;
    return true; // Checksum/network validation is performed by the verified native helper.
}
}

NativeWalletControlJob::NativeWalletControlJob(SwapWalletRpcProcess *owner, QObject *parent)
    : QObject(parent), m_owner(owner), m_process(new QProcess(this)), m_timeout(new QTimer(this)) {
    m_timeout->setSingleShot(true);
    m_timeout->setTimerType(Qt::PreciseTimer);
    connect(m_timeout,&QTimer::timeout,this,[this] { finish(false); });
    connect(m_process,&QProcess::started,this,[this] {
        const bool written = m_process->write(m_input) == m_input.size();
        clearPrivate(m_input);
        if (!written) { finish(false); return; }
        m_process->closeWriteChannel();
    });
    connect(m_process,&QProcess::readyReadStandardOutput,this,&NativeWalletControlJob::drain);
    connect(m_process,&QProcess::readyReadStandardError,this,&NativeWalletControlJob::drain);
    connect(m_process,&QProcess::errorOccurred,this,[this](QProcess::ProcessError) { finish(false); });
    connect(m_process,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[this](int code,QProcess::ExitStatus status) {
        QPointer<NativeWalletControlJob> guard(this);
        drain();
        if (guard) finish(code == 0 && status == QProcess::NormalExit);
    });
}

NativeWalletControlJob::~NativeWalletControlJob() {
    disconnect(m_process,nullptr,this,nullptr);
    if (m_process->state() != QProcess::NotRunning) { m_process->kill(); m_process->waitForFinished(1000); }
    if (!m_done && m_owner) m_owner->completeWalletOperation(m_token,false);
    clearPrivate(m_input);
    clearPrivate(m_output);
}

bool NativeWalletControlJob::validRequest(const QString &operation,const QString &walletId,
    const QByteArray &password,const QString &expectedAddress) {
    const bool close=operation=="close";
    if (password.size()>1024 || walletId.size()>36 || expectedAddress.size()>110) return false;
    QString secret=QString::fromUtf8(password);
    const bool valid=(operation=="create" || operation=="open" || close)
        && (close ? walletId.isEmpty() && password.isEmpty() : canonicalId(walletId)
            && !password.isEmpty() && !password.contains('\0') && secret.toUtf8()==password)
        && (operation=="create" ? expectedAddress.isEmpty() : addressShape(expectedAddress));
    clearPrivate(secret);
    return valid;
}

bool NativeWalletControlJob::start(const QString &program, const QString &session, const QString &operation,
    const QString &walletId, QByteArray password, const QString &expectedAddress, int timeoutMs) {
    const QFileInfo executable(program), file(session);
    const bool requestValid=validRequest(operation,walletId,password,expectedAddress);
    if (!requestValid) { clearPrivate(password); return false; }
    QString secret = QString::fromUtf8(password);
    const bool valid = !m_used && m_owner && m_owner->ownsHelperSession(session) && timeoutMs>0 && timeoutMs<=30000
        && executable.isAbsolute() && executable.isFile() && executable.isExecutable() && !executable.isSymLink()
        && file.isAbsolute() && file.isFile() && !file.isSymLink()
        && requestValid;
    clearPrivate(password);
    if (!valid) { clearPrivate(secret); return false; }
    m_request = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_input = QJsonDocument(QJsonObject{{"schema","monzero-wallet-control-v1"},{"requestId",m_request},
        {"operation",operation},{"walletId",walletId},{"password",secret},{"expectedAddress",expectedAddress}}).toJson(QJsonDocument::Compact);
    clearPrivate(secret);
    if (m_input.size()>4096) { clearPrivate(m_input); return false; }
    m_token = m_owner->beginWalletOperation();
    if (m_token.isEmpty()) { clearPrivate(m_input); return false; }
    m_used = true;
    m_done = false;
    m_operation = operation;
    m_expected = expectedAddress;
    QProcessEnvironment environment;
    environment.insert("LANG","C");
    m_process->setProcessEnvironment(environment);
    m_process->setProcessChannelMode(QProcess::SeparateChannels);
    m_deadline = QDeadlineTimer(timeoutMs,Qt::PreciseTimer);
    m_timeout->start(timeoutMs);
    m_process->start(program,{"--wallet-control-stdio-v1","--session-file",session});
    return true;
}

void NativeWalletControlJob::drain() {
    if (m_done) return;
    m_process->setReadChannel(QProcess::StandardOutput);
    m_output += m_process->read(2049-m_output.size());
    m_process->setReadChannel(QProcess::StandardError);
    m_errors += m_process->read(4097-m_errors).size();
    if (m_output.size()>2048 || m_errors>4096) finish(false);
}

void NativeWalletControlJob::finish(bool success) {
    if (m_done) return;
    m_done = true;
    m_timeout->stop();
    if (m_process->state() != QProcess::NotRunning) m_process->kill();
    const auto document = QJsonDocument::fromJson(m_output);
    const auto object = document.object();
    // Fixed ASCII schema from serde_json; exact canonical encoding also rejects
    // duplicate keys which QJsonDocument would otherwise silently overwrite.
    const bool reply = success && !m_deadline.hasExpired() && document.isObject() && object.size()==5
        && document.toJson(QJsonDocument::Compact) == m_output
        && object.value("schema") == "monzero-wallet-control-result-v1"
        && object.value("requestId") == m_request && object.value("operation") == m_operation
        && object.value("readyForTrading").isBool() && !object.value("readyForTrading").toBool()
        && addressShape(object.value("address").toString())
        && (m_operation == "create" || object.value("address") == m_expected);
    const bool verified = m_owner && m_owner->completeWalletOperation(m_token,reply);
    if (verified) m_result = object.toVariantMap();
    clearPrivate(m_input);
    clearPrivate(m_output);
    emit completed(verified);
}

void NativeWalletControlJob::cancel() { finish(false); }
