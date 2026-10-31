#include "../BchRefundWalletJob.h"
#include <memory>
#include "BchRefundWalletWorkerTestFixture.h"
class BchRefundWalletTests:public QObject {
    Q_OBJECT
private slots:
    void freshReview_data() {
        QTest::addColumn<QString>("mode");
        for (const auto mode:{"normal","expiry","request","stale","authority","secret","history-ready","history-downgrade","submit-success","submit-lost","submit-request","submit-approval","submit-source","submit-stop","submit-delete","submit-escaped","submit-open","submit-check"}) QTest::newRow(mode)<<QString(mode);
    }
    void freshReview() {
        QFETCH(QString,mode);RefundWalletWorkerContext c;QVERIFY(c.create());
        auto policy=c.config.value("policy").toObject();policy["xmz_confirmations"]=1;c.config["policy"]=policy;
        auto d=refundWalletStatusValue(c);d.remove("chainObserved");d.remove("executable");d["refund"]=d.take("refundHint");
        d["schema"]="monzero-bch-native-refund-wallet-review-v1";d["action"]="restore";d["restoreHeight"]=0;
        d["minimumBchConfirmations"]=2;d["minimumXmzConfirmations"]=1;
        d["walletBackend"]=QJsonObject{{"schema","monzero-local-wallet-endpoint-v1"},{"host","127.0.0.1"},{"daemonPort",18081},{"walletPort",18083}};
        if (mode=="history-downgrade") {d["action"]="check";d["state"]="attempted";d["walletIntent"]=pinBytes(2);}
        const bool reconcile=mode=="submit-open" || mode=="submit-check";
        if (reconcile) {d["action"]=mode=="submit-open"?"open":"check";d["state"]="attempted";d["walletIntent"]=pinBytes(2);}
        QByteArray binding="monzero-bch-native-refund-wallet-review-v1";binding.append('\0');binding+=QJsonDocument(d).toJson(QJsonDocument::Compact);
        const QJsonObject review{{"details",d},{"commitment",QString::fromLatin1(QCryptographicHash::hash(binding,QCryptographicHash::Sha256).toHex())}};
        const QByteArray program=R"PY(#!/usr/bin/python3
import json,sys,pathlib,time
root=pathlib.Path(sys.argv[2]).parent
r=json.load(sys.stdin);mode=(root/'mode').read_text()
if sys.argv[1]=='--native-refund-wallet-status-stdio-v1':
 wallet=json.loads((root/'status.json').read_text())
 if mode.startswith('history-'):wallet['state']='ready';wallet['walletIntent']=[2]*32
 if mode in ('submit-open','submit-check'):wallet['state']='attempted';wallet['walletIntent']=[2]*32
 v=dict(schema='monzero-bch-native-refund-wallet-status-response-v1',requestId=r['requestId'],tradeId=wallet['swapId'],executable=False,chainObserved=False,wallet=wallet)
 print(json.dumps(v,sort_keys=True,separators=(',',':')),end='');sys.exit(0)
if sys.argv[1]=='--native-refund-wallet-submit-stdio-v1':
 assert set(r)=={'schema','requestId','action','commitment','password'}
 expected_password='fixture' if mode!='submit-escaped' else 'quote'+chr(34)+' slash'+chr(92)+' café π'
 expected_action='check' if mode=='submit-check' else 'open' if mode=='submit-open' else 'restore'
 if mode=='submit-check':expected_password=''
 assert r['schema']=='monzero-bch-native-refund-wallet-submit-request-v1' and r['action']==expected_action and r['password']==expected_password
 review=json.loads((root/'review.json').read_text());assert r['commitment']==review['commitment']
 (root/'submitted').write_text('attempted')
 if mode=='submit-lost':sys.exit(1)
 if mode in ('submit-stop','submit-delete'):time.sleep(10)
 wallet=json.loads((root/'status.json').read_text());wallet['state']='ready';wallet['walletIntent']=[2]*32
 v=dict(schema='monzero-bch-native-refund-wallet-submit-response-v1',requestId=r['requestId'],tradeId=wallet['swapId'],action=r['action'],approvedCommitment=r['commitment'],wallet=wallet,executable=False,chainObserved=False)
 if mode=='submit-request':v['requestId']='wrong'
 if mode=='submit-approval':v['approvedCommitment']='ab'*32
 if mode=='submit-source':
  p=root/'observer.json';p.write_text(p.read_text()+' ')
 print(json.dumps(v,sort_keys=True,separators=(',',':')),end='');sys.exit(0)
assert sys.argv[1]=='--native-refund-wallet-review-stdio-v1' 
assert set(r)=={'schema','requestId','action'} and r['action'] in ('restore','open','check')
review=json.loads((root/'review.json').read_text());mode=(root/'mode').read_text()
v=dict(schema='monzero-bch-native-refund-wallet-review-response-v1',requestId=r['requestId'],tradeId=review['details']['swapId'],executable=False,chainObserved=True,observedAt=int(time.time()),review=review)
if mode=='request':v['requestId']='wrong'
if mode=='stale':v['observedAt']-=60
if mode=='authority':v['executable']=True
if mode=='secret':v['review']['details']['privateKey']='unexpected'
print(json.dumps(v,sort_keys=True,separators=(',',':')),end='')
)PY";
        QVERIFY(save(c.helper,program,true));c.config["helperDigest"]=QString::fromLatin1(QCryptographicHash::hash(program,QCryptographicHash::Sha256).toHex());QVERIFY(c.writeConfig());
        QVERIFY(save(c.mode,mode.toUtf8()));QVERIFY(save(c.directory.filePath("review.json"),QJsonDocument(review).toJson(QJsonDocument::Compact)));
        auto write=[&](QString path,QJsonObject value){return save(path,QJsonDocument(value).toJson(QJsonDocument::Compact));};
        auto native=c.directory.filePath("native.json"),bch=c.directory.filePath("bch.json"),wrapper=c.directory.filePath("review-config.json");
        QVERIFY(write(native,{{"schema","monzero-private-bch-native-rpc-v1"},{"daemonPort",18081},{"walletPort",18083},{"loginFile",c.directory.filePath("never-read-login")}}));
        QVERIFY(write(bch,{{"schema","monzero-private-bch-observer-rpc-v1"},{"port",18443},{"cookieFile",bch+".cookie"},{"checkpointHeight",1},{"checkpoint",QString(64,'7')},{"subversion","/Bitcoin Cash Node:29.0.0/"}}));
        const auto observer=c.directory.filePath("observer.json");
        QVERIFY(write(observer,{{"schema","monzero-private-bch-native-rpc-v1"},{"daemonPort",18081},{"walletPort",18084},{"loginFile",c.directory.filePath("observer-login")}}));
        SwapWalletRpcProcess owner;
        if (mode.startsWith("submit-") || mode=="expiry") {
            const auto service=c.directory.filePath("service.py");
            QVERIFY(save(service,R"PY(#!/usr/bin/python3
import sys,pathlib,os,signal,time
root=pathlib.Path.cwd();port=sys.argv[sys.argv.index('--rpc-bind-port')+1]
p=root/('monzero-wallet-rpc.'+port+'.login');p.write_text('fixture:fixture');os.chmod(p,0o400)
signal.signal(signal.SIGTERM,lambda *_:sys.exit(0))
while True:time.sleep(0.05)
)PY",true));
            QVERIFY(owner.startFresh(service,c.directory.path(),18081,18083));
            QTRY_VERIFY(!(native=owner.provisionBchNativeSession()).isEmpty());
            QVERIFY(owner.ownsBchNativeSession(native));
        }
        QVERIFY(write(wrapper,{{"schema","monzero-private-bch-native-refund-wallet-review-v1"},{"statusConfigFile",c.wrapper},{"bchRpcFile",bch},{"nativeRpcFile",native},{"observerRpcFile",observer}}));
        if (mode=="normal") {
            const auto original=QJsonDocument::fromJson(read(observer)).object();
            for (const auto &change:QList<QPair<QString,QJsonValue>>{{"walletPort",18083},{"walletPort",18081},
                {"daemonPort",18082},{"loginFile","relative"},{"password","unexpected"}}) {
                auto invalid=original;invalid[change.first]=change.second;QVERIFY(write(observer,invalid));
                BchRefundWalletJob rejected;QVERIFY(rejected.configure(c.wrapper));QVERIFY(!rejected.configureReview(wrapper));
            }
            QVERIFY(write(observer,original));
        }
        const auto action=d.value("action").toString();
        const auto id=QUuid::createUuid().toString(QUuid::WithoutBraces);
        QJsonObject request{{"schema","monzero-bch-native-refund-wallet-review-request-v1"},{"requestId",id},{"action",action}};
        QProcess adapter;adapter.start(QCoreApplication::applicationDirPath()+"/dex_bch_refund_wallet_probe",{"--native-refund-wallet-review-stdio-v1",wrapper});
        QVERIFY(adapter.waitForStarted());adapter.write(QJsonDocument(request).toJson(QJsonDocument::Compact));adapter.closeWriteChannel();
        QVERIFY(adapter.waitForFinished(10000));QCOMPARE(adapter.exitStatus(),QProcess::NormalExit);
        const bool accepted=mode=="normal" || mode=="expiry" || mode.startsWith("submit-");
        QCOMPARE(adapter.exitCode(),accepted?0:1);
        const auto output=adapter.readAllStandardOutput();
        if (accepted) {const auto response=QJsonDocument::fromJson(output).object();QCOMPARE(response.value("requestId").toString(),id);QCOMPARE(response.value("review").toObject(),review);}
        else QVERIFY(output.isEmpty());
        auto jobHolder=std::make_unique<BchRefundWalletJob>();auto &job=*jobHolder;
        QVERIFY(job.configure(c.wrapper));QVERIFY(!job.requestReview("restore"));QVERIFY(job.configureReview(wrapper));
        QVERIFY(!job.requestReview("restore"));QVERIFY(job.refresh());QTRY_COMPARE(job.status().value("state").toString(),mode.startsWith("history-")?QString("ready"):reconcile?QString("attempted"):QString("not-attempted"));
        if (mode=="history-ready") {QVERIFY(!job.requestReview("restore"));return;}
        if (!mode.startsWith("history-") && !reconcile) QVERIFY(!job.requestReview("open"));
        QVERIFY(!job.requestReview("send"));QVERIFY(job.requestReview(action));QVERIFY(!job.requestReview("restore"));
        QTRY_VERIFY(!job.status().value("busy").toBool());
        QCOMPARE(job.status().value("state").toString(),accepted?QString("reviewed"):QString("unknown"));
        QCOMPARE(job.status().value("chainObserved").toBool(),accepted);QVERIFY(!job.status().value("executable").toBool());
        QCOMPARE(job.status().value("review").toMap().isEmpty(),!accepted);
        if (mode.startsWith("submit-")) {
            const auto commitment=review.value("commitment").toString();
            QByteArray rejectedPassword("fixture");
            QVERIFY(!job.submit(nullptr,commitment,std::move(rejectedPassword)));QVERIFY(rejectedPassword.isEmpty());
            SwapWalletRpcProcess unrelated;QVERIFY(!job.submit(&unrelated,commitment,"fixture"));
            QVERIFY(!job.submit(&owner,QString(64,'0'),"fixture"));
            if (action=="check") QVERIFY(!job.submit(&owner,commitment,"unexpected"));
            else QVERIFY(!job.submit(&owner,commitment,QByteArray()));
            QVERIFY(!job.submit(&owner,commitment,QByteArray(1025,'a')));
            QVERIFY(!job.submit(&owner,commitment,QByteArray("bad\npassword")));
            QVERIFY(!QFileInfo::exists(c.directory.filePath("submitted")));
            const auto available=owner.beginWalletOperation();QVERIFY(!available.isEmpty());
            QVERIFY(!job.submit(&owner,commitment,"fixture"));QVERIFY(owner.completeWalletOperation(available,true));
            QByteArray password=action=="check"?QByteArray():QByteArray("fixture");
            if (mode=="submit-escaped") password=QByteArray("quote")+char(34)+" slash"+char(92)+QString::fromUtf8(" café π").toUtf8();
            QVERIFY(job.submit(&owner,commitment,std::move(password)));QVERIFY(password.isEmpty());
            QVERIFY(!job.submit(&owner,commitment,"fixture"));QVERIFY(owner.beginWalletOperation().isEmpty());
            QVERIFY(job.status().value("review").toMap().isEmpty());
            if (mode=="submit-delete") {
                QTRY_VERIFY(QFileInfo::exists(c.directory.filePath("submitted")));
                jobHolder.reset();QVERIFY(owner.beginWalletOperation().isEmpty());
                QSignalSpy stopped(&owner,&SwapWalletRpcProcess::stopped);owner.stop();QTRY_COMPARE(stopped.count(),1);
                QCOMPARE(stopped.at(0).at(0).toBool(),false);
                QFile state(QDir(owner.dataDirectory()).filePath("service.state"));QVERIFY(state.open(QIODevice::ReadOnly));
                QCOMPARE(state.readAll(),QByteArray("monzero-swap-rpc-v1\nrecovery-required\n"));return;
            }
            if (mode=="submit-stop") {QTRY_VERIFY(QFileInfo::exists(c.directory.filePath("submitted")));owner.stop();}
            QTRY_VERIFY(!job.status().value("busy").toBool());
            const bool success=mode=="submit-success" || mode=="submit-escaped" || reconcile;
            QCOMPARE(job.status().value("state").toString(),success?QString("ready"):QString("recovery-required"));
            QCOMPARE(job.status().value("submissionUncertain").toBool(),!success);
            QCOMPARE(job.status().value("result").toMap().isEmpty(),!success);
            QCOMPARE(job.status().value("savedWallet").toMap().value("state").toString(),success?QString("ready"):QString("not-attempted"));
            QVERIFY(!job.submit(&owner,commitment,"fixture"));
            const auto next=owner.beginWalletOperation();QCOMPARE(next.isEmpty(),!success);
            if (success) QVERIFY(owner.completeWalletOperation(next,true));
            QSignalSpy stopped(&owner,&SwapWalletRpcProcess::stopped);owner.stop();
            if (QFileInfo::exists(owner.loginFile())) QTRY_VERIFY(!QFileInfo::exists(owner.loginFile()));
            QFile state(QDir(owner.dataDirectory()).filePath("service.state"));QVERIFY(state.open(QIODevice::ReadOnly));
            QCOMPARE(state.readAll(),success?QByteArray("monzero-swap-rpc-v1\nstopped\n"):QByteArray("monzero-swap-rpc-v1\nrecovery-required\n"));
        }
        if (mode=="expiry") {
            QTRY_COMPARE_WITH_TIMEOUT(job.status().value("state").toString(),QString("review-expired"),32000);
            QVERIFY(job.status().value("review").toMap().isEmpty());QVERIFY(job.status().value("result").toMap().isEmpty());
            QVERIFY(!job.status().value("chainObserved").toBool());
            QVERIFY(!job.submit(&owner,review.value("commitment").toString(),"fixture"));
            QVERIFY(!QFileInfo::exists(c.directory.filePath("submitted")));
        }
        if (mode=="normal") {
            QVERIFY(save(observer,read(observer)+" "));QVERIFY(!job.requestReview("restore"));
            QVERIFY(job.status().value("review").toMap().isEmpty());QVERIFY(!job.status().value("chainObserved").toBool());
        }
    }

