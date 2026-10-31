#include "../PrivateQuoteChannel.h"
#include "../PrivateQuoteServer.h"
#include "../PrivateQuoteSession.h"
#include "../SettlementJournal.h"
#include "TestAcceptedQuote.h"
#include <QDateTime>
#include <QtTest>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QProcess>
#include <QFile>
#include <QSslKey>
#include <QCryptographicHash>
#include <limits>

class TlsFixture final : public QTcpServer {
public:
    QSslCertificate certificate;
    QSslKey key;
    QByteArray plaintext;
    void incomingConnection(qintptr descriptor) override {
        auto *socket = new QSslSocket(this);
        socket->setLocalCertificate(certificate); socket->setPrivateKey(key);
        socket->setPeerVerifyMode(QSslSocket::VerifyNone); // Test server only.
        connect(socket, &QSslSocket::readyRead, this, [this, socket] {
            const auto bytes = socket->readAll(); plaintext += bytes; socket->write(bytes);
        });
        if (!socket->setSocketDescriptor(descriptor)) { socket->deleteLater(); return; }
        socket->startServerEncryption();
    }
};

class MutualTlsFixture final : public QTcpServer {
public:
    QSslConfiguration configuration;
    QByteArray pin, plaintext;
    QPointer<PrivateQuoteChannel> accepted;
    void incomingConnection(qintptr descriptor) override {
        accepted = new PrivateQuoteChannel(configuration, pin, this);
        auto *channel = accepted.data();
        connect(channel, &PrivateQuoteChannel::received, this, [this, channel](const QByteArray &message) {
            plaintext += message;
            channel->send(message);
        });
        if (!channel->acceptPeer(descriptor)) {
            QTcpSocket closer; closer.setSocketDescriptor(descriptor); closer.abort();
        }
    }
};

class PrivateQuoteChannelTest : public QObject {
    Q_OBJECT
private slots:
    void writeAdmissionCountsBothTlsQueues() {
        using Profile=PrivateQuoteChannel::Profile;
        QVERIFY(PrivateQuoteChannel::hasWriteCapacity(0,0,410000,Profile::LitecoinNegotiation));
        QVERIFY(!PrivateQuoteChannel::hasWriteCapacity(0,0,410001,Profile::LitecoinNegotiation));
        QVERIFY(!PrivateQuoteChannel::hasWriteCapacity(0,0,410000,Profile::Quotes));
        QVERIFY(!PrivateQuoteChannel::hasWriteCapacity(0,0,1,static_cast<Profile>(999)));
        constexpr int message = 98304;
        constexpr qint64 available = 2 * message - message - 4;
        QVERIFY(PrivateQuoteChannel::hasWriteCapacity(0, 0, message));
        QVERIFY(PrivateQuoteChannel::hasWriteCapacity(available, 0, message));
        QVERIFY(PrivateQuoteChannel::hasWriteCapacity(0, available, message));
        QVERIFY(PrivateQuoteChannel::hasWriteCapacity(available / 2, available / 2, message));
        QVERIFY(!PrivateQuoteChannel::hasWriteCapacity(0, available + 1, message));
        QVERIFY(!PrivateQuoteChannel::hasWriteCapacity(available / 2, available / 2 + 1, message));
        QVERIFY(!PrivateQuoteChannel::hasWriteCapacity(std::numeric_limits<qint64>::max(), 1, 1));
        QVERIFY(!PrivateQuoteChannel::hasWriteCapacity(1, std::numeric_limits<qint64>::max(), 1));
        QVERIFY(!PrivateQuoteChannel::hasWriteCapacity(-1, 0, 1));
        QVERIFY(!PrivateQuoteChannel::hasWriteCapacity(0, -1, 1));
        QVERIFY(!PrivateQuoteChannel::hasWriteCapacity(0, 0, 0));
        QVERIFY(!PrivateQuoteChannel::hasWriteCapacity(0, 0, message + 1));
    }

