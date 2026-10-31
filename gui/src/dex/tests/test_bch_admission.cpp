#include "BchAdmissionTestFixture.h"
class BchAdmissionTests: public QObject {
    Q_OBJECT
private slots:
    void explicitReviewSubmitAndRestart() {
        Fixture f;QVERIFY(f.create());BchAdmissionJob job;
        QVERIFY(job.configure(f.path));QVERIFY(!job.configure(f.path));
        QVERIFY(!job.submit(QString(64,'0')));QVERIFY(job.review());QVERIFY(!job.review());
        QTRY_VERIFY_WITH_TIMEOUT(!job.status().value("busy").toBool(),10000);
        QCOMPARE(job.status().value("state").toString(),QString("reviewed"));
        const auto digest=job.status().value("review").toMap().value("commitment").toString();
        QVERIFY(!job.submit(QString(64,'0')));QVERIFY(job.submit(digest));
        QTRY_VERIFY_WITH_TIMEOUT(!job.status().value("busy").toBool(),10000);
        QCOMPARE(job.status().value("state").toString(),QString("admitted"));
        QVERIFY(!job.submit(digest));QVERIFY(!job.review());QVERIFY(!job.status().value("executable").toBool());
        BchAdmissionJob resumed;QVERIFY(resumed.configure(f.path));QVERIFY(resumed.refresh());
        QTRY_VERIFY_WITH_TIMEOUT(!resumed.status().value("busy").toBool(),10000);
        QCOMPARE(resumed.status().value("state").toString(),QString("admitted"));
        QVERIFY(!resumed.status().value("submitAvailable").toBool());
        QCOMPARE(read(f.log),QByteArray("review\nsubmit\nstatus\n"));
    }
    void invalidResponses_data() {
        QTest::addColumn<QString>("mode");QTest::addColumn<bool>("status");
        for (const auto mode:{"request","trade","terms","digest","extra","execute","flag","stderr","oversized","newline","replace"})
            QTest::newRow(mode)<<QString(mode)<<false;
        QTest::newRow("future status")<<QString("future")<<true;
        QTest::newRow("early status")<<QString("early")<<true;
    }
    void invalidResponses() {
        QFETCH(QString,mode);QFETCH(bool,status);Fixture f;QVERIFY(f.create());
        QVERIFY(save(f.mode,mode.toUtf8()));BchAdmissionJob job;QVERIFY(job.configure(f.path));
        QVERIFY(status?job.refresh():job.review());
        QTRY_VERIFY_WITH_TIMEOUT(!job.status().value("busy").toBool(),10000);
        QCOMPARE(job.status().value("state").toString(),QString("unknown"));
        QVERIFY(job.status().value("review").toMap().isEmpty());
        QVERIFY(job.status().value("admission").toMap().isEmpty());
        QVERIFY(!job.status().value("submitAvailable").toBool());
    }
    void lostReplyNeedsStatusAndCannotResubmit() {
        Fixture f;QVERIFY(f.create());BchAdmissionJob job;QVERIFY(job.configure(f.path));QVERIFY(job.review());
        QTRY_VERIFY_WITH_TIMEOUT(!job.status().value("busy").toBool(),10000);
        const auto digest=job.status().value("review").toMap().value("commitment").toString();
        QVERIFY(!digest.isEmpty());QVERIFY(save(f.mode,"lost"));QVERIFY(job.submit(digest));
        QTRY_VERIFY_WITH_TIMEOUT(!job.status().value("busy").toBool(),10000);
        QCOMPARE(job.status().value("state").toString(),QString("unknown"));
        QVERIFY(!job.submit(digest));QVERIFY(!job.review());QVERIFY(job.status().value("submitAttempted").toBool());
        QVERIFY(save(f.mode,"normal"));QVERIFY(job.refresh());
        QTRY_VERIFY_WITH_TIMEOUT(!job.status().value("busy").toBool(),10000);
        QCOMPARE(job.status().value("state").toString(),QString("admitted"));
        QCOMPARE(read(f.log),QByteArray("review\nsubmit\nstatus\n"));
    }
    void changedConfigurationInvalidatesConsent() {
        Fixture f;QVERIFY(f.create());BchAdmissionJob job;QVERIFY(job.configure(f.path));QVERIFY(job.review());
        QTRY_VERIFY_WITH_TIMEOUT(!job.status().value("busy").toBool(),10000);
        const auto digest=job.status().value("review").toMap().value("commitment").toString();
        f.config["role"]="bch-funder";QVERIFY(f.writeConfig());QVERIFY(!job.submit(digest));
        QCOMPARE(job.status().value("state").toString(),QString("unknown"));QCOMPARE(read(f.log),QByteArray("review\n"));
    }
    void unsafeConfigurationRejected() {
        Fixture f;QVERIFY(f.create());
        QVERIFY(QFile::setPermissions(f.path,QFile::ReadOwner|QFile::WriteOwner|QFile::ReadOther));
        BchAdmissionJob permissions;QVERIFY(!permissions.configure(f.path));QVERIFY(f.writeConfig());
        auto policy=f.config.value("policy").toObject();policy["maximum_xmz_atoms"]=9007199254740992.0;
        f.config["policy"]=policy;QVERIFY(f.writeConfig());BchAdmissionJob imprecise;QVERIFY(!imprecise.configure(f.path));
    }
    void realHelperReview() {
        const auto path=qEnvironmentVariable("MONZERO_BCH_QT_FIXTURE");
        if (path.isEmpty()) QSKIP("Requires the independent signed-quote subprocess fixture");
        BchAdmissionJob job;QVERIFY(job.configure(path));QVERIFY(job.review());
        QTRY_VERIFY_WITH_TIMEOUT(!job.status().value("busy").toBool(),10000);
        QCOMPARE(job.status().value("state").toString(),QString("reviewed"));
        const auto digest=job.status().value("review").toMap().value("commitment").toString();
        QCOMPARE(digest.size(),64);QVERIFY(job.submit(digest));
        QTRY_VERIFY_WITH_TIMEOUT(!job.status().value("busy").toBool(),10000);
        QCOMPARE(job.status().value("state").toString(),QString("unknown"));
        QVERIFY(!job.submit(digest));QVERIFY(!job.review());
    }
};
QTEST_GUILESS_MAIN(BchAdmissionTests)
#include "test_bch_admission.moc"
