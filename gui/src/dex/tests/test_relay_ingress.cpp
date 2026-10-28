#include "../RelayIngress.h"
#include "../RelayPeerConnection.h"
#include "../RelayPeerServer.h"
#include "../RelayOutboxStore.h"
#include "../RelayReceipt.h"
#include "../RelaySender.h"
#include <QTcpServer>
#include <QDateTime>
#include <QProcess>

#include <QTemporaryDir>
#include <QFile>
#include <QJsonDocument>
#include <QtTest>

namespace {
constexpr auto relayProtocol = "monzero-dex-relay-v1";
constexpr auto offerProtocol = "monzero-dex-offer-v1";
constexpr auto network = "isolated-btc-xmz";

SignedOffer::Terms offerTerms()
{
    return {offerProtocol, network,
        "12345678-1234-4abc-8def-1234567890ab", "BTC/XMZ", "sell",
        "100000", "117250000000000000", 100, 3700};
}

RelayEnvelope::Evidence relay(const OfferIdentity &peer, const QString &type,
                              quint64 sequence, const QByteArray &artifact,
                              qint64 createdAt = 100)
{
    RelayEnvelope::Terms terms;
    terms.protocol = relayProtocol;
    terms.network = network;
    terms.type = type;
    terms.sequence = sequence;
    terms.createdAt = createdAt;
    terms.expiresAt = createdAt + 600;
    terms.artifact = artifact;
    return RelayEnvelope::create(terms, peer);
}
}

class RelayIngressTest : public QObject
{
    Q_OBJECT
private slots:
    void standaloneSenderRejectsMissingConfiguration()
    {
        QProcess process;
        process.start(QCoreApplication::applicationDirPath() + "/dex_relay_send", QStringList{});
        QVERIFY(process.waitForFinished(5000));
        QCOMPARE(process.exitCode(), 1);
        QVERIFY(process.readAllStandardOutput().isEmpty());
    }

