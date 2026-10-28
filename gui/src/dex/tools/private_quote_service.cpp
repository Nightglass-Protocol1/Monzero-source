#include "../PrivateQuoteServer.h"
#include "../PrivateQuoteSession.h"
#include "../SettlementJournal.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QCommandLineParser>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
#include <QSslKey>
#include <QCryptographicHash>
#include <sodium.h>
#ifdef Q_OS_UNIX
#include <unistd.h>
#endif

namespace {
QByteArray privateFile(const QString &path)
{
    const QFileInfo file(path), parent(file.absolutePath());
    const auto shared = QFileDevice::ReadGroup | QFileDevice::WriteGroup | QFileDevice::ExeGroup
        | QFileDevice::ReadOther | QFileDevice::WriteOther | QFileDevice::ExeOther;
    if (path.isEmpty() || file.isSymLink() || !file.isFile() || (file.permissions() & shared)
        || parent.isSymLink() || !parent.isDir() || (parent.permissions() & shared)) return {};
#ifdef Q_OS_UNIX
    if (file.ownerId() != geteuid() || parent.ownerId() != geteuid()) return {};
#endif
    QFile input(path);
    if (!input.open(QIODevice::ReadOnly)) return {};
    const auto bytes = input.read(16385);
    return bytes.size() <= 16384 && input.atEnd() ? bytes : QByteArray();
}
void report(const QJsonObject &value)
{
    QTextStream output(stdout);
    output << QJsonDocument(value).toJson(QJsonDocument::Compact) << '\n'; output.flush();
}
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("monzero-private-quote-research");
    QCommandLineParser parser;
    parser.addHelpOption();
    for (const auto &name : {"state-dir", "tls-cert", "tls-key", "peer-cert", "maker-key", "taker-key"})
        parser.addOption({name, "Explicit private research configuration", "value"});
    parser.addOption({"port", "Loopback listener port", "port", "0"});
    parser.addOption({"run-seconds", "Stop after this duration; zero runs until stopped", "seconds", "0"});
    parser.process(app);
    const auto fail = [] { QTextStream(stderr) << "Invalid or unavailable private quote configuration\n"; return 1; };
    bool portOk = false, durationOk = false;
    const auto port = parser.value("port").toUInt(&portOk);
    const auto duration = parser.value("run-seconds").toUInt(&durationOk);
    const auto makerHex = parser.value("maker-key").toLatin1(), takerHex = parser.value("taker-key").toLatin1();
    const auto maker = QByteArray::fromHex(makerHex), taker = QByteArray::fromHex(takerHex);
    if (!parser.positionalArguments().isEmpty() || !parser.isSet("state-dir")
        || !portOk || port > 65535 || !durationOk || duration > 86400
        || maker.size() != 32 || taker.size() != 32 || maker.toHex() != makerHex || taker.toHex() != takerHex
        || maker == taker || maker == QByteArray(32, '\0') || taker == QByteArray(32, '\0')) return fail();
    const auto certs = QSslCertificate::fromData(privateFile(parser.value("tls-cert")));
    const auto peers = QSslCertificate::fromData(privateFile(parser.value("peer-cert")));
    auto keyBytes = privateFile(parser.value("tls-key"));
    const QSslKey key(keyBytes, QSsl::Rsa);
    if (!keyBytes.isEmpty()) sodium_memzero(keyBytes.data(), size_t(keyBytes.size()));
    keyBytes.clear();
    if (certs.size() != 1 || peers.size() != 1 || key.isNull()) return fail();
    auto config = QSslConfiguration::defaultConfiguration();
    config.setLocalCertificate(certs.first()); config.setPrivateKey(key); config.setCaCertificates(peers);
    const QDir directory(parser.value("state-dir"));
    SignedQuoteAcceptor acceptor(directory.filePath("quotes.json"), "btc-xmz-adaptor-draft-v1",
        "btc-regtest", "xmz-fakechain", "xmz-fakechain", "xmz-fakechain-lab-v1");
    if (!acceptor.store().ready()) return fail();
    QVector<RecoveredMutualQuote> history;
    if (!acceptor.recoverMutualHistory(QDateTime::currentSecsSinceEpoch(), history)) return fail();
    SettlementJournal journal(directory.filePath("settlement.json"));
    if (!journal.ready()) return fail();
    PrivateQuoteServer server(config, peers.first().digest(QCryptographicHash::Sha256));
    QObject::connect(&server, &PrivateQuoteServer::authenticated, &app, [&](PrivateQuoteChannel *channel) {
        auto *session = new PrivateQuoteSession(*channel, acceptor, journal, maker, taker, channel);
        QObject::connect(session, &PrivateQuoteSession::quoteAccepted, &app, [](const QVariantMap &details) {
            report({{"event", "quote-accepted"}, {"commitment", details.value("commitment").toString()},
                {"executable", false}});
        });
    });
    if (!server.start(QHostAddress::LocalHost, quint16(port))) return fail();
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &server, &PrivateQuoteServer::stop);
    report({{"event", "ready"}, {"address", "127.0.0.1"}, {"port", int(server.serverPort())},
        {"executable", false}});
    if (duration) QTimer::singleShot(int(duration * 1000), &app, &QCoreApplication::quit);
    return app.exec();
}
