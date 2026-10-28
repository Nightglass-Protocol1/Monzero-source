#include "../RelaySender.h"
#include <QCoreApplication>
#include <QCommandLineParser>
#include <QDateTime>
#include <QHostAddress>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTextStream>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("monzero-relay-send-research");
    QCommandLineParser parser;
    parser.setApplicationDescription("Deliver one already signed, durable public-market message to a loopback lab receiver.");
    parser.addHelpOption();
    for (const auto &name : {"outbox-file", "sender-key", "receiver-key", "sequence", "port"})
        parser.addOption({name, "Explicit isolated delivery configuration", "value"});
    parser.process(app);
    const auto fail = [] {
        QTextStream(stderr) << "Cannot deliver research message; retain the original outbox\n";
        return 1;
    };
    const auto senderHex = parser.value("sender-key").toLatin1();
    const auto receiverHex = parser.value("receiver-key").toLatin1();
    const auto senderKey = QByteArray::fromHex(senderHex), receiverKey = QByteArray::fromHex(receiverHex);
    bool portOk = false, sequenceOk = false;
    const auto port = parser.value("port").toUInt(&portOk);
    const auto sequenceText = parser.value("sequence");
    const auto sequence = sequenceText.toULongLong(&sequenceOk);
    if (!parser.positionalArguments().isEmpty() || !parser.isSet("outbox-file")
        || !portOk || !port || port > 65535 || !sequenceOk || !sequence
        || sequenceText != QString::number(sequence)
        || senderKey.size() != 32 || senderKey.toHex() != senderHex || senderKey == QByteArray(32, '\0')
        || receiverKey.size() != 32 || receiverKey.toHex() != receiverHex || receiverKey == QByteArray(32, '\0'))
        return fail();

    // The codec's unused receiver stores are ephemeral, never the sender's history.
    QTemporaryDir scratch;
    if (!scratch.isValid()) return fail();
    RelayReplayStore replay(scratch.filePath("relay.json"), "monzero-dex-relay-v1", "isolated-btc-xmz");
    OfferBookStore book(scratch.filePath("offers.json"), "monzero-dex-offer-v1", "isolated-btc-xmz");
    if (!replay.ready() || !book.ready()) return fail();
    RelayIngress::Coordinator codec(replay, book, "monzero-dex-relay-v1", "monzero-dex-offer-v1", "isolated-btc-xmz");
    RelayOutboxStore outbox(parser.value("outbox-file"), senderKey, codec, RelayOutboxStore::Mode::OpenExisting);
    if (!outbox.ready() || outbox.message(sequence).isEmpty()) return fail();
    const auto report = [&](const QString &event) {
        QTextStream output(stdout);
        output << QJsonDocument(QJsonObject{{"event", event}, {"sequence", sequenceText},
            {"executable", false}}).toJson(QJsonDocument::Compact) << '\n';
        output.flush();
    };
    if (outbox.acknowledged(sequence, receiverKey, QDateTime::currentSecsSinceEpoch())) {
        report("already-acknowledged");
        return 0;
    }
    QTcpSocket socket;
    RelaySender sender(socket, outbox, codec, receiverKey);
    bool finished = false;
    const auto finish = [&](bool success) {
        if (finished) return;
        finished = true;
        if (success) report("acknowledged");
        else fail();
        app.exit(success ? 0 : 1);
    };
    QObject::connect(&socket, &QTcpSocket::connected, &app, [&] {
        if (!sender.send(sequence)) finish(false);
    });
    QObject::connect(&socket, &QTcpSocket::errorOccurred, &app,
        [&](QAbstractSocket::SocketError) { finish(false); });
    QObject::connect(&sender, &RelaySender::rejected, &app, [&] { finish(false); });
    QObject::connect(&sender, &RelaySender::acknowledged, &app,
        [&](quint64, const QByteArray &) { finish(true); });
    QTimer::singleShot(20000, &app, [&] { finish(false); });
    QTimer::singleShot(0, &app, [&] { socket.connectToHost(QHostAddress::LocalHost, quint16(port)); });
    const auto result = app.exec();
    QObject::disconnect(&socket, nullptr, &app, nullptr);
    QObject::disconnect(&sender, nullptr, &app, nullptr);
    socket.abort();
    return result;
}
