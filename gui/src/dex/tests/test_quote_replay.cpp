#include "../QuoteReplayStore.h"
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QtTest>

class QuoteReplayTest : public QObject {
    Q_OBJECT
private slots:
    void rejectsSharedAndSymlinkStorage()
    {
#ifndef Q_OS_UNIX
        QSKIP("Unix filesystem permissions");
#endif
        QTemporaryDir directory;
        const auto path = directory.filePath("quotes.json");
        {
            QuoteReplayStore store(path);
            QVERIFY(store.accept(QByteArray(32, 'm'), QByteArray(32, 'n'), QByteArray(32, 'c'), 200, 100));
        }
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto original = file.readAll(); file.close();
        const auto link = directory.filePath("link.json");
        QVERIFY(QFile::link(path, link));
        QuoteReplayStore linked(link);
        QVERIFY(!linked.ready());
        QVERIFY(!QFile::exists(link + ".lock"));
        QVERIFY(file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ReadOther));
        QuoteReplayStore shared(path);
        QVERIFY(!shared.ready());
        QVERIFY(file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner));
        const auto permissions = QFileInfo(directory.path()).permissions();
        QVERIFY(QFile::setPermissions(directory.path(), permissions | QFileDevice::ReadOther));
        QuoteReplayStore unsafe(directory.filePath("new.json"));
        QVERIFY(!unsafe.ready());
        QVERIFY(!QFile::exists(directory.filePath("new.json.lock")));
        QVERIFY(QFile::setPermissions(directory.path(), permissions));
        QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), original);
    }

    void rejectsReplayAndPersists() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath("quotes.json");
        const QByteArray maker(32, 'm'), nonce(32, 'n');
        const QByteArray commitmentA(32, 'a'), commitmentB(32, 'b');
        {
            QuoteReplayStore store(path);
            QVERIFY(store.ready());
            QVERIFY(store.accept(maker, nonce, commitmentA, 200, 100));
            QVERIFY(store.accept(maker, nonce, commitmentA, 200, 101));
            QVERIFY(!store.accept(maker, nonce, commitmentB, 200, 101));
            QVERIFY(!store.accept(maker, nonce, commitmentA, 201, 101));
            QVERIFY(store.contains(maker, nonce));
        }
        QuoteReplayStore restored(path);
        QVERIFY(restored.ready());
        QVERIFY(restored.contains(maker, nonce));
        QVERIFY(!restored.accept(maker, nonce, commitmentB, 200, 102));
        QCOMPARE(restored.records().size(), 1);
        QCOMPARE(QFileInfo(path).permissions() & (QFileDevice::ReadGroup | QFileDevice::WriteGroup
            | QFileDevice::ReadOther | QFileDevice::WriteOther), QFileDevice::Permissions{});
    }

    void validatesInputsAndConcurrency() {
        QTemporaryDir directory;
        const QString path = directory.filePath("quotes.json");
        QuoteReplayStore first(path);
        QVERIFY(first.ready());
        const QByteArray maker(32, 'm'), nonce(32, 'n'), commitment(32, 'c');
        QVERIFY(!first.accept(QByteArray(31, 'm'), nonce, commitment, 200, 100));
        QVERIFY(!first.accept(maker, QByteArray(31, 'n'), commitment, 200, 100));
        QVERIFY(!first.accept(maker, nonce, QByteArray(31, 'c'), 200, 100));
        QVERIFY(!first.accept(maker, nonce, commitment, 100, 100));
        QVERIFY(!first.accept(maker, nonce, commitment, 100 + 7 * 24 * 60 * 60 + 1, 100));
        QuoteReplayStore second(path);
        QVERIFY(!second.ready());
    }

    void failsClosedOnCorruption() {
        QTemporaryDir directory;
        const QString path = directory.filePath("quotes.json");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("{broken"), qint64(7));
        file.close();
        QuoteReplayStore store(path);
        QVERIFY(!store.ready());
    }
};

QTEST_GUILESS_MAIN(QuoteReplayTest)
#include "test_quote_replay.moc"
