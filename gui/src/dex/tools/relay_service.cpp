#include "../RelayPeerServer.h"
#include "../OfferIdentityStore.h"
#include <QFile>
#include <QCoreApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QTextStream>
#include <QDateTime>
#ifdef Q_OS_UNIX
#include <unistd.h>
#endif

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("monzero-dex-relay-research");
    QCommandLineParser parser;
    parser.setApplicationDescription("Isolated signed-market receiver; no trading or wallet authority.");
    parser.addHelpOption();
    parser.addOption({"state-dir", "Existing private state directory", "directory"});
    parser.addOption({"port", "Loopback TCP port (0 chooses an available port)", "port", "0"});
    parser.addOption({"run-seconds", "Stop cleanly after this many seconds (0 runs until stopped)", "seconds", "0"});
    parser.addOption({"identity-file", "Existing encrypted receipt-signing identity (never created automatically)", "file"});
    parser.addOption({"passphrase-stdin", "Read one newline-terminated passphrase from a non-terminal stdin pipe"});
    parser.process(app);
    const auto fail = [](const QString &error) {
        QTextStream(stderr) << error << '\n';
        return 1;
    };
    bool portOk = false, durationOk = false;
    const auto port = parser.value("port").toUInt(&portOk);
    const auto duration = parser.value("run-seconds").toUInt(&durationOk);
    if (!parser.positionalArguments().isEmpty() || !parser.isSet("state-dir")
        || !portOk || port > 65535 || !durationOk || duration > 86400)
        return fail("Invalid research receiver arguments");
    if (parser.isSet("identity-file") != parser.isSet("passphrase-stdin"))
        return fail("Receipt identity requires private stdin passphrase input");
    std::unique_ptr<OfferIdentityStore> identity;
    if (parser.isSet("identity-file")) {
#ifdef Q_OS_UNIX
        if (isatty(STDIN_FILENO)) return fail("Passphrase input must be a private pipe, not a terminal");
#else
        return fail("Private pipe identity loading is currently supported only on Unix");
#endif
        QFile input;
        if (!input.open(stdin, QIODevice::ReadOnly)) return fail("Cannot read private passphrase input");
        auto passphrase = input.readLine(1026);
        const bool valid = passphrase.endsWith('\n') && passphrase.size() > 1 && passphrase.size() <= 1025;
        if (valid) {
            passphrase.chop(1);
            identity.reset(new OfferIdentityStore(parser.value("identity-file"), passphrase,
                OfferIdentityStore::Mode::OpenExisting));
        }
        if (!passphrase.isEmpty()) sodium_memzero(passphrase.data(), size_t(passphrase.size()));
        passphrase.clear();
        if (!valid || !identity || !identity->ready()) return fail("Cannot unlock existing receipt identity");
    }
    const QFileInfo state(parser.value("state-dir"));
    const auto shared = QFileDevice::ReadGroup | QFileDevice::WriteGroup | QFileDevice::ExeGroup
        | QFileDevice::ReadOther | QFileDevice::WriteOther | QFileDevice::ExeOther;
    if (!state.isDir() || state.isSymLink() || (state.permissions() & shared))
        return fail("State directory must exist and be private");
#ifdef Q_OS_UNIX
    if (state.ownerId() != geteuid()) return fail("State directory must belong to this user");
#endif
    const QDir directory(state.absoluteFilePath());
    for (const auto &name : {"relay.json", "offers.json"}) {
        const QFileInfo file(directory.filePath(name));
        if (file.isSymLink() || (file.exists() && (!file.isFile() || (file.permissions() & shared))))
            return fail("Unsafe research state file");
    }
    constexpr auto relayProtocol = "monzero-dex-relay-v1";
    constexpr auto offerProtocol = "monzero-dex-offer-v1";
    constexpr auto network = "isolated-btc-xmz";
    RelayReplayStore replay(directory.filePath("relay.json"), relayProtocol, network);
    OfferBookStore book(directory.filePath("offers.json"), offerProtocol, network);
    if (!replay.ready() || !book.ready()) return fail("Cannot open research market state");
    RelayIngress::Coordinator ingress(replay, book, relayProtocol, offerProtocol, network);
    int pending = 0;
    for (const auto &entry : replay.records()) {
        const auto record = entry.toObject();
        if (record.value("pending").toBool()
            && !ingress.recover(QByteArray::fromHex(record.value("senderPublicKey").toString().toLatin1()),
                                QDateTime::currentSecsSinceEpoch()).accepted()) ++pending;
    }
    std::unique_ptr<RelayPeerServer> server(identity
        ? new RelayPeerServer(ingress, *identity->identity()) : new RelayPeerServer(ingress));
    if (!server->listen(QHostAddress::LocalHost, quint16(port))) return fail("Cannot bind loopback listener");
    const auto report = [](const QJsonObject &object) {
        QTextStream output(stdout);
        output << QJsonDocument(object).toJson(QJsonDocument::Compact) << '\n';
        output.flush();
    };
    QObject::connect(server.get(), &RelayPeerServer::applied, &app, [report](const QVariantMap &evidence) {
        report({{"event", "applied"}, {"evidence", QJsonObject::fromVariantMap(evidence)}, {"executable", false}});
    });
    QObject::connect(&app, &QCoreApplication::aboutToQuit, server.get(), &RelayPeerServer::stop);
    report({{"event", "ready"}, {"address", "127.0.0.1"}, {"port", int(server->port())},
            {"signedReceipts", bool(identity)},
            {"receiverPublicKey", identity ? QString::fromLatin1(identity->identity()->publicKey().toHex()) : QString()},
            {"network", network}, {"pendingRecovery", pending}, {"executable", false}});
    if (duration) QTimer::singleShot(int(duration * 1000), &app, &QCoreApplication::quit);
    return app.exec();
}
