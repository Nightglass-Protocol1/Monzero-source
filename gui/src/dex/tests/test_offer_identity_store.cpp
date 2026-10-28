#include "../OfferIdentityStore.h"
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

class OfferIdentityStoreTest : public QObject
{
    Q_OBJECT
private slots:
    void rejectsSharedDirectoryAndFilePermissions()
    {
        QTemporaryDir directory;
        const auto path = directory.filePath("identity.bin");
        const QByteArray password("test-only-passphrase");
        const auto privateDirectory = QFileDevice::ReadOwner | QFileDevice::WriteOwner
            | QFileDevice::ExeOwner;
        QVERIFY(QFile::setPermissions(directory.path(), privateDirectory | QFileDevice::ReadGroup));
        {
            OfferIdentityStore shared(path, password, OfferIdentityStore::Mode::CreateNew);
            QVERIFY(!shared.ready());
            QVERIFY(!QFile::exists(path));
        }
        QVERIFY(QFile::setPermissions(directory.path(), privateDirectory));
        {
            OfferIdentityStore created(path, password, OfferIdentityStore::Mode::CreateNew);
            QVERIFY(created.ready());
        }
        QVERIFY(QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner
            | QFileDevice::ReadGroup));
        OfferIdentityStore shared(path, password, OfferIdentityStore::Mode::OpenExisting);
        QVERIFY(!shared.ready());
        QVERIFY(QFile::exists(path));
    }

    void restoresIdentityAndNeverReplacesExistingFile()
    {
        QTemporaryDir directory;
        const auto path = directory.filePath("identity.bin");
        const QByteArray password("test-only-strong-passphrase");
        QByteArray publicKey, signature, original;
        {
            OfferIdentityStore missing(path, password, OfferIdentityStore::Mode::OpenExisting);
            QVERIFY(!missing.ready());
            QVERIFY(!QFile::exists(path));
        }
        {
            OfferIdentityStore store(path, password, OfferIdentityStore::Mode::CreateNew);
            QVERIFY2(store.ready(), qPrintable(store.error()));
            publicKey = store.identity()->publicKey();
            signature = store.identity()->sign("offer", "isolated-test");
            OfferIdentityStore concurrent(path, password, OfferIdentityStore::Mode::OpenExisting);
            QVERIFY(!concurrent.ready());
        }
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        original = file.readAll();
        file.close();
        QVERIFY(!(file.permissions() & (QFileDevice::ReadGroup | QFileDevice::ReadOther)));
        {
            OfferIdentityStore existing(path, password, OfferIdentityStore::Mode::CreateNew);
            QVERIFY(!existing.ready());
        }
        {
            OfferIdentityStore wrong(path, "wrong", OfferIdentityStore::Mode::OpenExisting);
            QVERIFY(!wrong.ready());
        }
        {
            OfferIdentityStore restored(path, password, OfferIdentityStore::Mode::OpenExisting);
            QVERIFY(restored.ready());
            QCOMPARE(restored.identity()->publicKey(), publicKey);
            QCOMPARE(restored.identity()->sign("offer", "isolated-test"), signature);
        }
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), original);
    }

    void rejectsCorruptionAndSymlinks()
    {
        QTemporaryDir directory;
        const auto path = directory.filePath("broken.bin");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QVERIFY(file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner));
        QCOMPARE(file.write("broken"), qint64(6));
        file.close();
        {
            OfferIdentityStore broken(path, "password", OfferIdentityStore::Mode::OpenExisting);
            QVERIFY(!broken.ready());
        }
        const auto link = directory.filePath("link.bin");
        QVERIFY(QFile::link(path, link));
        OfferIdentityStore linked(link, "password", OfferIdentityStore::Mode::OpenExisting);
        QVERIFY(!linked.ready());
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QByteArray("broken"));
    }
};
QTEST_GUILESS_MAIN(OfferIdentityStoreTest)
#include "test_offer_identity_store.moc"
