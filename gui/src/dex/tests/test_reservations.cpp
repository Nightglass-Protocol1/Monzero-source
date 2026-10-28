#include "../BalanceReservationStore.h"
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QCoreApplication>
#include <QProcess>
#include <QtTest>

class ReservationTest : public QObject {
    Q_OBJECT
private slots:
    void preventsOverspendAndPersists() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath("reservations.json");
        {
            BalanceReservationStore store(path);
            QVERIFY(store.ready());
            QVERIFY(store.reserve("offer-a", "60", "100"));
            QVERIFY(store.reserve("offer-a", "60", "100"));
            QVERIFY(!store.reserve("offer-a", "61", "100"));
            QVERIFY(!store.reserve("offer-b", "41", "100"));
            QVERIFY(store.reserve("offer-b", "40", "100"));
            QCOMPARE(store.heldAtomic(), QString("100"));
            QVERIFY(store.commit("offer-a"));
            QVERIFY(!store.release("offer-a"));
            QVERIFY(store.release("offer-b"));
            QCOMPARE(store.heldAtomic(), QString("60"));
        }
        BalanceReservationStore restored(path);
        QVERIFY(restored.ready());
        QCOMPARE(restored.heldAtomic(), QString("60"));
        QVERIFY(!restored.finalize("offer-a", "settled", "bad"));
        QVERIFY(restored.finalize("offer-a", "settled", QString(64, 'a')));
        QVERIFY(restored.finalize("offer-a", "settled", QString(64, 'a')));
        QVERIFY(!restored.finalize("offer-a", "refunded", QString(64, 'b')));
        QCOMPARE(restored.heldAtomic(), QString("0"));
        QCOMPARE(QFileInfo(path).permissions() & (QFileDevice::ReadGroup | QFileDevice::WriteGroup
            | QFileDevice::ReadOther | QFileDevice::WriteOther), QFileDevice::Permissions{});
    }
    void rejectsInvalidAndConcurrentAccess() {
        QTemporaryDir directory;
        const QString path = directory.filePath("reservations.json");
        BalanceReservationStore first(path);
        QVERIFY(first.ready());
        QVERIFY(!first.reserve("../bad", "1", "10"));
        QVERIFY(!first.reserve("good", "01", "10"));
        QVERIFY(!first.reserve("good", "0", "10"));
        QVERIFY(!first.reserve("good", "18446744073709551616", "10"));
        BalanceReservationStore second(path);
        QVERIFY(!second.ready());
    }
    void failsClosedOnCorruption() {
        QTemporaryDir directory;
        const QString path = directory.filePath("reservations.json");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("{broken"), qint64(7));
        file.close();
        BalanceReservationStore store(path);
        QVERIFY(!store.ready());
        QVERIFY(!store.reserve("offer", "1", "10"));
    }
    void survivesAbruptProcessLoss() {
        QTemporaryDir directory;
        const QString path = directory.filePath("reservations.json");
        const QString helper = QCoreApplication::applicationDirPath() + "/reservation_crash_writer";
        QProcess process;
        process.start(helper, {path});
        QVERIFY(process.waitForFinished(5000));
        QCOMPARE(process.exitCode(), 0);
        BalanceReservationStore restored(path);
        QVERIFY(restored.ready());
        QCOMPARE(restored.heldAtomic(), QString("25"));
    }
};
QTEST_GUILESS_MAIN(ReservationTest)
#include "test_reservations.moc"
