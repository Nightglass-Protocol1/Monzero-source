#include "CryptoVerifierClient.h"

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <unistd.h>

namespace {
const QString bubblewrapPath = "/usr/bin/bwrap";
const QRegularExpression hash64("^[0-9a-f]{64}$");
constexpr qint64 maximumVerifierBytes = 16 * 1024 * 1024;

QVariantMap rejected(const QString &error)
{
    return {{"valid", false}, {"responseAuthenticated", false},
            {"cryptographicallyVerified", false}, {"executable", false}, {"error", error}};
}

bool protectedExecutable(const QString &path, bool requireCurrentOwner)
{
    const QFileInfo info(path);
    QFileDevice::Permissions unsafeWritable = QFileDevice::WriteGroup | QFileDevice::WriteOther;
    if (requireCurrentOwner) unsafeWritable |= QFileDevice::WriteOwner;
    return info.exists() && info.isFile() && !info.isSymLink()
        && info.canonicalFilePath() == info.absoluteFilePath()
        && info.size() > 0 && info.size() <= maximumVerifierBytes
        && (info.permissions() & QFileDevice::ReadOwner)
        && (info.permissions() & QFileDevice::ExeOwner)
        && !(info.permissions() & unsafeWritable)
        && (!requireCurrentOwner || info.ownerId() == static_cast<uint>(geteuid()))
        && (requireCurrentOwner || info.ownerId() == 0);
}

QByteArray fileHash(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() <= 0
        || file.size() > maximumVerifierBytes) return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!file.atEnd()) {
        const QByteArray chunk = file.read(64 * 1024);
        if (chunk.isEmpty() && !file.atEnd()) return {};
        hash.addData(chunk);
    }
    return hash.result();
}
}

CryptoVerifierClient::CryptoVerifierClient(QString verifierPath, QString expectedSha256,
                                           int timeoutMilliseconds,
                                           int maximumResponseBytes)
    : m_verifierPath(std::move(verifierPath)), m_expectedSha256(std::move(expectedSha256)),
      m_timeoutMilliseconds(timeoutMilliseconds), m_maximumResponseBytes(maximumResponseBytes) {}

bool CryptoVerifierClient::configured() const
{
    return hash64.match(m_expectedSha256).hasMatch()
        && protectedExecutable(m_verifierPath, true)
        && protectedExecutable(bubblewrapPath, false)
        && QString::fromLatin1(fileHash(m_verifierPath).toHex()) == m_expectedSha256
        && m_timeoutMilliseconds > 0 && m_timeoutMilliseconds <= 60000
        && m_maximumResponseBytes >= 256 && m_maximumResponseBytes <= 64 * 1024;
}

QVariantMap CryptoVerifierClient::verify(const CryptoVerifierProtocol::Request &request) const
{
    if (!CryptoVerifierProtocol::canonicalRequest(request))
        return rejected("Inconsistent crypto-verifier request");
    if (!configured()) return rejected("Untrusted crypto-verifier configuration");
    const QByteArray requestBytes = QJsonDocument(request.message).toJson(QJsonDocument::Compact);
    if (requestBytes.isEmpty() || requestBytes.size() > 512 * 1024)
        return rejected("Invalid crypto-verifier request size");
    const QByteArray beforeHash = fileHash(m_verifierPath);
    if (QString::fromLatin1(beforeHash.toHex()) != m_expectedSha256)
        return rejected("Crypto-verifier changed before execution");

    QProcess process;
    process.setProcessEnvironment(QProcessEnvironment{});
    process.setProgram(bubblewrapPath);
    process.setArguments({"--unshare-all", "--unshare-user", "--die-with-parent", "--new-session",
        "--disable-userns", "--cap-drop", "ALL", "--clearenv",
        "--ro-bind", m_verifierPath, "/verifier",
        "--ro-bind", "/lib", "/lib", "--ro-bind", "/lib64", "/lib64",
        "--ro-bind", "/usr/lib", "/usr/lib", "--dev", "/dev", "--chdir", "/",
        "--", "/verifier"});
    process.start(QIODevice::ReadWrite);
    if (!process.waitForStarted(5000)) return rejected("Crypto-verifier sandbox failed to start");
    if (process.write(requestBytes) != requestBytes.size()) {
        process.kill(); process.waitForFinished(1000);
        return rejected("Crypto-verifier request write failed");
    }
    process.closeWriteChannel();
    if (!process.waitForFinished(m_timeoutMilliseconds)) {
        process.kill(); process.waitForFinished(1000);
        return rejected("Crypto-verifier timed out");
    }
    const QByteArray response = process.readAllStandardOutput();
    const QByteArray standardError = process.readAllStandardError();
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0
        || !standardError.isEmpty() || response.isEmpty()
        || response.size() > m_maximumResponseBytes)
        return rejected("Crypto-verifier process failed");
    if (fileHash(m_verifierPath) != beforeHash)
        return rejected("Crypto-verifier changed during execution");
    QVariantMap result = CryptoVerifierProtocol::validateResponse(
        response, request, m_maximumResponseBytes);
    if (!result.value("valid").toBool()) return result;
    result["responseAuthenticated"] = true;
    result["sandboxed"] = true;
    result["verifierSha256"] = m_expectedSha256;
    result["executionBlocker"] =
        "Independent verifier reproduction and protocol vectors are not established";
    result["executable"] = false;
    return result;
}
