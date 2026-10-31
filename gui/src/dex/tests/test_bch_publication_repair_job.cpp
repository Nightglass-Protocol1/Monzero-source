#include "BchPublicationRepairTestFixture.h"
#include "../BchPublicationRepairJob.h"
#include <QPointer>
class BchPublicationRepairJobTests:public QObject {
    Q_OBJECT
    PublicationRepairFixture c;
private slots:
    void init() {QVERIFY(c.createRepair());}
    void explicitReviewAndRepair() {
        BchPublicationRepairJob job;QVERIFY(job.configure(c.wrapper));QVERIFY(!job.submit(QString(64,'0')));
        QVERIFY(job.review("prepared"));QVERIFY(!job.review("signed"));
        QTRY_COMPARE(job.status().value("state").toString(),QString("reviewed"));
        const auto approval=job.status().value("review").toMap().value("commitment").toString();
        QVERIFY(job.submit(approval));QTRY_COMPARE(job.status().value("state").toString(),QString("repaired"));
        QVERIFY(!job.status().value("submitAvailable").toBool());QVERIFY(!job.status().value("chainObserved").toBool());
        QVERIFY(!job.submit(approval));QCOMPARE(read(c.log),QByteArray("review\nsubmit\n"));
        BchPublicationRepairJob reopened;QVERIFY(reopened.configure(c.wrapper));QVERIFY(!reopened.submit(approval));
        QVERIFY(reopened.review("signed"));QTRY_COMPARE(reopened.status().value("state").toString(),QString("reviewed"));
    }
    void wrongApprovalNeverStartsHelper() {
        BchPublicationRepairJob job;QVERIFY(job.configure(c.wrapper));QVERIFY(job.review("prepared"));
        QTRY_COMPARE(job.status().value("state").toString(),QString("reviewed"));
        QVERIFY(!job.submit(QString(64,'0')));QCOMPARE(read(c.log),QByteArray("review\n"));
        QCOMPARE(job.status().value("state").toString(),QString("unknown"));QVERIFY(job.status().value("review").toMap().isEmpty());
    }
    void sourceChangeInvalidatesConsent() {
        BchPublicationRepairJob job;QVERIFY(job.configure(c.wrapper));QVERIFY(job.review("prepared"));
        QTRY_COMPARE(job.status().value("state").toString(),QString("reviewed"));
        const auto approval=job.status().value("review").toMap().value("commitment").toString();
        QVERIFY(save(c.rpc,"{}"));QVERIFY(!job.submit(approval));QCOMPARE(read(c.log),QByteArray("review\n"));
        QVERIFY(!job.status().value("reviewAvailable").toBool());
    }
    void lostReplyRequiresFreshReview() {
        BchPublicationRepairJob job;QVERIFY(job.configure(c.wrapper));QVERIFY(job.review("prepared"));
        QTRY_COMPARE(job.status().value("state").toString(),QString("reviewed"));
        const auto approval=job.status().value("review").toMap().value("commitment").toString();
        QVERIFY(save(c.mode,"lost"));QVERIFY(job.submit(approval));QTRY_COMPARE(job.status().value("state").toString(),QString("unknown"));
        QVERIFY(job.status().value("attempted").toBool());QVERIFY(!job.submit(approval));
        QCOMPARE(read(c.log),QByteArray("review\nsubmit\n"));QVERIFY(job.status().value("review").toMap().isEmpty());
    }
    void cancellationWaitsForProcessExit() {
        QVERIFY(save(c.mode,"slow"));BchPublicationRepairJob job;QVERIFY(job.configure(c.wrapper));
        QSignalSpy changed(&job,&BchPublicationRepairJob::changed);
        QVERIFY(job.review("prepared"));QTRY_VERIFY(!read(c.log).isEmpty());
        job.invalidate();QVERIFY(!job.status().value("reviewAvailable").toBool());
        const auto notifications=changed.count();
        QTRY_VERIFY(job.status().value("reviewAvailable").toBool());QTRY_VERIFY(changed.count()>notifications);
        QCOMPARE(job.status().value("state").toString(),QString("unknown"));
    }
    void repeatedInvalidationDoesNotScheduleMoreWork() {
        BchPublicationRepairJob job;QSignalSpy changed(&job,&BchPublicationRepairJob::changed);
        job.invalidate();QCOMPARE(changed.count(),0);
        QVERIFY(job.configure(c.wrapper));job.invalidate();changed.clear();
        job.invalidate();job.invalidate();QTest::qWait(1);QCOMPARE(changed.count(),0);
    }
    void consumerCanDeleteJobOnCompletion() {
        QPointer<BchPublicationRepairJob> job=new BchPublicationRepairJob;
        QVERIFY(job->configure(c.wrapper));
        connect(job.data(),&BchPublicationRepairJob::changed,this,[&job]{
            if (job && job->status().value("state").toString()=="reviewed") delete job.data();
        });
        QVERIFY(job->review("prepared"));QTRY_VERIFY(job.isNull());
    }
    void rejectsBadResponse_data() {QTest::addColumn<QByteArray>("mode");for (const auto mode:{"private","request","chain"}) QTest::newRow(mode)<<QByteArray(mode);}
    void rejectsBadResponse() {
        QFETCH(QByteArray,mode);QVERIFY(save(c.mode,mode));BchPublicationRepairJob job;QVERIFY(job.configure(c.wrapper));
        QVERIFY(job.review("prepared"));QTRY_COMPARE(job.status().value("state").toString(),QString("unknown"));
        QVERIFY(job.status().value("review").toMap().isEmpty());
        QVERIFY(!QJsonDocument::fromVariant(job.status()).toJson().contains("SECRET-MUST-NOT-BE-EXPOSED"));
    }
};
QTEST_GUILESS_MAIN(BchPublicationRepairJobTests)
#include "test_bch_publication_repair_job.moc"
