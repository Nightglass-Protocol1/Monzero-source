#include "../ConfirmationTracker.h"
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

class ConfirmationTest : public QObject {
    Q_OBJECT
private slots:
    void confirmsConsistentEvidenceAndPersists() {
        QTemporaryDir directory;
        const QString path = directory.filePath("confirmations.json");
        const QString tx(64, 'a'), block(64, 'b');
        {
            ConfirmationTracker tracker(path);
            QVERIFY(tracker.ready());
            auto status = tracker.observe("BTC", tx, true, "", "0", "0", "100", 3);
            QCOMPARE(status.value("state").toString(), QString("mempool"));
            status = tracker.observe("BTC", tx, true, block, "101", "1", "101", 3);
            QCOMPARE(status.value("state").toString(), QString("confirming"));
            status = tracker.observe("BTC", tx, true, block, "101", "3", "103", 3);
            QVERIFY(status.value("confirmed").toBool());
            QVERIFY(!status.value("executable").toBool());
            status = tracker.current("BTC", tx);
            QVERIFY(status.value("confirmed").toBool());
            QCOMPARE(status.value("blockHash").toString(), block);
            QVERIFY(!tracker.current("BTC", QString(64, 'f')).value("valid").toBool());
        }
        ConfirmationTracker restored(path);
        QVERIFY(restored.ready());
        QCOMPARE(restored.records().size(), 1);
        QVERIFY(restored.records().at(0).toObject().value("state") == QJsonValue("confirmed"));
    }

    void detectsRollbackReplacementAndDisappearance() {
        QTemporaryDir directory;
        const QString tx(64, 'a'), blockA(64, 'b'), blockB(64, 'c');
        ConfirmationTracker tracker(directory.filePath("confirmations.json"));
        QVERIFY(tracker.observe("XMZ", tx, true, blockA, "10", "3", "12", 3).value("confirmed").toBool());
        auto status = tracker.observe("XMZ", tx, true, blockB, "12", "1", "12", 3);
        QVERIFY(status.value("reorgDetected").toBool());
        QCOMPARE(status.value("state").toString(), QString("reorged"));
        status = tracker.observe("XMZ", tx, false, "", "0", "0", "13", 3);
        QVERIFY(status.value("reorgDetected").toBool());
        QVERIFY(!status.value("confirmed").toBool());
        status = tracker.current("XMZ", tx);
        QVERIFY(status.value("reorgDetected").toBool());
        QVERIFY(!status.value("confirmed").toBool());
        status = tracker.observe("XMZ", tx, true, blockB, "12", "3", "14", 3);
        QVERIFY(status.value("reorgDetected").toBool());
        QVERIFY(!status.value("confirmed").toBool());
    }

    void rejectsInconsistentAndCorruptEvidence() {
        QTemporaryDir directory;
        const QString path = directory.filePath("confirmations.json");
        const QString tx(64, 'a'), block(64, 'b');
        ConfirmationTracker tracker(path);
        QVERIFY(!tracker.observe("btc", tx, true, block, "10", "2", "10", 2).value("valid").toBool());
        QVERIFY(!tracker.observe("BTC", tx, true, block, "10", "2", "10", 2).value("valid").toBool());
        QVERIFY(!tracker.observe("BTC", tx, false, block, "10", "1", "10", 2).value("valid").toBool());
        QVERIFY(!tracker.observe("BTC", tx, false, "", "0", "0", "10", 2).value("valid").toBool());
        QVERIFY(tracker.observe("BTC", tx, true, "", "0", "0", "10", 2).value("valid").toBool());
        QVERIFY(!tracker.observe("BTC", tx, true, "", "0", "0", "10", 3).value("valid").toBool());
    }

    void rejectsInternallyInconsistentStore() {
        QTemporaryDir directory;
        const QString path = directory.filePath("confirmations.json");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        const QByteArray record = QByteArray("{\"version\":1,\"records\":[{")
            + "\"chain\":\"BTC\",\"transactionId\":\"" + QByteArray(64, 'a')
            + "\",\"blockHash\":\"" + QByteArray(64, 'b')
            + "\",\"blockHeight\":\"10\",\"confirmations\":\"9\",\"nodeHeight\":\"10\","
              "\"requiredConfirmations\":2,\"everReorged\":false,\"state\":\"confirmed\"}]}";
        QCOMPARE(file.write(record), qint64(record.size()));
        file.close();
        ConfirmationTracker tracker(path);
        QVERIFY(!tracker.ready());
    }

    void blocksConcurrentWriter() {
        QTemporaryDir directory;
        const QString path = directory.filePath("confirmations.json");
        ConfirmationTracker first(path), second(path);
        QVERIFY(first.ready());
        QVERIFY(!second.ready());
    }
};

QTEST_GUILESS_MAIN(ConfirmationTest)
#include "test_confirmations.moc"
