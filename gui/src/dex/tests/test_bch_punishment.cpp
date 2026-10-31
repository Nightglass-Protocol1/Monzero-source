#include "../BchPunishmentJob.h"
#include "BchPunishmentWorkerTestFixture.h"
class BchPunishmentTests:public QObject {
    Q_OBJECT
private slots:
    void offlineReviewAndHistory() {PunishmentWorkerContext c;QVERIFY(c.create());BchPunishmentJob job;QVERIFY(job.configure(c.wrapper));
        QVERIFY(job.review());QTRY_VERIFY(!job.status().value("busy").toBool());QCOMPARE(job.status().value("state").toString(),QString("reviewed"));
        QVERIFY(!job.status().value("submitAvailable").toBool());QVERIFY(!job.status().value("chainObserved").toBool());
        for (const auto state:{"attempted","acknowledged"}) {QVERIFY(save(c.mode,state));QVERIFY(job.refresh());
            QTRY_VERIFY(!job.status().value("busy").toBool());QCOMPARE(job.status().value("state").toString(),QString(state));}
    }
    void rejectsResponse_data() {QTest::addColumn<QString>("mode");
        for (const auto mode:{"request","trade","chain","execute","observation","fee","stderr","replace","oversized","newline"}) QTest::newRow(mode)<<QString(mode);}
    void rejectsResponse() {QFETCH(QString,mode);PunishmentWorkerContext c;QVERIFY(c.create());BchPunishmentJob job;QVERIFY(job.configure(c.wrapper));
        QVERIFY(save(c.mode,mode.toUtf8()));QVERIFY(job.review());QTRY_VERIFY(!job.status().value("busy").toBool());
        QCOMPARE(job.status().value("state").toString(),QString("unknown"));QVERIFY(job.status().value("review").toMap().isEmpty());}
    void changedSourceBeforeRequest() {PunishmentWorkerContext c;QVERIFY(c.create());BchPunishmentJob job;QVERIFY(job.configure(c.wrapper));
        QVERIFY(save(c.path,read(c.path)+" "));QVERIFY(!job.refresh());QVERIFY(!job.status().value("checkAvailable").toBool());}
    void retainedReviewAndHistory_data() {QTest::addColumn<QString>("mode");
        QTest::newRow("changed-review")<<QString("changed");QTest::newRow("history-regression")<<QString("normal");}
    void retainedReviewAndHistory() {QFETCH(QString,mode);PunishmentWorkerContext c;QVERIFY(c.create());BchPunishmentJob job;
        QVERIFY(job.configure(c.wrapper));QVERIFY(save(c.mode,"acknowledged"));QVERIFY(job.refresh());
        QTRY_VERIFY(!job.status().value("busy").toBool());QCOMPARE(job.status().value("state").toString(),QString("acknowledged"));
        QVERIFY(save(c.mode,mode.toUtf8()));QVERIFY(job.refresh());QTRY_VERIFY(!job.status().value("busy").toBool());
        QCOMPARE(job.status().value("state").toString(),QString("unknown"));
        QVERIFY(save(c.mode,"acknowledged"));QVERIFY(job.refresh());QTRY_VERIFY(!job.status().value("busy").toBool());
        QCOMPARE(job.status().value("state").toString(),QString("acknowledged"));}
    void confirmationIsSeparateFromHistory() {PunishmentWorkerContext c;QVERIFY(c.create());QVERIFY(c.observationFiles());
        BchPunishmentJob job;QVERIFY(job.configure(c.wrapper));QVERIFY(job.configureObservation(c.observationPath));
        QVERIFY(job.observe());QTRY_VERIFY(!job.status().value("busy").toBool());QVERIFY(job.status().value("chainObserved").toBool());
        QCOMPARE(job.status().value("state").toString(),QString("attempted"));
        QVERIFY(save(c.mode,"attempted"));QVERIFY(job.refresh());QVERIFY(!job.status().value("chainObserved").toBool());
        QTRY_VERIFY(!job.status().value("busy").toBool());QCOMPARE(job.status().value("state").toString(),QString("attempted"));
        QVERIFY(QFile::remove(c.rpcPath));QVERIFY(job.refresh());QTRY_VERIFY(!job.status().value("busy").toBool());
        QCOMPARE(job.status().value("state").toString(),QString("attempted"));QVERIFY(!job.observe());}
    void invalidConfirmation_data() {QTest::addColumn<QString>("mode");
        for (const auto mode:{"stale","wrongtx","shallow","unsent","rpc-replace"}) QTest::newRow(mode)<<QString(mode);}
    void invalidConfirmation() {QFETCH(QString,mode);PunishmentWorkerContext c;QVERIFY(c.create());QVERIFY(c.observationFiles());
        BchPunishmentJob job;QVERIFY(job.configure(c.wrapper));QVERIFY(job.configureObservation(c.observationPath));
        QVERIFY(save(c.mode,mode.toUtf8()));QVERIFY(job.observe());QTRY_VERIFY(!job.status().value("busy").toBool());
        QCOMPARE(job.status().value("state").toString(),QString("unknown"));QVERIFY(!job.status().value("chainObserved").toBool());}
    void failedObservationRetainsHistoryOnly() {PunishmentWorkerContext c;QVERIFY(c.create());QVERIFY(c.observationFiles());
        BchPunishmentJob job;QVERIFY(job.configure(c.wrapper));QVERIFY(job.configureObservation(c.observationPath));
        QVERIFY(job.observe());QTRY_VERIFY(!job.status().value("busy").toBool());QVERIFY(job.status().value("chainObserved").toBool());
        QVERIFY(save(c.mode,"wrongtx"));QVERIFY(job.observe());QVERIFY(!job.status().value("chainObserved").toBool());
        QTRY_VERIFY(!job.status().value("busy").toBool());QCOMPARE(job.status().value("state").toString(),QString("unknown"));
        QCOMPARE(job.status().value("savedRelayState").toString(),QString("attempted"));
        QVERIFY(!job.status().value("savedReview").toMap().isEmpty());QVERIFY(job.status().value("result").toMap().isEmpty());}
    void changedObservationDeploymentRejected() {PunishmentWorkerContext c;QVERIFY(c.create());QVERIFY(c.observationFiles());
        BchPunishmentJob job;QVERIFY(job.configure(c.wrapper));
        auto rpc=QJsonDocument::fromJson(read(c.rpcPath)).object();rpc["port"]=18444;
        QVERIFY(save(c.rpcPath,QJsonDocument(rpc).toJson(QJsonDocument::Compact)));
        QVERIFY(!job.configureObservation(c.observationPath));QVERIFY(!job.observe());QVERIFY(job.review());
        QTRY_VERIFY(!job.status().value("busy").toBool());QCOMPARE(job.status().value("state").toString(),QString("reviewed"));}
    void approvedSubmissionIsOneShot_data() {QTest::addColumn<QString>("mode");
        for (const auto mode:{"normal","lost","changed","stderr","rpc-replace"}) QTest::newRow(mode)<<QString(mode);}
    void approvedSubmissionIsOneShot() {QFETCH(QString,mode);PunishmentWorkerContext c;QVERIFY(c.create());QVERIFY(c.submissionFiles());
        BchPunishmentJob job;QVERIFY(job.configure(c.wrapper));QVERIFY(job.configureObservation(c.observationPath));
        QVERIFY(job.configureSubmission(c.submissionPath));QVERIFY(!job.submit(QString(64,'0')));QVERIFY(job.review());
        QTRY_VERIFY(!job.status().value("busy").toBool());QVERIFY(job.status().value("submitAvailable").toBool());
        const auto approval=job.status().value("review").toMap().value("commitment").toString();
        QVERIFY(!job.submit(QString(64,'0')));QVERIFY(save(c.mode,mode.toUtf8()));QVERIFY(job.submit(approval));
        QVERIFY(!job.submit(approval));QTRY_VERIFY(!job.status().value("busy").toBool());
        QCOMPARE(job.status().value("state").toString(),mode=="normal"?QString("acknowledged"):QString("unknown"));
        QVERIFY(job.status().value("submitAttempted").toBool());QVERIFY(!job.status().value("submitAvailable").toBool());
        QVERIFY(!job.status().value("chainObserved").toBool());QVERIFY(!job.submit(approval));
        QCOMPARE(read(c.log).count("--punish-submit-stdio-v1"),1);
        if (mode!="rpc-replace") {
            QVERIFY(save(c.mode,mode=="normal"?"acknowledged":"attempted"));
            BchPunishmentJob reopened;QVERIFY(reopened.configure(c.wrapper));QVERIFY(reopened.configureObservation(c.observationPath));
            QVERIFY(reopened.configureSubmission(c.submissionPath));QVERIFY(reopened.review());
            QTRY_VERIFY(!reopened.status().value("busy").toBool());QVERIFY(!reopened.status().value("submitAvailable").toBool());
            QVERIFY(!reopened.submit(approval));
        }
        QVERIFY(save(c.mode,mode=="normal"?"acknowledged":"attempted"));QVERIFY(job.refresh());QTRY_VERIFY(!job.status().value("busy").toBool());
        QVERIFY(!job.status().value("submitAvailable").toBool());
    }
    void terminatedSubmitNotifiesAvailability() {PunishmentWorkerContext c;QVERIFY(c.create());QVERIFY(c.submissionFiles());
        BchPunishmentJob job;QVERIFY(job.configure(c.wrapper));QVERIFY(job.configureObservation(c.observationPath));
        QVERIFY(job.configureSubmission(c.submissionPath));QVERIFY(job.review());QTRY_VERIFY(!job.status().value("busy").toBool());
        bool checkAvailable=false;connect(&job,&BchPunishmentJob::changed,this,[&]{checkAvailable=job.status().value("checkAvailable").toBool();});
        QVERIFY(save(c.mode,"overflow-hang"));QVERIFY(job.submit(job.status().value("review").toMap().value("commitment").toString()));
        QTRY_VERIFY(checkAvailable);QCOMPARE(job.status().value("state").toString(),QString("unknown"));
        QVERIFY(job.status().value("submitAttempted").toBool());QVERIFY(!job.status().value("submitAvailable").toBool());
        QCOMPARE(read(c.log).count("--punish-submit-stdio-v1"),1);}
    void changedSubmissionSourceNeedsReopen() {PunishmentWorkerContext c;QVERIFY(c.create());QVERIFY(c.submissionFiles());
        BchPunishmentJob job;QVERIFY(job.configure(c.wrapper));QVERIFY(job.configureObservation(c.observationPath));
        QVERIFY(job.configureSubmission(c.submissionPath));QVERIFY(job.review());QTRY_VERIFY(!job.status().value("busy").toBool());
        const auto approval=job.status().value("review").toMap().value("commitment").toString();
        QVERIFY(save(c.nativeRpcPath,read(c.nativeRpcPath)+" "));QVERIFY(!job.submit(approval));
        QCOMPARE(read(c.log).count("--punish-submit-stdio-v1"),0);QVERIFY(!job.status().value("submitAvailable").toBool());}
    void submissionProbe_data() {
        QTest::addColumn<QString>("mode");QTest::addColumn<QString>("approval");
        QTest::addColumn<bool>("success");QTest::addColumn<int>("attempts");
        QTest::newRow("approved")<<QString("normal")<<QString("correct")<<true<<1;
        QTest::newRow("wrong-approval")<<QString("normal")<<QString("wrong")<<false<<0;
        QTest::newRow("missing-approval")<<QString("normal")<<QString("missing")<<false<<0;
        QTest::newRow("saved-attempt")<<QString("attempted")<<QString("correct")<<false<<0;
        QTest::newRow("saved-ack")<<QString("acknowledged")<<QString("correct")<<false<<0;
        QTest::newRow("lost-reply")<<QString("lost")<<QString("correct")<<false<<1;
    }
    void submissionProbe() {
        QFETCH(QString,mode);QFETCH(QString,approval);QFETCH(bool,success);QFETCH(int,attempts);
        PunishmentWorkerContext c;QVERIFY(c.create());QVERIFY(c.submissionFiles());QVERIFY(save(c.mode,mode.toUtf8()));
        const auto id=QUuid::createUuid().toString(QUuid::WithoutBraces);
        QJsonObject request{{"schema","monzero-bch-punish-submit-request-v1"},{"requestId",id}};
        if (approval!="missing") request["commitment"]=approval=="correct"
            ?QJsonDocument::fromJson(read(c.directory.filePath("review.json"))).object().value("commitment")
            :QJsonValue(QString(64,'0'));
        QProcess process;process.start(QCoreApplication::applicationDirPath()+"/dex_bch_punishment_probe",
            {"--punish-submit-stdio-v1",c.submissionPath});
        QVERIFY(process.waitForStarted());const auto bytes=QJsonDocument(request).toJson(QJsonDocument::Compact);
        QCOMPARE(process.write(bytes),qint64(bytes.size()));process.closeWriteChannel();QVERIFY(process.waitForFinished(10000));
        QCOMPARE(process.exitStatus(),QProcess::NormalExit);QCOMPARE(process.exitCode(),success?0:1);
        const auto output=process.readAllStandardOutput();
        if (success) {
            QVERIFY(process.readAllStandardError().isEmpty());const auto reply=QJsonDocument::fromJson(output).object();
            QCOMPARE(reply.value("requestId").toString(),id);
            QCOMPARE(reply.value("relay").toObject().value("state").toString(),QString("acknowledged"));
            QVERIFY(!reply.value("chainObserved").toBool());
        } else QVERIFY(output.isEmpty());
        QCOMPARE(read(c.log).count("--punish-submit-stdio-v1"),attempts);
    }
    void invalidRole() {PunishmentWorkerContext c;QVERIFY(c.create());c.config["role"]="bch-funder";QVERIFY(c.writeConfig());
        BchPunishmentJob job;QVERIFY(!job.configure(c.wrapper));QVERIFY(!job.review());}
};
QTEST_GUILESS_MAIN(BchPunishmentTests)
#include "test_bch_punishment.moc"