    void standalonePersistsQuoteAcrossProcessRestart_data() {
        QTest::addColumn<bool>("interruptedBeforeJournal");
        QTest::newRow("new-quote") << false;
        QTest::newRow("quote-persisted-journal-missing") << true;
    }
    void standalonePersistsQuoteAcrossProcessRestart() {
        QFETCH(bool, interruptedBeforeJournal);
        QTemporaryDir directory, receiverState, senderState;
        QProcess openssl;
        const auto certPath = directory.filePath("cert.pem"), keyPath = directory.filePath("key.pem");
        openssl.start("openssl", {"req", "-x509", "-newkey", "rsa:2048", "-nodes",
            "-keyout", keyPath, "-out", certPath, "-days", "1", "-subj", "/CN=localhost",
            "-addext", "subjectAltName=DNS:localhost"});
        QVERIFY(openssl.waitForFinished(10000)); QCOMPARE(openssl.exitCode(), 0);
        for (const auto &path : {certPath, keyPath})
            QVERIFY(QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner));
        QFile cert(certPath), key(keyPath);
        QVERIFY(cert.open(QIODevice::ReadOnly)); QVERIFY(key.open(QIODevice::ReadOnly));
        auto config = QSslConfiguration::defaultConfiguration();
        config.setLocalCertificate(QSslCertificate(cert.readAll()));
        config.setPrivateKey(QSslKey(key.readAll(), QSsl::Rsa));
        config.setCaCertificates({config.localCertificate()});
        const auto pin = config.localCertificate().digest(QCryptographicHash::Sha256);
        const auto now = QDateTime::currentSecsSinceEpoch();
        auto terms = TestAcceptedQuote::create(directory.filePath("fixture.json"), now).terms();
        OfferIdentity maker, taker;
        terms.makerKey = maker.publicKey(); terms.takerKey = taker.publicKey();
        terms.payerKey = terms.takerKey;
        const QByteArray domain("xmz-fakechain-lab-v1");
        const auto built = SettlementTerms::build(terms, now);
        const auto makerSig = maker.sign(built.payload, domain);
        const auto takerSig = taker.sign(SignedQuoteAcceptor::takerAcceptancePayload(terms, now), domain);
        if (interruptedBeforeJournal) {
            SignedQuoteAcceptor interrupted(receiverState.filePath("quotes.json"), terms.protocolId,
                terms.payNetwork, terms.receiveNetwork, terms.xmzNetwork, domain);
            QVERIFY(interrupted.acceptMutualEvidence(terms, makerSig, takerSig, now).mutuallyAuthorized());
            QVERIFY(!QFile::exists(receiverState.filePath("settlement.json")));
        }
        SignedQuoteAcceptor outgoing(senderState.filePath("quotes.json"), terms.protocolId,
            terms.payNetwork, terms.receiveNetwork, terms.xmzNetwork, domain);
        QByteArray firstJournal;
        for (int attempt = 0; attempt < 3; ++attempt) {
            QByteArray corruptedBytes;
            if (attempt == 2) {
                QFile stored(receiverState.filePath("quotes.json"));
                QVERIFY(stored.open(QIODevice::ReadOnly));
                auto root = QJsonDocument::fromJson(stored.readAll()).object(); stored.close();
                auto records = root.value("records").toArray();
                auto record = records.at(0).toObject();
                auto artifact = QJsonDocument::fromJson(QByteArray::fromBase64(
                    record.value("signedArtifact").toString().toLatin1())).object();
                artifact["takerSignature"] = QString(128, '0');
                record["signedArtifact"] = QString::fromLatin1(
                    QJsonDocument(artifact).toJson(QJsonDocument::Compact).toBase64());
                records[0] = record; root["records"] = records;
                corruptedBytes = QJsonDocument(root).toJson(QJsonDocument::Compact);
                QVERIFY(stored.open(QIODevice::WriteOnly | QIODevice::Truncate));
                QCOMPARE(stored.write(corruptedBytes), qint64(corruptedBytes.size()));
            }
            QProcess process;
            process.start(QCoreApplication::applicationDirPath() + "/dex_private_quote_service",
                {"--state-dir", receiverState.path(), "--tls-cert", certPath, "--tls-key", keyPath,
                 "--peer-cert", certPath, "--maker-key", QString::fromLatin1(maker.publicKey().toHex()),
                 "--taker-key", QString::fromLatin1(taker.publicKey().toHex()), "--run-seconds", "2"});
            QVERIFY(process.waitForStarted());
            if (attempt == 2) {
                QVERIFY(process.waitForFinished(5000));
                QCOMPARE(process.exitCode(), 1);
                QVERIFY(process.readAllStandardOutput().isEmpty());
                QFile unchanged(receiverState.filePath("quotes.json"));
                QVERIFY(unchanged.open(QIODevice::ReadOnly));
                QCOMPARE(unchanged.readAll(), corruptedBytes);
                continue;
            }
            QTRY_VERIFY(process.canReadLine());
            const auto ready = QJsonDocument::fromJson(process.readLine()).object();
            QCOMPARE(ready.value("event").toString(), QString("ready"));
            QCOMPARE(ready.value("address").toString(), QString("127.0.0.1"));
            PrivateQuoteChannel client(config, pin);
            PrivateQuoteSession session(client, outgoing, maker.publicKey(), taker.publicKey());
            QSignalSpy acknowledgements(&session, &PrivateQuoteSession::peerAcknowledged);
            QVERIFY(client.connectToPeer("127.0.0.1", quint16(ready.value("port").toInt()), "localhost"));
            QTRY_VERIFY(client.ready());
            QVERIFY(session.send(terms, makerSig, takerSig));
            QVERIFY(session.waitingForAcknowledgement());
            QVERIFY(!session.send(terms, makerSig, takerSig));
            QTRY_COMPARE(acknowledgements.size(), 1);
            QFile durableJournal(receiverState.filePath("settlement.json"));
            QVERIFY(durableJournal.open(QIODevice::ReadOnly));
            const auto journalBytes = durableJournal.readAll();
            if (attempt == 0) firstJournal = journalBytes;
            else QCOMPARE(journalBytes, firstJournal);
            QCOMPARE(QJsonDocument::fromJson(journalBytes).object().value("records").toArray().size(), 1);
            QCOMPARE(acknowledgements.at(0).at(0).toByteArray(), built.commitment);
            QVERIFY(!session.waitingForAcknowledgement());
            QTRY_VERIFY(process.canReadLine());
            const auto accepted = QJsonDocument::fromJson(process.readLine()).object();
            QCOMPARE(accepted.value("event").toString(), QString("quote-accepted"));
            QCOMPARE(accepted.value("commitment").toString().toLatin1(), built.commitment.toHex());
            QVERIFY(!accepted.value("executable").toBool(true));
            QVERIFY(!accepted.contains("terms"));
            QTRY_COMPARE(process.state(), QProcess::NotRunning);
            QCOMPARE(process.exitCode(), 0);
            QVERIFY(!process.readAllStandardError().contains("PRIVATE KEY"));
            SignedQuoteAcceptor restored(receiverState.filePath("quotes.json"), terms.protocolId,
                terms.payNetwork, terms.receiveNetwork, terms.xmzNetwork, domain);
            QVERIFY(restored.store().ready());
            QCOMPARE(restored.store().records().size(), 1);
            const auto recovered = restored.recoverMutualQuote(terms.makerKey, terms.nonce, terms.quoteExpiry + 1);
            QVERIFY(recovered.valid()); QCOMPARE(recovered.commitment(), built.commitment);
            SettlementJournal journal(receiverState.filePath("settlement.json"));
            QVERIFY(journal.ready());
            QVERIFY(journal.recoverQuote(QString::fromLatin1(built.commitment.toHex()), restored,
                terms.makerKey, terms.nonce, terms.quoteExpiry + 1).valid());
        }
    }

    void privateSessionPersistsOnlyExpectedParticipants() {
        QTemporaryDir directory;
        QProcess openssl;
        openssl.start("openssl", {"req", "-x509", "-newkey", "rsa:2048", "-nodes",
            "-keyout", directory.filePath("key.pem"), "-out", directory.filePath("cert.pem"),
            "-days", "1", "-subj", "/CN=localhost", "-addext", "subjectAltName=DNS:localhost"});
        QVERIFY(openssl.waitForFinished(10000)); QCOMPARE(openssl.exitCode(), 0);
        QFile cert(directory.filePath("cert.pem")), key(directory.filePath("key.pem"));
        QVERIFY(cert.open(QIODevice::ReadOnly)); QVERIFY(key.open(QIODevice::ReadOnly));
        auto config = QSslConfiguration::defaultConfiguration();
        config.setLocalCertificate(QSslCertificate(cert.readAll()));
        config.setPrivateKey(QSslKey(key.readAll(), QSsl::Rsa));
        config.setCaCertificates({config.localCertificate()});
        const auto pin = config.localCertificate().digest(QCryptographicHash::Sha256);
        const auto now = QDateTime::currentSecsSinceEpoch();
        auto terms = TestAcceptedQuote::create(directory.filePath("fixture.json"), now).terms();
        OfferIdentity maker, taker, other;
        terms.makerKey = maker.publicKey(); terms.takerKey = taker.publicKey();
        terms.payerKey = terms.takerKey;
        const QByteArray domain("journal-test-domain");
        const auto makerSig = maker.sign(SettlementTerms::build(terms, now).payload, domain);
        const auto takerSig = taker.sign(SignedQuoteAcceptor::takerAcceptancePayload(terms, now), domain);
        for (int scenario = 0; scenario < 7; ++scenario) {
            const bool wrongParticipant = scenario == 1;
            const auto suffix = QString::number(scenario);
            SignedQuoteAcceptor outgoing(directory.filePath(QString("out-%1.json").arg(suffix)),
                terms.protocolId, terms.payNetwork, terms.receiveNetwork, terms.xmzNetwork, domain);
            SignedQuoteAcceptor incoming(directory.filePath(QString("in-%1.json").arg(suffix)),
                terms.protocolId, terms.payNetwork, terms.receiveNetwork, terms.xmzNetwork, domain);
            PrivateQuoteServer server(config, pin);
            SettlementJournal unavailableJournal("");
            SettlementJournal outgoingJournal(directory.filePath(QString("journal-%1.json").arg(suffix)));
            QVERIFY(outgoingJournal.ready());
            int accepted = 0, rejected = 0;
            QVariantMap details;
            connect(&server, &PrivateQuoteServer::authenticated, this, [&](PrivateQuoteChannel *channel) {
                if (scenario >= 2 && scenario <= 4) {
                    connect(channel, &PrivateQuoteChannel::received, channel, [channel, scenario](const QByteArray &) {
                        if (scenario == 2) channel->send(QJsonDocument(QJsonObject{{"version", 1},
                            {"type", "quote-accepted"}, {"commitment", QString(64, '0')}}).toJson(QJsonDocument::Compact));
                        if (scenario == 3) channel->close();
                        // Scenario 4 deliberately withholds acknowledgement.
                    });
                    return;
                }
                auto *session = scenario == 5
                    ? new PrivateQuoteSession(*channel, incoming, unavailableJournal,
                        maker.publicKey(), taker.publicKey(), channel)
                    : new PrivateQuoteSession(*channel, incoming, maker.publicKey(),
                        wrongParticipant ? other.publicKey() : taker.publicKey(), channel);
                connect(session, &PrivateQuoteSession::quoteAccepted, this, [&](const QVariantMap &value) {
                    ++accepted; details = value;
                });
                connect(session, &PrivateQuoteSession::rejected, this, [&] { ++rejected; });
            });
            QVERIFY(server.start(QHostAddress::LocalHost, 0));
            PrivateQuoteChannel client(config, pin);
            PrivateQuoteSession session(client, outgoing,
                scenario == 6 ? unavailableJournal : outgoingJournal, maker.publicKey(), taker.publicKey());
            QVERIFY(!session.send(terms, makerSig, takerSig));
            QSignalSpy peerAcknowledgements(&session, &PrivateQuoteSession::peerAcknowledged);
            QSignalSpy uncertain(&session, &PrivateQuoteSession::deliveryUncertain);
            QVERIFY(client.connectToPeer("127.0.0.1", server.serverPort(), "localhost"));
            QTRY_VERIFY(client.ready());
            if (scenario == 6) {
                QVERIFY(!session.send(terms, makerSig, takerSig));
                QVERIFY(outgoing.store().contains(terms.makerKey, terms.nonce));
                QVERIFY(!incoming.store().contains(terms.makerKey, terms.nonce));
                QVERIFY(!session.waitingForAcknowledgement());
                QCOMPARE(peerAcknowledgements.size(), 0);
                server.stop();
                continue;
            }
            QVERIFY(session.send(terms, makerSig, takerSig));
            QCOMPARE(outgoingJournal.records().size(), 1);
            if (scenario >= 2) {
                QTRY_COMPARE_WITH_TIMEOUT(uncertain.size(), 1, 12000);
                QCOMPARE(peerAcknowledgements.size(), 0);
                QVERIFY(!session.waitingForAcknowledgement());
                QCOMPARE(uncertain.at(0).at(0).toByteArray(), SettlementTerms::build(terms, now).commitment);
                QVERIFY(outgoing.recoverMutualQuote(terms.makerKey, terms.nonce,
                    QDateTime::currentSecsSinceEpoch()).valid());
                QCOMPARE(incoming.store().contains(terms.makerKey, terms.nonce), scenario == 5);
                if (scenario == 5) {
                    QTRY_COMPARE(rejected, 1);
                    QCOMPARE(accepted, 0);
                }
            } else if (wrongParticipant) {
                QTRY_COMPARE(rejected, 1);
                QCOMPARE(peerAcknowledgements.size(), 0);
                QCOMPARE(accepted, 0);
                QVERIFY(!incoming.store().contains(terms.makerKey, terms.nonce));
            } else {
                QTRY_COMPARE(accepted, 1);
                QTRY_COMPARE(peerAcknowledgements.size(), 1);
                QVERIFY(details.value("mutuallyAuthorized").toBool());
                QVERIFY(!details.value("executable", true).toBool());
                QVERIFY(incoming.store().contains(terms.makerKey, terms.nonce));
                QVERIFY(incoming.recoverMutualQuote(terms.makerKey, terms.nonce, now + 1).valid());
            }
            server.stop();
        }
    }

    void standaloneServiceRejectsMissingConfiguration() {
        QProcess process;
        process.start(QCoreApplication::applicationDirPath() + "/dex_private_quote_service", QStringList{});
        QVERIFY(process.waitForFinished(5000));
        QCOMPARE(process.exitCode(), 1);
        QVERIFY(process.readAllStandardOutput().isEmpty());
    }

    void serverRequiresClientCertificateAndPin() {
        QTemporaryDir directory;
        QList<QSslCertificate> certificates;
        QList<QSslKey> keys;
        for (const auto &name : {QString("server"), QString("client")}) {
            QProcess openssl;
            openssl.start("openssl", {"req", "-x509", "-newkey", "rsa:2048", "-nodes",
                "-keyout", directory.filePath(name + ".key"), "-out", directory.filePath(name + ".pem"),
                "-days", "1", "-subj", "/CN=localhost", "-addext", "subjectAltName=DNS:localhost"});
            QVERIFY(openssl.waitForFinished(10000)); QCOMPARE(openssl.exitCode(), 0);
            QFile cert(directory.filePath(name + ".pem")), key(directory.filePath(name + ".key"));
            QVERIFY(cert.open(QIODevice::ReadOnly)); QVERIFY(key.open(QIODevice::ReadOnly));
            certificates.append(QSslCertificate(cert.readAll())); keys.append(QSslKey(key.readAll(), QSsl::Rsa));
        }
        for (int mode = 0; mode < 3; ++mode) {
            MutualTlsFixture server;
            server.configuration = QSslConfiguration::defaultConfiguration();
            server.configuration.setLocalCertificate(certificates[0]);
            server.configuration.setPrivateKey(keys[0]);
            server.configuration.setCaCertificates({certificates[1]});
            server.pin = mode == 1 ? QByteArray(32, 'x') : certificates[1].digest(QCryptographicHash::Sha256);
            QVERIFY(server.listen(QHostAddress::LocalHost, 0));
            auto config = QSslConfiguration::defaultConfiguration();
            config.setCaCertificates({certificates[0]});
            if (mode != 2) { config.setLocalCertificate(certificates[1]); config.setPrivateKey(keys[1]); }
            PrivateQuoteChannel client(config, certificates[0].digest(QCryptographicHash::Sha256));
            QSignalSpy received(&client, &PrivateQuoteChannel::received);
            QVERIFY(client.connectToPeer("127.0.0.1", server.serverPort(), "localhost"));
            QTRY_VERIFY(server.accepted);
            if (mode == 0) {
                QTRY_VERIFY(server.accepted->ready()); QTRY_VERIFY(client.ready());
                QVERIFY(client.send("mutual-private-quote"));
                QTRY_COMPARE(received.size(), 1);
                QCOMPARE(received.at(0).at(0).toByteArray(), QByteArray("mutual-private-quote"));
            } else {
                QTRY_VERIFY(server.accepted->failed());
                QVERIFY(server.plaintext.isEmpty());
                QCOMPARE(received.size(), 0);
            }
        }
    }

    void boundedListenerReleasesIncompletePeers() {
        QTemporaryDir directory;
        QProcess openssl;
        openssl.start("openssl", {"req", "-x509", "-newkey", "rsa:2048", "-nodes",
            "-keyout", directory.filePath("key.pem"), "-out", directory.filePath("cert.pem"),
            "-days", "1", "-subj", "/CN=localhost"});
        QVERIFY(openssl.waitForFinished(10000)); QCOMPARE(openssl.exitCode(), 0);
        QFile cert(directory.filePath("cert.pem")), key(directory.filePath("key.pem"));
        QVERIFY(cert.open(QIODevice::ReadOnly)); QVERIFY(key.open(QIODevice::ReadOnly));
        auto config = QSslConfiguration::defaultConfiguration();
        config.setLocalCertificate(QSslCertificate(cert.readAll()));
        config.setPrivateKey(QSslKey(key.readAll(), QSsl::Rsa));
        PrivateQuoteServer server(config, QByteArray(32, 'p'));
        QVERIFY(server.start(QHostAddress::LocalHost, 0));
        QTcpSocket first, second, third;
        first.connectToHost(QHostAddress::LocalHost, server.serverPort());
        QTRY_COMPARE(server.peerCount(), 1);
        second.connectToHost(QHostAddress::LocalHost, server.serverPort());
        QTRY_COMPARE(server.peerCount(), 2);
        third.connectToHost(QHostAddress::LocalHost, server.serverPort());
        QTRY_COMPARE(third.state(), QAbstractSocket::UnconnectedState);
        QCOMPARE(server.peerCount(), 2);
        first.abort();
        QTRY_COMPARE(server.peerCount(), 1);
        server.stop();
        QCOMPARE(server.peerCount(), 0);
        QTRY_COMPARE(second.state(), QAbstractSocket::UnconnectedState);
        QVERIFY(server.start(QHostAddress::LocalHost, 0));
        server.stop();
    }

    void validatesTrustNameAndPinBeforeSending() {
        QVERIFY(QSslSocket::supportsSsl());
        QTemporaryDir directory;
        QProcess openssl;
        openssl.start("openssl", {"req", "-x509", "-newkey", "rsa:2048", "-nodes",
            "-keyout", directory.filePath("key.pem"), "-out", directory.filePath("cert.pem"),
            "-days", "1", "-subj", "/CN=localhost", "-addext", "subjectAltName=DNS:localhost"});
        QVERIFY(openssl.waitForFinished(10000)); QCOMPARE(openssl.exitCode(), 0);
        QFile certFile(directory.filePath("cert.pem")), keyFile(directory.filePath("key.pem"));
        QVERIFY(certFile.open(QIODevice::ReadOnly)); QVERIFY(keyFile.open(QIODevice::ReadOnly));
        TlsFixture server;
        server.certificate = QSslCertificate(certFile.readAll());
        server.key = QSslKey(keyFile.readAll(), QSsl::Rsa);
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        for (int mode = 0; mode < 4; ++mode) {
            server.plaintext.clear();
            auto config = QSslConfiguration::defaultConfiguration();
            config.setCaCertificates(mode == 3 ? QList<QSslCertificate>{} : QList<QSslCertificate>{server.certificate});
            const auto pin = mode == 1 ? QByteArray(32, 'x') : server.certificate.digest(QCryptographicHash::Sha256);
            PrivateQuoteChannel channel(config, pin);
            QSignalSpy received(&channel, &PrivateQuoteChannel::received);
            QVERIFY(!channel.send("private-quote"));
            QVERIFY(channel.connectToPeer("127.0.0.1", server.serverPort(), mode == 2 ? "wrong.example" : "localhost"));
            if (mode == 0) {
                QTRY_VERIFY(channel.ready());
                QVERIFY(channel.send("private-quote"));
                QTRY_COMPARE(received.size(), 1);
                QCOMPARE(received.at(0).at(0).toByteArray(), QByteArray("private-quote"));
                // Queue a burst without processing events; the receiver must
                // stop before exposing unbounded messages to signature checks.
                for (int i = 0; i < 65; ++i) QVERIFY(channel.send("x"));
                QTRY_VERIFY(channel.failed());
                QVERIFY(received.size() <= 65);
            } else {
                QTRY_VERIFY(channel.failed());
                QVERIFY(!channel.send("private-quote"));
                QVERIFY(server.plaintext.isEmpty());
                QCOMPARE(received.size(), 0);
            }
        }
    }
};
QTEST_GUILESS_MAIN(PrivateQuoteChannelTest)
#include "test_private_quote_channel.moc"
