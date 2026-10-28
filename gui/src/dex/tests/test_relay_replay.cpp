#include "../RelayReplayStore.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

namespace {
RelayEnvelope::Evidence envelope(const OfferIdentity &sender, quint64 sequence,
                                 char artifact = 'a')
{
    RelayEnvelope::Terms terms;
    terms.protocol = "monzero-dex-relay-v1";
    terms.network = "isolated-btc-xmz";
    terms.type = "offer";
    terms.sequence = sequence;
    terms.createdAt = 100;
    terms.expiresAt = 3700;
    terms.artifact = QByteArray(32, artifact);
    return RelayEnvelope::create(terms, sender);
}
}

class RelayReplayTest : public QObject
{
    Q_OBJECT
private slots:
    void rejectsTamperedEnvelopeOnRestart_data()
    {
        QTest::addColumn<QString>("field");
        for (const auto &field : {"artifact", "signature", "createdAt",
                                 "sequence", "missing-envelope", "old-version",
                                 "fractional-version", "string-version",
                                 "completed-payload", "empty-pending-payload",
                                 "oversized-pending-payload"})
            QTest::newRow(field) << QString(field);
    }

    void rejectsTamperedEnvelopeOnRestart()
    {
        QFETCH(QString, field);
        QTemporaryDir directory;
        const auto path = directory.filePath("relay.json");
        OfferIdentity sender;
        {
            RelayReplayStore store(path, "monzero-dex-relay-v1", "isolated-btc-xmz");
            QVERIFY(store.accept(envelope(sender, 1), 101));
        }
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        auto root = QJsonDocument::fromJson(file.readAll()).object();
        file.close();
        auto records = root.value("records").toArray();
        auto record = records.at(0).toObject();
        auto stored = record.value("envelope").toObject();
        if (field == "artifact") stored[field] = QString(64, 'b');
        if (field == "signature") stored[field] = QString(128, '0');
        if (field == "createdAt") stored[field] = "0100";
        if (field == "sequence") record[field] = "2";
        record["envelope"] = stored;
        if (field == "missing-envelope") record.remove("envelope");
        if (field == "old-version") root["version"] = 3;
        if (field == "fractional-version") root["version"] = 4.5;
        if (field == "string-version") root["version"] = "4";
        if (field == "completed-payload")
            record["artifactPayload"] = QJsonObject{{"unexpected", true}};
        if (field == "empty-pending-payload") record["pending"] = true;
        if (field == "oversized-pending-payload") {
            record["pending"] = true;
            record["artifactPayload"] = QJsonObject{{"oversized", QString(4096, 'a')}};
        }
        records[0] = record;
        root["records"] = records;
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        const auto bytes = QJsonDocument(root).toJson();
        QCOMPARE(file.write(bytes), qint64(bytes.size()));
        file.close();
        RelayReplayStore restored(path, "monzero-dex-relay-v1", "isolated-btc-xmz");
        QVERIFY(!restored.ready());
        QVERIFY(!restored.pendingEnvelope(sender.publicKey()).accepted());
    }

    void acceptsStrictSequenceAndExactRetry()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath("relay.json");
        OfferIdentity sender;
        const auto first = envelope(sender, 1);
        const auto second = envelope(sender, 2, 'b');
        {
            RelayReplayStore store(path, "monzero-dex-relay-v1",
                                   "isolated-btc-xmz");
            QVERIFY(store.ready());
            QVERIFY(store.accept(first, 101));
            QVERIFY(store.accept(first, 102));
            QVERIFY(store.accept(second, 103));
            QCOMPARE(store.lastSequence(sender.publicKey()), quint64(2));
        }
        {
            RelayReplayStore restored(path, "monzero-dex-relay-v1",
                                      "isolated-btc-xmz");
            QVERIFY(restored.ready());
            QVERIFY(restored.accept(second, 104));
            QCOMPARE(restored.records().size(), 1);
            QCOMPARE(QFileInfo(path).permissions()
                         & (QFileDevice::ReadGroup | QFileDevice::WriteGroup
                            | QFileDevice::ReadOther | QFileDevice::WriteOther),
                     QFileDevice::Permissions{});
        }

        RelayReplayStore wrongDeployment(path, "monzero-dex-relay-v1",
                                         "other-network");
        QVERIFY(!wrongDeployment.ready());
    }

    void rejectsSkippedStaleAndConflictingSequences()
    {
        QTemporaryDir directory;
        RelayReplayStore store(directory.filePath("relay.json"),
                               "monzero-dex-relay-v1", "isolated-btc-xmz");
        OfferIdentity sender, other;
        QVERIFY(!store.accept(envelope(sender, 2), 101));
        QVERIFY(store.accept(envelope(sender, 1), 101));
        QVERIFY(!store.accept(envelope(sender, 1, 'b'), 102));
        QVERIFY(!store.accept(envelope(sender, 3), 102));
        QVERIFY(store.accept(envelope(sender, 2), 102));
        QVERIFY(!store.accept(envelope(sender, 1), 103));
        QVERIFY(store.accept(envelope(other, 1), 103));
        QCOMPARE(store.records().size(), 2);
    }

    void reauthenticatesAndChecksDeploymentAndLifetime()
    {
        QTemporaryDir directory;
        OfferIdentity sender;
        const auto first = envelope(sender, 1);
        RelayReplayStore wrongNetwork(directory.filePath("wrong.json"),
            "monzero-dex-relay-v1", "other-network");
        QVERIFY(!wrongNetwork.accept(first, 101));
        RelayReplayStore store(directory.filePath("relay.json"),
            "monzero-dex-relay-v1", "isolated-btc-xmz");
        QVERIFY(!store.accept(first, 99));
        QVERIFY(!store.accept(first, 3700));
        QVERIFY(store.accept(first, 101));
        RelayEnvelope::Evidence empty;
        QVERIFY(!store.accept(empty, 101));
    }

    void rejectsConcurrentWriterAndCorruption()
    {
        QTemporaryDir directory;
        const QString path = directory.filePath("relay.json");
        {
            RelayReplayStore first(path, "monzero-dex-relay-v1",
                                   "isolated-btc-xmz");
            QVERIFY(first.ready());
            RelayReplayStore second(path, "monzero-dex-relay-v1",
                                    "isolated-btc-xmz");
            QVERIFY(!second.ready());
        }
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("{broken"), qint64(7));
        file.close();
        RelayReplayStore corrupt(path, "monzero-dex-relay-v1",
                                 "isolated-btc-xmz");
        QVERIFY(!corrupt.ready());
    }
};

QTEST_GUILESS_MAIN(RelayReplayTest)
#include "test_relay_replay.moc"