    void adapterRejectsSelfReference() {
        RefundWalletWorkerContext c;QVERIFY(c.create());
        const auto probe=QCoreApplication::applicationDirPath()+"/dex_bch_refund_wallet_probe";
        c.config["helper"]=probe;c.config["helperDigest"]=QString::fromLatin1(QCryptographicHash::hash(read(probe),QCryptographicHash::Sha256).toHex());
        QVERIFY(c.writeConfig());
        QProcess process;process.start(probe,{"--native-refund-wallet-status-stdio-v1",c.wrapper});
        QVERIFY(process.waitForStarted());
        process.write(QJsonDocument(QJsonObject{{"schema","monzero-bch-native-refund-wallet-status-request-v1"},
            {"requestId",QUuid::createUuid().toString(QUuid::WithoutBraces)}}).toJson(QJsonDocument::Compact));process.closeWriteChannel();
        QVERIFY(process.waitForFinished(10000));QCOMPARE(process.exitCode(),1);QVERIFY(process.readAllStandardOutput().isEmpty());QVERIFY(read(c.log).isEmpty());
    }
    void processAdapter_data() {
        QTest::addColumn<QString>("mode");QTest::addColumn<bool>("malformed");QTest::addColumn<bool>("success");
        QTest::newRow("saved")<<QString("normal")<<false<<true;
        QTest::newRow("attempted")<<QString("attempted")<<false<<true;
        QTest::newRow("ready")<<QString("ready")<<false<<true;
        QTest::newRow("invalid-request")<<QString("normal")<<true<<false;
        QTest::newRow("authority")<<QString("authority")<<false<<false;
        QTest::newRow("correlation")<<QString("request")<<false<<false;
    }
    void processAdapter() {
        QFETCH(QString,mode);QFETCH(bool,malformed);QFETCH(bool,success);
        RefundWalletWorkerContext c;QVERIFY(c.create());QVERIFY(save(c.mode,mode.toUtf8()));
        const auto id=QUuid::createUuid().toString(QUuid::WithoutBraces);
        QJsonObject request{{"schema","monzero-bch-native-refund-wallet-status-request-v1"},{"requestId",id}};
        if (malformed) request["commitment"]=QJsonValue::Null;
        QProcess process;process.start(QCoreApplication::applicationDirPath()+"/dex_bch_refund_wallet_probe",
            {"--native-refund-wallet-status-stdio-v1",c.wrapper});
        QVERIFY(process.waitForStarted());process.write(QJsonDocument(request).toJson(QJsonDocument::Compact));process.closeWriteChannel();
        QVERIFY(process.waitForFinished(10000));QCOMPARE(process.exitStatus(),QProcess::NormalExit);QCOMPARE(process.exitCode(),success?0:1);
        const auto output=process.readAllStandardOutput();
        if (success) {
            QVERIFY(process.readAllStandardError().isEmpty());
            const auto reply=QJsonDocument::fromJson(output).object();QCOMPARE(reply.value("requestId").toString(),id);
            QCOMPARE(reply.value("wallet").toObject().value("state").toString(),mode=="normal"?QString("not-attempted"):mode);
            QCOMPARE(read(c.log).count("--native-refund-wallet-status-stdio-v1"),1);
        } else {QVERIFY(output.isEmpty());if (malformed) QVERIFY(read(c.log).isEmpty());}
    }