    void standaloneSignedDeliverySurvivesProcessRestart()
    {
#ifndef Q_OS_UNIX
        QSKIP("Private pipe identity loading is Unix-only");
#endif
        QTemporaryDir receiverDirectory, senderDirectory;
        OfferIdentity identity, maker, peer;
        const auto identityPath = receiverDirectory.filePath("identity.enc");
        QFile file(identityPath);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QVERIFY(file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner));
        const auto encrypted = identity.encrypt("process-test-secret");
        QCOMPARE(file.write(encrypted), qint64(encrypted.size())); file.close();
        RelayReplayStore replay(senderDirectory.filePath("relay.json"), relayProtocol, network);
        OfferBookStore book(senderDirectory.filePath("book.json"), offerProtocol, network);
        RelayIngress::Coordinator ingress(replay, book, relayProtocol, offerProtocol, network);
        const auto now = QDateTime::currentSecsSinceEpoch();
        auto terms = offerTerms(); terms.createdAt = now; terms.expiresAt = now + 3600;
        const auto offer = SignedOffer::create(terms, maker);
        const auto envelope = relay(peer, "offer", 1, offer.commitment(), now);
        const auto outboxPath = senderDirectory.filePath("outbox.json");
        {
            RelayOutboxStore outbox(outboxPath, peer.publicKey(), ingress, RelayOutboxStore::Mode::CreateNew);
            QVERIFY(outbox.append(ingress.encodeMessage(envelope, offer, now), now));
        }
        for (int attempt = 0; attempt < 2; ++attempt) {
            QProcess process;
            process.start(QCoreApplication::applicationDirPath() + "/dex_relay_service",
                {"--state-dir", receiverDirectory.path(), "--identity-file", identityPath,
                 "--passphrase-stdin", "--run-seconds", "2"});
            QVERIFY(process.waitForStarted());
            QVERIFY(process.write("process-test-secret\n") > 0);
            process.closeWriteChannel();
            QTRY_VERIFY(process.canReadLine());
            const auto ready = QJsonDocument::fromJson(process.readLine()).object();
            QCOMPARE(ready.value("event").toString(), QString("ready"));
            QCOMPARE(ready.value("receiverPublicKey").toString().toLatin1(), identity.publicKey().toHex());
            QVERIFY(ready.value("signedReceipts").toBool());
            if (attempt == 0) {
                QFile original(outboxPath);
                QVERIFY(original.open(QIODevice::ReadOnly));
                const auto before = original.readAll();
                original.close();
                QProcess wrongPin;
                wrongPin.start(QCoreApplication::applicationDirPath() + "/dex_relay_send",
                    {"--outbox-file", outboxPath, "--sender-key", QString::fromLatin1(peer.publicKey().toHex()),
                     "--receiver-key", QString::fromLatin1(maker.publicKey().toHex()),
                     "--sequence", "1", "--port", QString::number(ready.value("port").toInt())});
                QVERIFY(wrongPin.waitForStarted());
                QTRY_COMPARE(wrongPin.state(), QProcess::NotRunning);
                QCOMPARE(wrongPin.exitStatus(), QProcess::NormalExit);
                QCOMPARE(wrongPin.exitCode(), 1);
                QVERIFY(wrongPin.readAllStandardOutput().isEmpty());
                QVERIFY(original.open(QIODevice::ReadOnly));
                QCOMPARE(original.readAll(), before);
                original.close();
                // The receiver may already have persisted the offer: retry the
                // same sequence with the correct pin, never create a new offer.
            }
            QProcess delivery;
            delivery.start(QCoreApplication::applicationDirPath() + "/dex_relay_send",
                {"--outbox-file", outboxPath, "--sender-key", QString::fromLatin1(peer.publicKey().toHex()),
                 "--receiver-key", QString::fromLatin1(identity.publicKey().toHex()),
                 "--sequence", "1", "--port", QString::number(ready.value("port").toInt())});
            QVERIFY(delivery.waitForStarted());
            QTRY_COMPARE(delivery.state(), QProcess::NotRunning);
            QCOMPARE(delivery.exitCode(), 0);
            const auto delivered = QJsonDocument::fromJson(delivery.readAllStandardOutput()).object();
            QCOMPARE(delivered.value("event").toString(),
                QString(attempt ? "already-acknowledged" : "acknowledged"));
            QCOMPARE(delivered.value("sequence").toString(), QString("1"));
            QVERIFY(!delivered.value("executable").toBool(true));
            RelayOutboxStore outbox(outboxPath, peer.publicKey(), ingress, RelayOutboxStore::Mode::OpenExisting);
            QVERIFY(outbox.ready());
            QVERIFY(outbox.acknowledged(1, identity.publicKey(), QDateTime::currentSecsSinceEpoch()));
            QTRY_COMPARE(process.state(), QProcess::NotRunning);
            QCOMPARE(process.exitCode(), 0);
            QVERIFY(!process.readAllStandardOutput().contains("process-test-secret"));
            QVERIFY(!process.readAllStandardError().contains("process-test-secret"));
        }
        RelayReplayStore received(receiverDirectory.filePath("relay.json"), relayProtocol, network);
        OfferBookStore receivedBook(receiverDirectory.filePath("offers.json"), offerProtocol, network);
        QVERIFY(received.ready()); QVERIFY(receivedBook.ready());
        QCOMPARE(received.lastSequence(peer.publicKey()), quint64(1));
        QCOMPARE(receivedBook.records().size(), 1);
        QVERIFY(book.records().isEmpty());
        QVERIFY(replay.records().isEmpty());
    }

    void standaloneReceiptIdentityUsesPrivateInput()
    {
#ifndef Q_OS_UNIX
        QSKIP("Private pipe identity loading is Unix-only");
#endif
        QTemporaryDir directory;
        OfferIdentity identity;
        const QByteArray secret("test-secret-not-for-logs");
        const auto path = directory.filePath("identity.enc");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QVERIFY(file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner));
        const auto encrypted = identity.encrypt(secret);
        QCOMPARE(file.write(encrypted), qint64(encrypted.size())); file.close();
        for (bool correct : {false, true}) {
            QProcess process;
            process.start(QCoreApplication::applicationDirPath() + "/dex_relay_service",
                {"--state-dir", directory.path(), "--identity-file", path,
                 "--passphrase-stdin", "--run-seconds", "1"});
            QVERIFY(process.waitForStarted());
            const QByteArray input = (correct ? secret : QByteArray("wrong-secret")) + '\n';
            QCOMPARE(process.write(input), qint64(input.size()));
            process.closeWriteChannel();
            QTRY_COMPARE(process.state(), QProcess::NotRunning);
            const auto output = process.readAllStandardOutput();
            const auto errors = process.readAllStandardError();
            QVERIFY(!output.contains(secret)); QVERIFY(!errors.contains(secret));
            if (correct) {
                QCOMPARE(process.exitCode(), 0);
                const auto ready = QJsonDocument::fromJson(output.trimmed()).object();
                QVERIFY(ready.value("signedReceipts").toBool());
                QCOMPARE(ready.value("receiverPublicKey").toString().toLatin1(), identity.publicKey().toHex());
                QVERIFY(!ready.value("executable").toBool(true));
            } else {
                QCOMPARE(process.exitCode(), 1);
                QVERIFY(output.isEmpty());
            }
        }
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), encrypted);
    }

    void listenerAcknowledgesWithRestoredIdentity()
    {
        QTemporaryDir directory;
        RelayReplayStore replay(directory.filePath("relay.json"), relayProtocol, network);
        OfferBookStore book(directory.filePath("book.json"), offerProtocol, network);
        RelayIngress::Coordinator ingress(replay, book, relayProtocol, offerProtocol, network);
        OfferIdentity maker, peer;
        QByteArray encrypted, pinnedKey;
        {
            OfferIdentity receiver;
            pinnedKey = receiver.publicKey();
            encrypted = receiver.encrypt("test-only-passphrase");
        }
        const auto now = QDateTime::currentSecsSinceEpoch();
        auto terms = offerTerms(); terms.createdAt = now; terms.expiresAt = now + 3600;
        const auto offer = SignedOffer::create(terms, maker);
        const auto envelope = relay(peer, "offer", 1, offer.commitment(), now);
        const auto path = directory.filePath("outbox.json");
        {
            RelayOutboxStore outbox(path, peer.publicKey(), ingress, RelayOutboxStore::Mode::CreateNew);
            QVERIFY(outbox.append(ingress.encodeMessage(envelope, offer, now), now));
        }
        for (int attempt = 0; attempt < 2; ++attempt) {
            auto receiver = OfferIdentity::decrypt(encrypted, "test-only-passphrase");
            QVERIFY(receiver);
            RelayPeerServer server(ingress, *receiver);
            QVERIFY(server.listen(QHostAddress::LocalHost, 0));
            QTcpSocket socket;
            socket.connectToHost(QHostAddress::LocalHost, server.port());
            QTRY_COMPARE(socket.state(), QAbstractSocket::ConnectedState);
            RelayOutboxStore outbox(path, peer.publicKey(), ingress, RelayOutboxStore::Mode::OpenExisting);
            QVERIFY(outbox.ready());
            QCOMPARE(outbox.acknowledged(1, pinnedKey, QDateTime::currentSecsSinceEpoch()), attempt != 0);
            RelaySender sender(socket, outbox, ingress, pinnedKey);
            QSignalSpy receipts(&sender, &RelaySender::acknowledged);
            QVERIFY(sender.send(1));
            QTRY_COMPARE(receipts.size(), 1);
            QVERIFY(outbox.acknowledged(1, pinnedKey, QDateTime::currentSecsSinceEpoch()));
            server.stop();
            QCOMPARE(server.peerCount(), 0);
        }
        QCOMPARE(book.records().size(), 1);
        QCOMPARE(replay.lastSequence(peer.publicKey()), quint64(1));
    }

    void deliveryReceiptsSurviveRestartAndRejectCorruption()
    {
        QTemporaryDir directory;
        RelayReplayStore replay(directory.filePath("relay.json"), relayProtocol, network);
        OfferBookStore book(directory.filePath("book.json"), offerProtocol, network);
        RelayIngress::Coordinator ingress(replay, book, relayProtocol, offerProtocol, network);
        OfferIdentity maker, peer, receiver, other;
        const auto offer = SignedOffer::create(offerTerms(), maker);
        const auto envelope = relay(peer, "offer", 1, offer.commitment());
        const auto accepted = ingress.ingestOffer(envelope, offer, 101);
        const auto receipt = RelayReceipt::create(accepted, envelope, receiver, 101);
        const auto path = directory.filePath("outbox.json");
        {
            RelayOutboxStore outbox(path, peer.publicKey(), ingress, RelayOutboxStore::Mode::CreateNew);
            QVERIFY(outbox.append(ingress.encodeMessage(envelope, offer, 101), 101));
            QVERIFY(!outbox.acknowledged(1, receiver.publicKey(), 102));
            QVERIFY(!outbox.acknowledge(2, receiver.publicKey(), receipt, 102));
            QVERIFY(!outbox.acknowledge(1, other.publicKey(), receipt, 102));
            QVERIFY(outbox.acknowledge(1, receiver.publicKey(), receipt, 102));
            QVERIFY(outbox.acknowledge(1, receiver.publicKey(), receipt, 102));
        }
        {
            RelayOutboxStore restored(path, peer.publicKey(), ingress, RelayOutboxStore::Mode::OpenExisting);
            QVERIFY(restored.ready());
            QVERIFY(restored.acknowledged(1, receiver.publicKey(), 800));
            QVERIFY(!restored.acknowledged(1, other.publicKey(), 800));
            QCOMPARE(restored.nextSequence(), quint64(2));
        }
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        auto root = QJsonDocument::fromJson(file.readAll()).object(); file.close();
        auto records = root.value("messages").toArray();
        auto record = records[0].toObject();
        record["receipts"] = QJsonObject{{QString::fromLatin1(receiver.publicKey().toHex()), "e30="}};
        records[0] = record; root["messages"] = records;
        const auto corrupted = QJsonDocument(root).toJson(QJsonDocument::Compact);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QCOMPARE(file.write(corrupted), qint64(corrupted.size())); file.close();
        RelayOutboxStore invalid(path, peer.publicKey(), ingress, RelayOutboxStore::Mode::OpenExisting);
        QVERIFY(!invalid.ready());
        QVERIFY(!invalid.acknowledged(1, receiver.publicKey(), 800));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), corrupted);
    }

    void senderRequiresPinnedReceiptForPersistedMessage()
    {
        QTemporaryDir directory;
        RelayReplayStore replay(directory.filePath("relay.json"), relayProtocol, network);
        OfferBookStore book(directory.filePath("book.json"), offerProtocol, network);
        RelayIngress::Coordinator ingress(replay, book, relayProtocol, offerProtocol, network);
        OfferIdentity maker, peer, receiver, impostor;
        RelayOutboxStore outbox(directory.filePath("outbox.json"), peer.publicKey(), ingress,
            RelayOutboxStore::Mode::CreateNew);
        const auto now = QDateTime::currentSecsSinceEpoch();
        auto terms = offerTerms(); terms.createdAt = now; terms.expiresAt = now + 3600;
        const auto offer = SignedOffer::create(terms, maker);
        const auto envelope = relay(peer, "offer", 1, offer.commitment(), now);
        QVERIFY(outbox.append(ingress.encodeMessage(envelope, offer, now), now));
        for (bool correctPeer : {true, false}) {
            QTcpServer server;
            QVERIFY(server.listen(QHostAddress::LocalHost, 0));
            QTcpSocket client;
            client.connectToHost(QHostAddress::LocalHost, server.serverPort());
            QTRY_VERIFY(server.hasPendingConnections());
            QTRY_COMPARE(client.state(), QAbstractSocket::ConnectedState);
            auto socket = server.nextPendingConnection();
            RelayPeerConnection connection(*socket, ingress, correctPeer ? receiver : impostor);
            RelaySender sender(client, outbox, ingress, receiver.publicKey());
            QSignalSpy acknowledgements(&sender, &RelaySender::acknowledged);
            QVERIFY(!sender.send(2)); // Not in durable outbox.
            QVERIFY(sender.send(1));
            QVERIFY(sender.busy());
            QVERIFY(!sender.send(1));
            QCOMPARE(acknowledgements.size(), 0); // Queued bytes are not acceptance.
            if (correctPeer) {
                QTRY_COMPARE(acknowledgements.size(), 1);
                QVERIFY(outbox.acknowledged(1, receiver.publicKey(), QDateTime::currentSecsSinceEpoch()));
                QVERIFY(!sender.failed());
                QVERIFY(!sender.busy());
                QCOMPARE(acknowledgements.at(0).at(0).toULongLong(), quint64(1));
            } else {
                QTRY_VERIFY(sender.failed());
                QCOMPARE(acknowledgements.size(), 0);
            }
        }
        QCOMPARE(outbox.nextSequence(), quint64(2));
    }

    void receiverSendsSignedReceiptAfterPersistence()
    {
        QTemporaryDir directory;
        RelayReplayStore replay(directory.filePath("relay.json"), relayProtocol, network);
        OfferBookStore book(directory.filePath("book.json"), offerProtocol, network);
        RelayIngress::Coordinator ingress(replay, book, relayProtocol, offerProtocol, network);
        OfferIdentity maker, peer, receiver;
        const auto now = QDateTime::currentSecsSinceEpoch();
        auto terms = offerTerms(); terms.createdAt = now; terms.expiresAt = now + 3600;
        const auto offer = SignedOffer::create(terms, maker);
        const auto envelope = relay(peer, "offer", 1, offer.commitment(), now);
        const auto message = ingress.encodeMessage(envelope, offer, now);
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        QTcpSocket client;
        client.connectToHost(QHostAddress::LocalHost, server.serverPort());
        QTRY_VERIFY(server.hasPendingConnections());
        QTRY_COMPARE(client.state(), QAbstractSocket::ConnectedState);
        auto socket = server.nextPendingConnection();
        RelayPeerConnection connection(*socket, ingress, receiver);
        const auto framed = RelayPeerConnection::frame(message);
        for (int attempt = 0; attempt < 2; ++attempt) {
            QCOMPARE(client.write(framed), qint64(framed.size()));
            QTRY_VERIFY(client.bytesAvailable() >= 4);
            const auto header = client.read(4);
            quint32 size = 0;
            for (char byte : header) size = (size << 8) | static_cast<unsigned char>(byte);
            QVERIFY(size > 0 && size <= 2048);
            QTRY_VERIFY(client.bytesAvailable() >= size);
            const auto receipt = client.read(size);
            QVERIFY(RelayReceipt::verify(receipt, envelope, receiver.publicKey(),
                QDateTime::currentSecsSinceEpoch()));
            QCOMPARE(book.records().size(), 1);
            QCOMPARE(replay.lastSequence(peer.publicKey()), quint64(1));
        }
        const auto invalid = RelayPeerConnection::frame("{}");
        QCOMPARE(client.write(invalid), qint64(invalid.size()));
        QTRY_VERIFY(connection.failed());
        QTRY_COMPARE(client.state(), QAbstractSocket::UnconnectedState);
        QCOMPARE(client.bytesAvailable(), qint64(0));
        QCOMPARE(book.records().size(), 1);
    }

    void receiptsBindPersistedMessageAndPinnedReceiver()
    {
        QTemporaryDir directory;
        RelayReplayStore replay(directory.filePath("relay.json"), relayProtocol, network);
        OfferBookStore book(directory.filePath("offers.json"), offerProtocol, network);
        RelayIngress::Coordinator codec(replay, book, relayProtocol, offerProtocol, network);
        OfferIdentity maker, peer, receiver, other;
        const auto offer = SignedOffer::create(offerTerms(), maker);
        const auto envelope = relay(peer, "offer", 1, offer.commitment());
        QVERIFY(RelayReceipt::create({}, envelope, receiver, 101).isEmpty());
        const auto accepted = codec.ingestOffer(envelope, offer, 101);
        QVERIFY(accepted.accepted());
        const auto receipt = RelayReceipt::create(accepted, envelope, receiver, 101);
        QVERIFY(!receipt.isEmpty());
        QVERIFY(RelayReceipt::verify(receipt, envelope, receiver.publicKey(), 102));
        QVERIFY(!RelayReceipt::verify(receipt, envelope, other.publicKey(), 102));
        QVERIFY(!RelayReceipt::verify(receipt, envelope, receiver.publicKey(), 100));
        const auto next = relay(peer, "offer", 2, offer.commitment());
        QVERIFY(!RelayReceipt::verify(receipt, next, receiver.publicKey(), 102));
        QVERIFY(RelayReceipt::create(accepted, next, receiver, 102).isEmpty());
        QVERIFY(RelayReceipt::create(accepted, envelope, receiver, 99).isEmpty());
        QVERIFY(!RelayReceipt::verify(receipt + " ", envelope, receiver.publicKey(), 102));
        auto changed = QJsonDocument::fromJson(receipt).object();
        auto changedBody = changed.value("body").toObject();
        changedBody["acceptedAt"] = "102";
        changed["body"] = changedBody;
        QVERIFY(!RelayReceipt::verify(QJsonDocument(changed).toJson(QJsonDocument::Compact),
            envelope, receiver.publicKey(), 102));
        // Delayed acknowledgement of durable recovery may outlive the envelope.
        QVERIFY(RelayReceipt::verify(RelayReceipt::create(accepted, envelope, receiver, 800),
            envelope, receiver.publicKey(), 801));
    }

    void outboxPersistsExactSequencesWithoutChangingReceiverState()
    {
        QTemporaryDir directory;
        RelayReplayStore replay(directory.filePath("relay.json"), relayProtocol, network);
        OfferBookStore book(directory.filePath("offers.json"), offerProtocol, network);
        RelayIngress::Coordinator codec(replay, book, relayProtocol, offerProtocol, network);
        OfferIdentity maker, peer, other;
        const auto offer = SignedOffer::create(offerTerms(), maker);
        const auto first = codec.encodeMessage(relay(peer, "offer", 1, offer.commitment()), offer, 101);
        const auto second = codec.encodeMessage(relay(peer, "offer", 2, offer.commitment()), offer, 101);
        const auto path = directory.filePath("outbox.json");
        {
            RelayOutboxStore missing(path, peer.publicKey(), codec, RelayOutboxStore::Mode::OpenExisting);
            QVERIFY(!missing.ready());
            QVERIFY(!QFile::exists(path));
        }
        {
            RelayOutboxStore outbox(path, peer.publicKey(), codec, RelayOutboxStore::Mode::CreateNew);
            QVERIFY(outbox.ready());
            QCOMPARE(outbox.nextSequence(), quint64(1));
            QVERIFY(!outbox.append(second, 101));
            QVERIFY(outbox.append(first, 101));
            QVERIFY(outbox.append(first, 102));
            QCOMPARE(outbox.nextSequence(), quint64(2));
            const auto conflicting = codec.encodeMessage(relay(peer, "offer", 1, offer.commitment(), 101), offer, 102);
            QVERIFY(!outbox.append(conflicting, 102));
            QVERIFY(!outbox.append(codec.encodeMessage(relay(other, "offer", 2,
                offer.commitment()), offer, 101), 101));
            RelayOutboxStore locked(path, peer.publicKey(), codec, RelayOutboxStore::Mode::OpenExisting);
            QVERIFY(!locked.ready());
        }
        {
            RelayOutboxStore restored(path, peer.publicKey(), codec, RelayOutboxStore::Mode::OpenExisting);
            QVERIFY(restored.ready());
            QCOMPARE(restored.message(1), first);
            QCOMPARE(restored.nextSequence(), quint64(2));
            QVERIFY(restored.append(second, 102));
            QCOMPARE(restored.message(2), second);
            QVERIFY(!restored.append(second, 701));
        }
        {
            RelayOutboxStore wrong(path, other.publicKey(), codec, RelayOutboxStore::Mode::OpenExisting);
            QVERIFY(!wrong.ready());
        }
        QVERIFY(book.records().isEmpty());
        QVERIFY(replay.records().isEmpty());
        QVERIFY(codec.encodeMessage(relay(peer, "offer", 3, offer.commitment(), 3700), offer, 3701).isEmpty());
        auto wrongTerms = offerTerms(); wrongTerms.network = "wrong-network";
        const auto wrongOffer = SignedOffer::create(wrongTerms, maker);
        QVERIFY(codec.encodeMessage(relay(peer, "offer", 3, wrongOffer.commitment()), wrongOffer, 101).isEmpty());
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        auto root = QJsonDocument::fromJson(file.readAll()).object();
        file.close();
        auto records = root.value("messages").toArray();
        auto record = records.at(0).toObject();
        auto changedMessage = QJsonDocument::fromJson(first).object();
        auto artifact = changedMessage.value("artifact").toObject();
        artifact["amountAtomic"] = "999";
        changedMessage["artifact"] = artifact;
        record["message"] = QString::fromLatin1(QJsonDocument(changedMessage)
            .toJson(QJsonDocument::Compact).toBase64());
        records[0] = record;
        root["messages"] = records;
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        const auto corruptedBytes = QJsonDocument(root).toJson(QJsonDocument::Compact);
        QCOMPARE(file.write(corruptedBytes), qint64(corruptedBytes.size()));
        file.close();
        RelayOutboxStore corrupted(path, peer.publicKey(), codec, RelayOutboxStore::Mode::OpenExisting);
        QVERIFY(!corrupted.ready());
        QCOMPARE(corrupted.nextSequence(), quint64(0));
        QVERIFY(corrupted.message(1).isEmpty());
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), corruptedBytes);
    }

    void standaloneReceiverRecoversPendingCancellationAtStartup()
    {
        QTemporaryDir directory;
        const auto now = QDateTime::currentSecsSinceEpoch();
        OfferIdentity maker, peer;
        auto terms = offerTerms();
        terms.createdAt = now - 2000;
        terms.expiresAt = now + 1000;
        const auto offer = SignedOffer::create(terms, maker);
        const auto cancellation = SignedOfferCancellation::create(
            {offerProtocol, network, offer.offerId(), offer.commitment(), now - 1000}, maker);
        {
            RelayReplayStore replay(directory.filePath("relay.json"), relayProtocol, network);
            OfferBookStore book(directory.filePath("offers.json"), offerProtocol, network);
            RelayIngress::Coordinator ingress(replay, book, relayProtocol, offerProtocol, network);
            QVERIFY(!ingress.ingestCancellation(relay(peer, "cancellation", 1,
                cancellation.commitment(), now - 1000), cancellation, now - 999).accepted());
            QVERIFY(replay.pendingEnvelope(peer.publicKey()).accepted());
            QVERIFY(book.insert(offer, now));
        }
        QProcess process;
        process.start(QCoreApplication::applicationDirPath() + "/dex_relay_service",
            {"--state-dir", directory.path(), "--run-seconds", "1"});
        QVERIFY(process.waitForStarted());
        QTRY_VERIFY(process.canReadLine());
        const auto ready = QJsonDocument::fromJson(process.readLine()).object();
        QCOMPARE(ready.value("event").toString(), QString("ready"));
        QCOMPARE(ready.value("pendingRecovery").toInt(-1), 0);
        QVERIFY(process.waitForFinished(5000));
        QCOMPARE(process.exitCode(), 0);
        RelayReplayStore replay(directory.filePath("relay.json"), relayProtocol, network);
        OfferBookStore book(directory.filePath("offers.json"), offerProtocol, network);
        QVERIFY(replay.ready());
        QVERIFY(book.ready());
        QVERIFY(!replay.pendingEnvelope(peer.publicKey()).accepted());
        QCOMPARE(book.cancellations().size(), 1);
        QVERIFY(book.active(now).isEmpty());
    }

    void standaloneReceiverRejectsUnsafeStartup()
    {
        QTemporaryDir directory;
        const auto executable = QCoreApplication::applicationDirPath() + "/dex_relay_service";
        const auto rejected = [&](const QStringList &arguments) {
            QProcess process;
            process.start(executable, arguments);
            if (!process.waitForStarted() || !process.waitForFinished(5000)) {
                process.kill();
                process.waitForFinished();
                return false;
            }
            return process.exitStatus() == QProcess::NormalExit
                && process.exitCode() != 0 && process.readAllStandardOutput().isEmpty();
        };
        QVERIFY(rejected({}));
        QVERIFY(rejected({"--state-dir", directory.path(), "--port", "65536"}));
        QVERIFY(rejected({"--state-dir", directory.path(), "--run-seconds", "86401"}));
        QVERIFY(rejected({"--state-dir", directory.filePath("missing")}));
        {
            RelayReplayStore locked(directory.filePath("relay.json"), relayProtocol, network);
            QVERIFY(locked.ready());
            QVERIFY(rejected({"--state-dir", directory.path()}));
        }
        QFile corrupted(directory.filePath("relay.json"));
        QVERIFY(corrupted.open(QIODevice::WriteOnly));
        QVERIFY(corrupted.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner));
        QCOMPARE(corrupted.write("broken"), qint64(6));
        corrupted.close();
        QVERIFY(rejected({"--state-dir", directory.path()}));
        QVERIFY(corrupted.open(QIODevice::ReadOnly));
        QCOMPARE(corrupted.readAll(), QByteArray("broken"));
    }

    void standaloneReceiverPersistsAcrossProcessRestart()
    {
        QTemporaryDir receiverDirectory, senderDirectory;
        RelayReplayStore replay(senderDirectory.filePath("relay.json"), relayProtocol, network);
        OfferBookStore book(senderDirectory.filePath("offers.json"), offerProtocol, network);
        RelayIngress::Coordinator encoder(replay, book, relayProtocol, offerProtocol, network);
        OfferIdentity maker, peer;
        const auto now = QDateTime::currentSecsSinceEpoch();
        auto terms = offerTerms();
        terms.createdAt = now;
        terms.expiresAt = now + 3600;
        const auto offer = SignedOffer::create(terms, maker);
        const auto message = encoder.encodeMessage(relay(peer, "offer", 1,
            offer.commitment(), now), offer, now);
        for (int attempt = 0; attempt < 2; ++attempt) {
            QProcess process;
            process.start(QCoreApplication::applicationDirPath() + "/dex_relay_service",
                {"--state-dir", receiverDirectory.path(), "--run-seconds", "2"});
            QVERIFY(process.waitForStarted());
            QTRY_VERIFY(process.canReadLine());
            const auto ready = QJsonDocument::fromJson(process.readLine()).object();
            QCOMPARE(ready.value("event").toString(), QString("ready"));
            QCOMPARE(ready.value("address").toString(), QString("127.0.0.1"));
            QCOMPARE(ready.value("executable").toBool(true), false);
            QTcpSocket client;
            client.connectToHost(QHostAddress::LocalHost, quint16(ready.value("port").toInt()));
            QTRY_COMPARE(client.state(), QAbstractSocket::ConnectedState);
            client.write(RelayPeerConnection::frame(message));
            QTRY_VERIFY(process.canReadLine());
            QCOMPARE(QJsonDocument::fromJson(process.readLine()).object().value("event").toString(), QString("applied"));
            QVERIFY(process.waitForFinished(5000));
            QCOMPARE(process.exitCode(), 0);
            QCOMPARE(process.exitStatus(), QProcess::NormalExit);
            OfferBookStore restored(receiverDirectory.filePath("offers.json"), offerProtocol, network);
            QVERIFY(restored.ready());
            QCOMPARE(restored.active(now).size(), 1);
        }
    }

    void serverEnforcesGlobalCapAcrossAddresses()
    {
        QTemporaryDir directory;
        RelayReplayStore replay(directory.filePath("relay.json"), relayProtocol, network);
        OfferBookStore book(directory.filePath("book.json"), offerProtocol, network);
        RelayIngress::Coordinator ingress(replay, book, relayProtocol, offerProtocol, network);
        RelayPeerServer server(ingress);
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        QTcpSocket clients[9];
        for (int i = 0; i < 8; ++i) {
            QVERIFY(clients[i].bind(QHostAddress(QString("127.0.0.%1").arg(2 + i / 2)), 0));
            clients[i].connectToHost(QHostAddress::LocalHost, server.port());
            QTRY_COMPARE(server.peerCount(), i + 1);
        }
        QVERIFY(clients[8].bind(QHostAddress("127.0.0.6"), 0));
        QSignalSpy closed(&clients[8], &QTcpSocket::disconnected);
        clients[8].connectToHost(QHostAddress::LocalHost, server.port());
        QTRY_COMPARE(closed.size(), 1);
        QCOMPARE(server.peerCount(), 8);
        clients[0].abort();
        QTRY_COMPARE(server.peerCount(), 7);
        QVERIFY(clients[8].bind(QHostAddress("127.0.0.6"), 0));
        clients[8].connectToHost(QHostAddress::LocalHost, server.port());
        QTRY_COMPARE(server.peerCount(), 8);
        server.stop();
        QCOMPARE(server.peerCount(), 0);
    }

    void serverEvictsSilentPeer()
    {
        QTemporaryDir directory;
        RelayReplayStore replay(directory.filePath("relay.json"), relayProtocol, network);
        OfferBookStore book(directory.filePath("book.json"), offerProtocol, network);
        RelayIngress::Coordinator ingress(replay, book, relayProtocol, offerProtocol, network);
        RelayPeerServer server(ingress);
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        QTcpSocket client;
        client.connectToHost(QHostAddress::LocalHost, server.port());
        QTRY_COMPARE(server.peerCount(), 1);
        QTRY_COMPARE_WITH_TIMEOUT(server.peerCount(), 0, 65000);
        QTRY_COMPARE(client.state(), QAbstractSocket::UnconnectedState);
        client.connectToHost(QHostAddress::LocalHost, server.port());
        QTRY_COMPARE(server.peerCount(), 1);
        QVERIFY(book.records().isEmpty());
        QVERIFY(replay.records().isEmpty());
    }

    void serverCapsConnectionsAndReclaimsSlots()
    {
        QTemporaryDir directory;
        RelayReplayStore replay(directory.filePath("relay.json"), relayProtocol, network);
        OfferBookStore book(directory.filePath("book.json"), offerProtocol, network);
        RelayIngress::Coordinator ingress(replay, book, relayProtocol, offerProtocol, network);
        RelayPeerServer server(ingress);
        QCOMPARE(server.port(), quint16(0));
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        QVERIFY(!server.listen(QHostAddress::LocalHost, 0));
        QTcpSocket first, second, excess;
        first.connectToHost(QHostAddress::LocalHost, server.port());
        QTRY_COMPARE(server.peerCount(), 1);
        const auto now = QDateTime::currentSecsSinceEpoch();
        auto terms = offerTerms();
        terms.createdAt = now;
        terms.expiresAt = now + 3600;
        OfferIdentity maker, peer;
        const auto offer = SignedOffer::create(terms, maker);
        QSignalSpy applied(&server, &RelayPeerServer::applied);
        first.write(RelayPeerConnection::frame(ingress.encodeMessage(
            relay(peer, "offer", 1, offer.commitment(), now), offer, now)));
        QTRY_COMPARE(applied.size(), 1);
        QCOMPARE(book.active(now).size(), 1);
        second.connectToHost(QHostAddress::LocalHost, server.port());
        QTRY_COMPARE(server.peerCount(), 2);
        QSignalSpy closed(&excess, &QTcpSocket::disconnected);
        excess.connectToHost(QHostAddress::LocalHost, server.port());
        QTRY_COMPARE(closed.size(), 1);
        QCOMPARE(server.peerCount(), 2);
        first.abort();
        QTRY_COMPARE(server.peerCount(), 1);
        excess.connectToHost(QHostAddress::LocalHost, server.port());
        QTRY_COMPARE(server.peerCount(), 2);
        // Invalid framing also releases its admitted connection slot.
        excess.write(QByteArray(4, '\0'));
        QTRY_COMPARE(server.peerCount(), 1);
        server.stop();
        QCOMPARE(server.peerCount(), 0);
        QTRY_COMPARE(second.state(), QAbstractSocket::UnconnectedState);
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        server.stop();
        QCOMPARE(book.records().size(), 1);
        QCOMPARE(replay.lastSequence(peer.publicKey()), quint64(1));
    }

    void outboundQueueBackpressureIsBounded()
    {
        QTemporaryDir directory;
        RelayReplayStore replay(directory.filePath("relay.json"), relayProtocol, network);
        OfferBookStore book(directory.filePath("book.json"), offerProtocol, network);
        RelayIngress::Coordinator ingress(replay, book, relayProtocol, offerProtocol, network);
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        QTcpSocket client;
        client.connectToHost(QHostAddress::LocalHost, server.serverPort());
        QTRY_VERIFY(server.hasPendingConnections());
        QTRY_COMPARE(client.state(), QAbstractSocket::ConnectedState);
        RelayPeerConnection connection(client, ingress);
        // Do not process events: queued writes must remain bounded even when
        // the producer outruns the socket. Transport framing is payload-agnostic.
        const QByteArray payload(8192, 'a');
        for (int i = 0; i < 7; ++i) QVERIFY(connection.send(payload));
        QCOMPARE(client.bytesToWrite(), qint64(7 * (8192 + 4)));
        QVERIFY(!connection.send(payload));
        QVERIFY(client.bytesToWrite() <= 65536);
        QVERIFY(!connection.failed()); // Backpressure is retryable, not peer fault.
        client.abort();
        QVERIFY(!connection.send(payload));
        QVERIFY(replay.records().isEmpty());
        QVERIFY(book.records().isEmpty());
    }

    void independentPeersExchangeAndPersistMarketMessages()
    {
        QTemporaryDir directory;
        const auto now = QDateTime::currentSecsSinceEpoch();
        OfferIdentity maker, peer;
        auto terms = offerTerms();
        terms.createdAt = now;
        terms.expiresAt = now + 3600;
        const auto offer = SignedOffer::create(terms, maker);
        {
            RelayReplayStore replayA(directory.filePath("relay-a.json"), relayProtocol, network);
            RelayReplayStore replayB(directory.filePath("relay-b.json"), relayProtocol, network);
            OfferBookStore bookA(directory.filePath("book-a.json"), offerProtocol, network);
            OfferBookStore bookB(directory.filePath("book-b.json"), offerProtocol, network);
            RelayIngress::Coordinator ingressA(replayA, bookA, relayProtocol, offerProtocol, network);
            RelayIngress::Coordinator ingressB(replayB, bookB, relayProtocol, offerProtocol, network);
            QTcpServer server;
            QVERIFY(server.listen(QHostAddress::LocalHost, 0));
            QTcpSocket client;
            client.connectToHost(QHostAddress::LocalHost, server.serverPort());
            QTRY_VERIFY(server.hasPendingConnections());
            QTRY_COMPARE(client.state(), QAbstractSocket::ConnectedState);
            auto *socket = server.nextPendingConnection();
            RelayPeerConnection connectionA(client, ingressA);
            RelayPeerConnection connectionB(*socket, ingressB);
            QSignalSpy receivedA(&connectionA, &RelayPeerConnection::applied);
            QSignalSpy receivedB(&connectionB, &RelayPeerConnection::applied);
            const auto message = ingressA.encodeMessage(relay(peer, "offer", 1,
                offer.commitment(), now), offer, now);
            QVERIFY(!connectionA.send({}));
            QVERIFY(!connectionA.send(QByteArray(8193, 'a')));
            QVERIFY(connectionA.send(message));
            QTRY_COMPARE(receivedB.size(), 1);
            QVERIFY(bookA.records().isEmpty()); // Sending is not local acceptance.
            QCOMPARE(bookB.active(now).size(), 1);
            QVERIFY(connectionB.send(message));
            QTRY_COMPARE(receivedA.size(), 1);
            QCOMPARE(bookA.records(), bookB.records());
            const auto cancellation = SignedOfferCancellation::create(
                {offerProtocol, network, offer.offerId(), offer.commitment(), now}, maker);
            const auto cancelMessage = ingressA.encodeMessage(relay(peer, "cancellation", 2,
                cancellation.commitment(), now), cancellation, now);
            QVERIFY(connectionA.send(cancelMessage));
            QTRY_COMPARE(receivedB.size(), 2);
            QVERIFY(connectionB.send(cancelMessage));
            QTRY_COMPARE(receivedA.size(), 2);
            QCOMPARE(bookA.cancellations(), bookB.cancellations());
            QVERIFY(bookA.active(now).isEmpty());
            QVERIFY(bookB.active(now).isEmpty());
            QVERIFY(!connectionA.failed());
            QVERIFY(!connectionB.failed());
        }
        for (const auto &suffix : {QString("a"), QString("b")}) {
            RelayReplayStore restored(directory.filePath("relay-" + suffix + ".json"),
                                      relayProtocol, network);
            OfferBookStore book(directory.filePath("book-" + suffix + ".json"), offerProtocol, network);
            QVERIFY(restored.ready());
            QVERIFY(book.ready());
            QCOMPARE(restored.lastSequence(peer.publicKey()), quint64(2));
            QCOMPARE(book.cancellations().size(), 1);
            QVERIFY(book.active(now).isEmpty());
        }
    }

    void loopbackRejectsIncompleteMessages_data()
    {
        QTest::addColumn<bool>("disconnect");
        QTest::newRow("truncated-disconnect") << true;
        QTest::newRow("partial-message-deadline") << false;
    }
    void loopbackRejectsIncompleteMessages()
    {
        QFETCH(bool, disconnect);
        QTemporaryDir directory;
        RelayReplayStore replayStore(directory.filePath("relay.json"), relayProtocol, network);
        OfferBookStore book(directory.filePath("book.json"), offerProtocol, network);
        RelayIngress::Coordinator ingress(replayStore, book, relayProtocol, offerProtocol, network);
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        QTcpSocket client;
        client.connectToHost(QHostAddress::LocalHost, server.serverPort());
        QTRY_VERIFY(server.hasPendingConnections());
        auto *socket = server.nextPendingConnection();
        RelayPeerConnection connection(*socket, ingress);
        client.write(QByteArray(1, '\0'));
        client.flush();
        QTest::qWait(50);
        QVERIFY(!connection.failed());
        if (disconnect) client.disconnectFromHost();
        QTRY_VERIFY_WITH_TIMEOUT(connection.failed(), 12000);
        QVERIFY(book.records().isEmpty());
        QVERIFY(replayStore.records().isEmpty());
    }

    void loopbackDeliversFragmentedOfferAndCancellation()
    {
        QTemporaryDir directory;
        RelayReplayStore replayStore(directory.filePath("relay.json"), relayProtocol, network);
        OfferBookStore book(directory.filePath("book.json"), offerProtocol, network);
        RelayIngress::Coordinator ingress(replayStore, book, relayProtocol, offerProtocol, network);
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        QTcpSocket client;
        client.connectToHost(QHostAddress::LocalHost, server.serverPort());
        QTRY_VERIFY(server.hasPendingConnections());
        auto *socket = server.nextPendingConnection();
        RelayPeerConnection connection(*socket, ingress);
        QSignalSpy applied(&connection, &RelayPeerConnection::applied);
        const auto now = QDateTime::currentSecsSinceEpoch();
        auto terms = offerTerms();
        terms.createdAt = now;
        terms.expiresAt = now + 3600;
        OfferIdentity maker, peer;
        const auto offer = SignedOffer::create(terms, maker);
        const auto message = ingress.encodeMessage(relay(peer, "offer", 1,
            offer.commitment(), now), offer, now);
        const auto bytes = RelayPeerConnection::frame(message);
        QVERIFY(!bytes.isEmpty());
        QCOMPARE(client.write(bytes.left(2)), qint64(2));
        client.flush();
        QTest::qWait(20);
        QCOMPARE(applied.size(), 0);
        QCOMPARE(client.write(bytes.mid(2)), qint64(bytes.size() - 2));
        QTRY_COMPARE(applied.size(), 1);
        QCOMPARE(book.active(now).size(), 1);
        const auto cancellation = SignedOfferCancellation::create(
            {offerProtocol, network, offer.offerId(), offer.commitment(), now}, maker);
        const auto cancelMessage = ingress.encodeMessage(relay(peer, "cancellation", 2,
            cancellation.commitment(), now), cancellation, now);
        const auto cancelFrame = RelayPeerConnection::frame(cancelMessage);
        // Coalesced exact retries are idempotent at the durable ingress boundary.
        client.write(cancelFrame + cancelFrame);
        QTRY_COMPARE(applied.size(), 3);
        QCOMPARE(book.cancellations().size(), 1);
        QVERIFY(book.active(now).isEmpty());
        QVERIFY(!connection.failed());
        client.write(QByteArray(4, char(0xff)));
        QTRY_VERIFY(connection.failed());
        QCOMPARE(book.cancellations().size(), 1);
        QVERIFY(!connection.send(message));
    }

    void completeMarketMessagesValidateBeforeMutation()
    {
        QTemporaryDir directory;
        RelayReplayStore replayStore(directory.filePath("relay.json"), relayProtocol, network);
        OfferBookStore book(directory.filePath("book.json"), offerProtocol, network);
        RelayIngress::Coordinator ingress(replayStore, book, relayProtocol, offerProtocol, network);
        OfferIdentity maker, peer;
        const auto offer = SignedOffer::create(offerTerms(), maker);
        const auto wrapper = relay(peer, "offer", 1, offer.commitment());
        const auto bytes = ingress.encodeMessage(wrapper, offer, 101);
        QVERIFY(!bytes.isEmpty());
        QVERIFY(book.records().isEmpty());
        QVERIFY(replayStore.records().isEmpty());
        auto root = QJsonDocument::fromJson(bytes).object();
        auto artifact = root.value("artifact").toObject();
        artifact["amountAtomic"] = "999";
        root["artifact"] = artifact;
        QVERIFY(!ingress.ingestMessage(QJsonDocument(root).toJson(QJsonDocument::Compact), 101).accepted());
        QVERIFY(!ingress.ingestMessage(bytes + ' ', 101).accepted());
        QVERIFY(!ingress.ingestMessage(bytes.left(bytes.size() - 1), 101).accepted());
        QVERIFY(!ingress.ingestMessage(QByteArray(8193, 'a'), 101).accepted());
        QVERIFY(book.records().isEmpty());
        QVERIFY(replayStore.records().isEmpty());
        QVERIFY(ingress.ingestMessage(bytes, 101).accepted());
        QVERIFY(ingress.ingestMessage(bytes, 102).accepted());
        QCOMPARE(book.records().size(), 1);
        const auto cancellation = SignedOfferCancellation::create(
            {offerProtocol, network, offer.offerId(), offer.commitment(), 200}, maker);
        const auto cancelBytes = ingress.encodeMessage(relay(peer, "cancellation", 2,
            cancellation.commitment(), 200), cancellation, 201);
        QVERIFY(!cancelBytes.isEmpty());
        QVERIFY(ingress.ingestMessage(cancelBytes, 201).accepted());
        QCOMPARE(book.cancellations().size(), 1);
        QVERIFY(book.active(201).isEmpty());
        QVERIFY(!ingress.ingestMessage(bytes, 202).accepted());
    }

    void rejectsCorruptedInnerRecoveryPayload()
    {
        QTemporaryDir directory;
        const auto path = directory.filePath("relay.json");
        const auto bookPath = directory.filePath("offers.json");
        OfferIdentity maker, peer;
        const auto offer = SignedOffer::create(offerTerms(), maker);
        const auto cancellation = SignedOfferCancellation::create(
            {offerProtocol, network, offer.offerId(), offer.commitment(), 200}, maker);
        {
            RelayReplayStore replayStore(path, relayProtocol, network);
            OfferBookStore book(bookPath, offerProtocol, network);
            RelayIngress::Coordinator ingress(replayStore, book, relayProtocol,
                                              offerProtocol, network);
            QVERIFY(!ingress.ingestCancellation(relay(peer, "cancellation", 1,
                cancellation.commitment(), 200), cancellation, 201).accepted());
        }
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        auto root = QJsonDocument::fromJson(file.readAll()).object();
        file.close();
        auto records = root.value("records").toArray();
        auto record = records.at(0).toObject();
        auto payload = record.value("artifactPayload").toObject();
        payload["cancelledAt"] = "201";
        record["artifactPayload"] = payload;
        records[0] = record;
        root["records"] = records;
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        const auto bytes = QJsonDocument(root).toJson();
        QCOMPARE(file.write(bytes), qint64(bytes.size()));
        file.close();
        RelayReplayStore replayStore(path, relayProtocol, network);
        OfferBookStore book(bookPath, offerProtocol, network);
        RelayIngress::Coordinator ingress(replayStore, book, relayProtocol,
                                          offerProtocol, network);
        QVERIFY(book.insert(offer, 801));
        QVERIFY(!ingress.recover(peer.publicKey(), 801).accepted());
        QCOMPARE(book.cancellations().size(), 0);
        QCOMPARE(book.active(801).size(), 1);
        QVERIFY(replayStore.pendingEnvelope(peer.publicKey()).accepted());
        QVERIFY(!replayStore.accept(relay(peer, "offer", 2,
            offer.commitment(), 801), 801));
    }

    void pendingCancellationBlocksAdvancementAcrossRestart_data()
    {
        QTest::addColumn<bool>("preaccepted");
        QTest::newRow("direct-ingress") << false;
        QTest::newRow("previously-replay-checked") << true;
    }

    void pendingCancellationBlocksAdvancementAcrossRestart()
    {
        QFETCH(bool, preaccepted);
        QTemporaryDir directory;
        const auto replayPath = directory.filePath("relay.json");
        const auto bookPath = directory.filePath("offers.json");
        OfferIdentity maker, peer;
        const auto offer = SignedOffer::create(offerTerms(), maker);
        const auto cancellation = SignedOfferCancellation::create(
            {offerProtocol, network, offer.offerId(), offer.commitment(), 200}, maker);
        const auto pending = relay(peer, "cancellation", 1,
                                   cancellation.commitment(), 200);
        const auto next = relay(peer, "offer", 2, offer.commitment(), 200);
        {
            RelayReplayStore replayStore(replayPath, relayProtocol, network);
            OfferBookStore book(bookPath, offerProtocol, network);
            RelayIngress::Coordinator ingress(replayStore, book, relayProtocol,
                                              offerProtocol, network);
            if (preaccepted) QVERIFY(replayStore.accept(pending, 201));
            QVERIFY(!ingress.ingestCancellation(pending, cancellation, 201).accepted());
            QVERIFY(!ingress.ingestOffer(next, offer, 201).accepted());
            QVERIFY(!replayStore.accept(next, 201));
            QCOMPARE(book.records().size(), 0);
        }
        RelayReplayStore replayStore(replayPath, relayProtocol, network);
        OfferBookStore book(bookPath, offerProtocol, network);
        RelayIngress::Coordinator ingress(replayStore, book, relayProtocol,
                                          offerProtocol, network);
        QVERIFY(replayStore.ready());
        const auto recovered = replayStore.pendingEnvelope(peer.publicKey());
        QVERIFY(recovered.accepted());
        QCOMPARE(recovered.commitment(), pending.commitment());
        QVERIFY(!ingress.ingestOffer(next, offer, 202).accepted());
        // Recovery after the wrapper expired still requires the exact pending
        // commitment; an unrecorded expired wrapper must remain rejected.
        QVERIFY(book.insert(offer, 801));
        QVERIFY(!replayStore.accept(pending, 801));
        QVERIFY(!ingress.ingestOffer(next, offer, 801).accepted());
        const auto changed = relay(peer, "cancellation", 1,
                                   cancellation.commitment(), 201);
        QVERIFY(!ingress.ingestCancellation(changed, cancellation, 801).accepted());
        RelayIngress::Coordinator wrongRelayProtocol(replayStore, book,
            "other-relay-protocol", offerProtocol, network);
        QVERIFY(!wrongRelayProtocol.ingestCancellation(pending, cancellation, 801)
                     .accepted());
        RelayIngress::Coordinator wrongOfferProtocol(replayStore, book,
            relayProtocol, "other-offer-protocol", network);
        QVERIFY(!wrongOfferProtocol.ingestCancellation(pending, cancellation, 801)
                     .accepted());
        RelayIngress::Coordinator wrongNetwork(replayStore, book,
            relayProtocol, offerProtocol, "other-network");
        QVERIFY(!wrongNetwork.ingestCancellation(pending, cancellation, 801)
                     .accepted());
        QCOMPARE(book.cancellations().size(), 0);
        QCOMPARE(replayStore.lastSequence(peer.publicKey()), quint64(1));
        QVERIFY(ingress.recover(peer.publicKey(), 801).accepted());
        QVERIFY(!replayStore.pendingEnvelope(peer.publicKey()).accepted());
        QVERIFY(!ingress.ingestCancellation(pending, cancellation, 802).accepted());
        QVERIFY(ingress.ingestOffer(relay(peer, "offer", 2,
            offer.commitment(), 801), offer, 802).accepted());
        QCOMPARE(book.active(802).size(), 0);
        QCOMPARE(replayStore.lastSequence(peer.publicKey()), quint64(2));
    }

    void ingestsOfferAndCancellationWithExactRetries()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString replayPath = directory.filePath("relay.json");
        const QString bookPath = directory.filePath("offers.json");
        OfferIdentity maker, relayPeer;
        const auto offer = SignedOffer::create(offerTerms(), maker);
        const auto offerRelay = relay(relayPeer, "offer", 1,
                                      offer.commitment());
        SignedOfferCancellation::Terms cancellationTerms{offerProtocol, network,
            offer.offerId(), offer.commitment(), 200};
        const auto cancellation = SignedOfferCancellation::create(
            cancellationTerms, maker);
        const auto cancellationRelay = relay(relayPeer, "cancellation", 2,
            cancellation.commitment(), 200);
        QByteArray offerIngressCommitment;
        {
            RelayReplayStore replayStore(replayPath, relayProtocol, network);
            OfferBookStore book(bookPath, offerProtocol, network);
            RelayIngress::Coordinator ingress(replayStore, book, relayProtocol,
                                              offerProtocol, network);
            const auto accepted = ingress.ingestOffer(offerRelay, offer, 101);
            QVERIFY(accepted.accepted());
            QVERIFY(!accepted.details().value("executable").toBool());
            QCOMPARE(accepted.action(), QString("offer"));
            QCOMPARE(book.records().size(), 1);
            offerIngressCommitment = accepted.commitment();
            QCOMPARE(ingress.ingestOffer(offerRelay, offer, 102).commitment(),
                     offerIngressCommitment);

            const auto cancelled = ingress.ingestCancellation(
                cancellationRelay, cancellation, 201);
            QVERIFY(cancelled.accepted());
            QCOMPARE(cancelled.action(), QString("cancellation"));
            QCOMPARE(book.cancellations().size(), 1);
            QCOMPARE(book.active(201).size(), 0);
        }

        RelayReplayStore replayStore(replayPath, relayProtocol, network);
        OfferBookStore book(bookPath, offerProtocol, network);
        RelayIngress::Coordinator restored(replayStore, book, relayProtocol,
                                           offerProtocol, network);
        QVERIFY(!restored.ingestOffer(offerRelay, offer, 202).accepted());
        QVERIFY(restored.ingestCancellation(cancellationRelay, cancellation,
                                            202).accepted());
        QCOMPARE(replayStore.lastSequence(relayPeer.publicKey()), quint64(2));
    }

    void rejectsSubstitutionBeforeConsumingSequence()
    {
        QTemporaryDir directory;
        RelayReplayStore replayStore(directory.filePath("relay.json"),
                                     relayProtocol, network);
        OfferBookStore book(directory.filePath("offers.json"), offerProtocol,
                            network);
        RelayIngress::Coordinator ingress(replayStore, book, relayProtocol,
                                          offerProtocol, network);
        OfferIdentity maker, peer;
        const auto offer = SignedOffer::create(offerTerms(), maker);

        QVERIFY(!ingress.ingestOffer(relay(peer, "cancellation", 1,
                                           offer.commitment()), offer, 101)
                     .accepted());
        QVERIFY(!ingress.ingestOffer(relay(peer, "offer", 1,
                                           QByteArray(32, 'x')), offer, 101)
                     .accepted());
        QCOMPARE(replayStore.lastSequence(peer.publicKey()), quint64(0));
        QVERIFY(ingress.ingestOffer(relay(peer, "offer", 1,
                                         offer.commitment()), offer, 101)
                    .accepted());
        QCOMPARE(replayStore.lastSequence(peer.publicKey()), quint64(1));
    }

    void rejectsSequenceGapDomainAndUnknownCancellation()
    {
        QTemporaryDir directory;
        RelayReplayStore replayStore(directory.filePath("relay.json"),
                                     relayProtocol, network);
        OfferBookStore book(directory.filePath("offers.json"), offerProtocol,
                            network);
        RelayIngress::Coordinator ingress(replayStore, book, relayProtocol,
                                          offerProtocol, network);
        OfferIdentity maker, peer, offerPeer;
        const auto offer = SignedOffer::create(offerTerms(), maker);
        QVERIFY(!ingress.ingestOffer(relay(peer, "offer", 2,
                                           offer.commitment()), offer, 101)
                     .accepted());

        SignedOfferCancellation::Terms cancellationTerms{offerProtocol, network,
            offer.offerId(), offer.commitment(), 200};
        const auto cancellation = SignedOfferCancellation::create(
            cancellationTerms, maker);
        const auto cancelRelay = relay(peer, "cancellation", 1,
            cancellation.commitment(), 200);
        QVERIFY(!ingress.ingestCancellation(cancelRelay, cancellation, 201)
                     .accepted());
        // The valid but premature cancellation sequence is durable and can be
        // retried after the referenced offer arrives through another peer.
        QCOMPARE(replayStore.lastSequence(peer.publicKey()), quint64(1));
        QVERIFY(ingress.ingestOffer(relay(offerPeer, "offer", 1,
                                         offer.commitment()), offer, 202)
                    .accepted());
        QVERIFY(ingress.ingestCancellation(cancelRelay, cancellation, 202)
                    .accepted());

        RelayReplayStore otherReplay(directory.filePath("other-relay.json"),
                                     relayProtocol, "other-network");
        OfferBookStore otherBook(directory.filePath("other-book.json"),
                                 offerProtocol, "other-network");
        RelayIngress::Coordinator wrongDomain(otherReplay, otherBook,
            relayProtocol, offerProtocol, "other-network");
        QVERIFY(!wrongDomain.ingestOffer(relay(peer, "offer", 1,
                                               offer.commitment()), offer, 101)
                     .accepted());
    }
};

QTEST_GUILESS_MAIN(RelayIngressTest)
#include "test_relay_ingress.moc"