    void readOnlyHistory() {RefundWalletWorkerContext c;QVERIFY(c.create());BchRefundWalletJob job;QVERIFY(job.configure(c.wrapper));QVERIFY(read(c.log).isEmpty());
        for (const auto state:{"not-attempted","attempted","ready"}) {
            QVERIFY(save(c.mode,QByteArray(state)=="not-attempted"?QByteArray("normal"):QByteArray(state)));
            QVERIFY(job.refresh());QVERIFY(!job.refresh());QTRY_VERIFY(!job.status().value("busy").toBool());
            QCOMPARE(job.status().value("state").toString(),QString(state));QVERIFY(!job.status().value("chainObserved").toBool());
            QVERIFY(!job.status().value("executable").toBool());
        }
        QCOMPARE(read(c.log).count("--native-refund-wallet-status-stdio-v1"),3);
    }
    void rejectsReply_data() {QTest::addColumn<QString>("mode");
        for (const auto mode:{"request","authority","chain","private","stderr","source","overflow"}) QTest::newRow(mode)<<QString(mode);}
    void rejectsReply() {QFETCH(QString,mode);RefundWalletWorkerContext c;QVERIFY(c.create());BchRefundWalletJob job;QVERIFY(job.configure(c.wrapper));
        QVERIFY(save(c.mode,mode.toUtf8()));QVERIFY(job.refresh());QTRY_VERIFY(!job.status().value("busy").toBool());
        QCOMPARE(job.status().value("state").toString(),QString("unknown"));QVERIFY(job.status().value("wallet").toMap().isEmpty());
        if (mode=="overflow") {QTRY_VERIFY(job.status().value("checkAvailable").toBool());}
        if (mode=="source") QVERIFY(!job.status().value("checkAvailable").toBool());
    }
    void regressionRetainsLabelledHistory() {RefundWalletWorkerContext c;QVERIFY(c.create());BchRefundWalletJob job;QVERIFY(job.configure(c.wrapper));
        QVERIFY(save(c.mode,"ready"));QVERIFY(job.refresh());QTRY_VERIFY(!job.status().value("busy").toBool());
        QCOMPARE(job.status().value("state").toString(),QString("ready"));
        QVERIFY(save(c.mode,"attempted"));QVERIFY(job.refresh());QVERIFY(job.status().value("wallet").toMap().isEmpty());
        QTRY_VERIFY(!job.status().value("busy").toBool());QCOMPARE(job.status().value("state").toString(),QString("unknown"));
        QCOMPARE(job.status().value("savedWallet").toMap().value("state").toString(),QString("ready"));
    }
    void changedAccountIsRejected() {RefundWalletWorkerContext c;QVERIFY(c.create());BchRefundWalletJob job;QVERIFY(job.configure(c.wrapper));
        QVERIFY(job.refresh());QTRY_VERIFY(!job.status().value("busy").toBool());QVERIFY(save(c.mode,"changed"));
        QVERIFY(job.refresh());QTRY_VERIFY(!job.status().value("busy").toBool());QCOMPARE(job.status().value("state").toString(),QString("unknown"));}
    void sourceMutationPreventsLaunch() {RefundWalletWorkerContext c;QVERIFY(c.create());BchRefundWalletJob job;QVERIFY(job.configure(c.wrapper));
        QVERIFY(save(c.wrapper,read(c.wrapper)+" "));QVERIFY(!job.refresh());QVERIFY(read(c.log).isEmpty());
        QVERIFY(!job.status().value("checkAvailable").toBool());}
};
QTEST_GUILESS_MAIN(BchRefundWalletTests)
#include "test_bch_refund_wallet.moc"
