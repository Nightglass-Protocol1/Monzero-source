#include "BchAdmissionTestFixture.h"
#include "../BchFundingJob.h"
namespace {
struct FundingFixture: Fixture {
    QString fundingPath,submissionPath,rpcPath,statePath;
    bool create() {
        if (!Fixture::create()) return false;
        const QByteArray program=R"PY(#!/usr/bin/python3
import json,sys,pathlib,hashlib,time
root=pathlib.Path(__file__).parent
wrapper=json.loads(pathlib.Path(sys.argv[2]).read_text())
submission=wrapper if 'fundingConfigFile' in wrapper else None
if submission:wrapper=json.loads(pathlib.Path(submission['fundingConfigFile']).read_text())
c=json.loads(pathlib.Path(wrapper['admissionConfigFile']).read_text())
terms=json.loads(pathlib.Path(c['quoteFile']).read_text())['terms']
request=json.load(sys.stdin);mode=(root/'mode').read_text()
action=request['schema'].split('-')[-3]
assert sys.argv[1]=='--funding-'+action+'-stdio-v1'
assert set(request)==({'schema','requestId','commitment'} if action=='submit' else {'schema','requestId'})
with (root/'calls').open('a') as f:f.write('funding-'+action+'\n')
compact=lambda v:json.dumps(v,sort_keys=True,separators=(',',':')).encode()
d={'schema':'monzero-bch-funding-relay-review-v1','action':'broadcast-funding','swapId':c['tradeId'],
'quote':list(bytes.fromhex(c['quoteCommitment'])),'admission':[1]*32,
'deployment':list(bytes.fromhex(wrapper['deploymentCommitment'])),'network':terms['bchNetwork'],
'genesis':terms['bchGenesis'],'expiresAt':terms['expiresAt'],'fundingTxid':'11'*32,
'contractVout':0,'signedBytes':[2]*32,'preparation':[3]*32,'feeAtoms':1,
'outputs':[{'atoms':int(terms['bchAtoms']),'script':'51'},{'atoms':1,'script':'52'}],
'privateRecovery':[4]*32,'cancelTxid':'22'*32,'refundTxid':'33'*32,'punishTxid':'44'*32}
if mode=='deployment':d['deployment']=[9]*32
if mode=='quote':d['quote']=[9]*32
if mode=='network':d['network']='bch-mainnet'
if mode=='expiry':d['expiresAt']='1'
if mode=='fee':d['feeAtoms']=2
if mode=='vout':d['contractVout']=2
if mode=='principal':d['outputs'][0]['atoms']=2
if mode=='precision':d['outputs'][0]['atoms']=9007199254740992
if mode=='fraction':d['outputs'][1]['atoms']=0.5
if mode=='script':d['outputs'][1]['script']='not-hex'
if mode=='empty':d['outputs']=[]
if mode=='pin':d['privateRecovery']=[1]*31
if mode=='zero-txid':d['fundingTxid']='00'*32
if mode=='detail-extra':d['extra']=True
if mode=='different-review' and action=='submit':d['outputs'][1]['script']='53'
review={'details':d,'commitment':hashlib.sha256(b'monzero-bch-funding-relay-review-v1\0'+compact(d)).hexdigest()}
if action=='submit':
 (root/'funding-state').write_text('attempted' if mode=='lost' else 'acknowledged')
 if mode=='lost':sys.exit(1)
r={'schema':'monzero-bch-funding-'+action+'-response-v1','requestId':request['requestId'],
   'tradeId':c['tradeId'],'executable':False,'chainObserved':False,'review':review}
if action!='review':
 r['relay']={'state':(root/'funding-state').read_text(),'txid':d['fundingTxid'],'confirmed':False}
 if mode=='relay-txid':r['relay']['txid']='22'*32
 if mode=='relay-state':r['relay']['state']='confirmed'
 if mode=='relay-extra':r['relay']['extra']=True
 if mode=='relay-flag':r['relay']['confirmed']='false'
 if mode=='confirmed':r['relay']['confirmed']=True
 if mode=='attempted-submit':r['relay']['state']='attempted'
 if mode=='rpc-replace' and action=='submit':
  with pathlib.Path(submission['rpcFile']).open('a') as f:f.write(' ')
if action=='observe':
 r['chainObserved']=True
 o={'state':'confirmed-unspent','txid':d['fundingTxid'],'tip':'88'*32,'confirmations':'3','lockOutputUnspent':True,'observedAt':str(int(time.time()))}
 r['observation']=o
 if mode=='mempool':o.update(state='mempool',confirmations=None,lockOutputUnspent=False)
 if mode=='unobserved':o.update(state='unobserved',confirmations=None,lockOutputUnspent=None)
 if mode=='observe-txid':o['txid']='22'*32
 if mode=='observe-tip':o['tip']='00'*32
 if mode=='observe-time':o['observedAt']='1'
 if mode=='observe-future':o['observedAt']=str(int(time.time())+3600)
 if mode=='observe-depth':o['confirmations']='0'
 if mode=='observe-numeric-depth':o['confirmations']=3
 if mode=='observe-leading-zero':o['confirmations']='03'
 if mode=='observe-overflow':o['confirmations']='18446744073709551616'
 if mode=='observe-spent':o['lockOutputUnspent']=False
 if mode=='observe-extra':o['extra']=True
 if mode=='observe-flag':r['chainObserved']=False
 if mode=='rpc-replace':
  with pathlib.Path(submission['rpcFile']).open('a') as f:f.write(' ')
if mode=='request':r['requestId']='changed'
if mode=='trade':r['tradeId']='changed'
if mode=='digest':review['commitment']='00'*32
if mode=='execute':r['executable']=True
if mode=='chain':r['chainObserved']=True
if mode=='flag':r['chainObserved']='false'
if mode=='extra':r['extra']=True
if mode=='stderr':sys.stderr.write('unexpected')
if mode=='oversized':sys.stdout.write(' '*16385);sys.exit(0)
if mode=='replace':
 with pathlib.Path(__file__).open('a') as f:f.write('\n# changed\n')
sys.stdout.buffer.write(compact(r)+(b'\n' if mode=='newline' else b''))
)PY";
        if (!save(helper,program,true)) return false;
        config["helperDigest"]=QString::fromLatin1(QCryptographicHash::hash(program,QCryptographicHash::Sha256).toHex());
        config["role"]="bch-funder";
        auto record=QJsonDocument::fromJson(read(config.value("quoteFile").toString())).object();
        auto terms=record.value("terms").toObject();
        const QString genesis="0f9188f13cb7b2c71f2a335e3a4fc328bf5beb436012afca590b1a11466e2206";
        terms["bchGenesis"]=genesis;record["terms"]=terms;
        auto policy=config.value("policy").toObject();policy["bch_genesis"]=genesis;config["policy"]=policy;
        if (!saveQuote(record)) return false;
        const QJsonObject rpc{{"schema","monzero-private-bch-funding-rpc-v1"},{"port",12345},{"wallet","test"},
            {"anchor","bchreg:qanchor"},{"anchorScript","51"},{"cookieFile",directory.filePath("cookie")},
            {"checkpointHeight",1},{"checkpoint",QString(64,'7')},{"subversion","/Bitcoin Cash Node:29.0.0/"}};
        const QJsonObject deployment{{"schema","monzero-bch-wallet-deployment-v1"},{"host","127.0.0.1"},
            {"port",12345},{"network","bch-regtest"},{"genesis",genesis},{"checkpointHeight",1},
            {"checkpoint",QString(64,'7')},{"wallet","test"},{"anchor","bchreg:qanchor"},{"script","51"},
            {"subversion","/Bitcoin Cash Node:29.0.0/"}};
        const auto deploymentDigest=QString::fromLatin1(QCryptographicHash::hash(
            QJsonDocument(deployment).toJson(QJsonDocument::Compact),QCryptographicHash::Sha256).toHex());
        fundingPath=directory.filePath("funding.json");submissionPath=directory.filePath("submission.json");
        rpcPath=directory.filePath("rpc.json");statePath=directory.filePath("funding-state");
        return writeConfig() && save(statePath,"not-attempted")
            && save(rpcPath,QJsonDocument(rpc).toJson(QJsonDocument::Compact))
            && save(submissionPath,QJsonDocument(QJsonObject{{"schema","monzero-private-bch-funding-submit-v1"},
                {"fundingConfigFile",fundingPath},{"rpcFile",rpcPath}}).toJson(QJsonDocument::Compact))
            && save(fundingPath,QJsonDocument(QJsonObject{{"schema","monzero-private-bch-funding-v1"},
                {"admissionConfigFile",path},{"deploymentCommitment",deploymentDigest}}).toJson(QJsonDocument::Compact));
    }
    bool saveQuote(QJsonObject record) {
        const auto terms=record.value("terms").toObject();
        QByteArray binding("monzero-native-bch-xmz-quote-v1");binding.append('\0');
        binding+=QJsonDocument(terms).toJson(QJsonDocument::Compact);
        const auto digest=QString::fromLatin1(QCryptographicHash::hash(binding,QCryptographicHash::Sha256).toHex());
        config["quoteCommitment"]=digest;record["commitment"]=digest;
        return save(config.value("quoteFile").toString(),QJsonDocument(record).toJson(QJsonDocument::Compact)) && writeConfig();
    }
    bool expire() {
        auto record=QJsonDocument::fromJson(read(config.value("quoteFile").toString())).object();
        auto terms=record.value("terms").toObject();
        terms["expiresAt"]=QString::number(QDateTime::currentSecsSinceEpoch()-100);record["terms"]=terms;
        config["admittedAt"]=QDateTime::currentSecsSinceEpoch()-300;record["admittedAt"]=config["admittedAt"];
        return saveQuote(record);
    }
};
}
class BchFundingTests: public QObject {
    Q_OBJECT
private slots:
    void observationClearsStaleEvidenceAndNeverRenewsRelay() {
        FundingFixture f;QVERIFY(f.create());QVERIFY(save(f.statePath,"attempted"));
        BchFundingJob job;QVERIFY(job.configureSubmission(f.submissionPath));QVERIFY(job.observe());
        QTRY_VERIFY_WITH_TIMEOUT(!job.status().value("busy").toBool(),10000);
        QVERIFY(job.status().value("chainObserved").toBool());
        QCOMPARE(job.status().value("observation").toMap().value("confirmations").toString(),QString("3"));
        QCOMPARE(job.status().value("relayState").toString(),QString("attempted"));
        QVERIFY(!job.review());QVERIFY(!job.status().value("submitAvailable").toBool());
        QVERIFY(save(f.mode,"mempool"));QVERIFY(job.observe());
        QVERIFY(!job.status().value("chainObserved").toBool());
        QTRY_VERIFY_WITH_TIMEOUT(!job.status().value("busy").toBool(),10000);
        QCOMPARE(job.status().value("observation").toMap().value("state").toString(),QString("mempool"));
        QVERIFY(save(f.mode,"unobserved"));QVERIFY(job.observe());
        QTRY_VERIFY_WITH_TIMEOUT(!job.status().value("busy").toBool(),10000);
        QVERIFY(job.status().value("chainObserved").toBool());QVERIFY(!job.review());
        QVERIFY(save(f.mode,"observe-tip"));QVERIFY(job.observe());
        QTRY_VERIFY_WITH_TIMEOUT(!job.status().value("busy").toBool(),10000);
        QVERIFY(!job.status().value("chainObserved").toBool());QVERIFY(job.status().value("observation").toMap().isEmpty());
        QCOMPARE(job.status().value("state").toString(),QString("unknown"));
        QVERIFY(!read(f.log).contains("funding-submit"));
    }
    void observationAfterExpiryAndMissingRpcStatus() {
        FundingFixture f;QVERIFY(f.create());QVERIFY(f.expire());BchFundingJob job;
        QVERIFY(job.configureSubmission(f.submissionPath));QVERIFY(job.observe());
        QTRY_VERIFY_WITH_TIMEOUT(!job.status().value("busy").toBool(),10000);
        QVERIFY(job.status().value("chainObserved").toBool());QVERIFY(!job.review());
        QVERIFY(QFile::remove(f.rpcPath));QVERIFY(!job.observe());
        QVERIFY(!job.status().value("chainObserved").toBool());
        QVERIFY(job.status().value("result").toMap().isEmpty());QVERIFY(job.refresh());
        QTRY_VERIFY_WITH_TIMEOUT(!job.status().value("busy").toBool(),10000);
        QVERIFY(job.status().value("statusKnown").toBool());QVERIFY(!job.status().value("observeAvailable").toBool());
        QVERIFY(!job.review());
    }
    void rejectMalformedObservation_data() {
        QTest::addColumn<QString>("mode");
        for (const auto mode:{"observe-txid","observe-tip","observe-time","observe-future","observe-depth",
            "observe-numeric-depth","observe-leading-zero","observe-overflow","observe-spent","observe-extra","observe-flag","rpc-replace"})
            QTest::newRow(mode)<<QString(mode);
    }
    void rejectMalformedObservation() {
        QFETCH(QString,mode);FundingFixture f;QVERIFY(f.create());QVERIFY(save(f.mode,mode.toUtf8()));
        BchFundingJob job;QVERIFY(job.configureSubmission(f.submissionPath));QVERIFY(job.observe());
        QTRY_VERIFY_WITH_TIMEOUT(!job.status().value("busy").toBool(),10000);
        QVERIFY(!job.status().value("chainObserved").toBool());QVERIFY(job.status().value("observation").toMap().isEmpty());
        QCOMPARE(job.status().value("state").toString(),QString("unknown"));
        QVERIFY(!read(f.log).contains("funding-submit"));
    }
    void approvedSubmitRequiresStatusAndFreshReview() {
        FundingFixture f;QVERIFY(f.create());BchFundingJob job;QVERIFY(job.configureSubmission(f.submissionPath));
        QVERIFY(!QFile::exists(f.log));QVERIFY(!job.submit(QString(64,'0')));
        QVERIFY(job.review());QTRY_VERIFY_WITH_TIMEOUT(!job.status().value("busy").toBool(),10000);
        QVERIFY(!job.status().value("submitAvailable").toBool());
        QVERIFY(job.refresh());QTRY_VERIFY_WITH_TIMEOUT(!job.status().value("busy").toBool(),10000);
        QCOMPARE(job.status().value("relayState").toString(),QString("not-attempted"));
        QVERIFY(!job.status().value("submitAvailable").toBool());
        QVERIFY(job.review());QTRY_VERIFY_WITH_TIMEOUT(!job.status().value("busy").toBool(),10000);
        QVERIFY(job.status().value("submitAvailable").toBool());
        const auto digest=job.status().value("review").toMap().value("commitment").toString();
        QVERIFY(!job.submit(QString(64,'0')));QVERIFY(job.submit(digest));QVERIFY(!job.submit(digest));
        QTRY_VERIFY_WITH_TIMEOUT(!job.status().value("busy").toBool(),10000);
        QCOMPARE(job.status().value("state").toString(),QString("acknowledged"));
        QVERIFY(job.status().value("submitAttempted").toBool());QVERIFY(!job.submit(digest));QVERIFY(!job.review());
        BchFundingJob resumed;QVERIFY(resumed.configureSubmission(f.submissionPath));
        QVERIFY(resumed.refresh());QTRY_VERIFY_WITH_TIMEOUT(!resumed.status().value("busy").toBool(),10000);
        QVERIFY(!resumed.review());QVERIFY(!resumed.submit(digest));
        QCOMPARE(read(f.log).count("funding-submit"),1);
    }
    void lostReplyCannotResubmitAndStatusNeedsNoRpcDescriptor() {
        FundingFixture f;QVERIFY(f.create());BchFundingJob job;QVERIFY(job.configureSubmission(f.submissionPath));
        QVERIFY(job.refresh());QTRY_VERIFY_WITH_TIMEOUT(!job.status().value("busy").toBool(),10000);
        QVERIFY(job.review());QTRY_VERIFY_WITH_TIMEOUT(!job.status().value("busy").toBool(),10000);
        const auto digest=job.status().value("review").toMap().value("commitment").toString();
        QVERIFY(save(f.mode,"lost"));QVERIFY(job.submit(digest));
        QTRY_VERIFY_WITH_TIMEOUT(!job.status().value("busy").toBool(),10000);
        QCOMPARE(job.status().value("state").toString(),QString("unknown"));QVERIFY(!job.submit(digest));QVERIFY(!job.review());
        QVERIFY(QFile::remove(f.rpcPath));QVERIFY(QFile::remove(f.submissionPath));
        QVERIFY(job.refresh());QTRY_VERIFY_WITH_TIMEOUT(!job.status().value("busy").toBool(),10000);
        QCOMPARE(job.status().value("relayState").toString(),QString("attempted"));QVERIFY(!job.submit(digest));
        BchFundingJob resumed;QVERIFY(resumed.configure(f.fundingPath));QVERIFY(resumed.refresh());
        QTRY_VERIFY_WITH_TIMEOUT(!resumed.status().value("busy").toBool(),10000);
        QCOMPARE(resumed.status().value("state").toString(),QString("attempted"));QVERIFY(!resumed.review());
        QCOMPARE(read(f.log).count("funding-submit"),1);
    }
    void historicalStatusNeverCreatesFreshConsent() {
        FundingFixture f;QVERIFY(f.create());QVERIFY(f.expire());BchFundingJob job;
        QVERIFY(job.configureSubmission(f.submissionPath));QVERIFY(job.refresh());
        QTRY_VERIFY_WITH_TIMEOUT(!job.status().value("busy").toBool(),10000);
        QVERIFY(job.status().value("statusKnown").toBool());QVERIFY(!job.status().value("savedReview").toMap().isEmpty());
        QVERIFY(job.status().value("review").toMap().isEmpty());QVERIFY(!job.status().value("submitAvailable").toBool());
        QVERIFY(job.review());QTRY_VERIFY_WITH_TIMEOUT(!job.status().value("busy").toBool(),10000);
        QCOMPARE(job.status().value("state").toString(),QString("unknown"));QVERIFY(!job.submit(QString(64,'0')));
        QVERIFY(!read(f.log).contains("funding-submit"));
    }
    void invalidRelayEvidence_data() {
        QTest::addColumn<QString>("mode");QTest::addColumn<bool>("submit");
        for (const auto mode:{"relay-txid","relay-state","relay-extra","relay-flag","confirmed"}) {
            QTest::newRow(qPrintable(QString(mode)+" status"))<<QString(mode)<<false;
            QTest::newRow(qPrintable(QString(mode)+" submit"))<<QString(mode)<<true;
        }
        for (const auto mode:{"different-review","attempted-submit","rpc-replace"}) QTest::newRow(mode)<<QString(mode)<<true;
    }
    void invalidRelayEvidence() {
        QFETCH(QString,mode);QFETCH(bool,submit);FundingFixture f;QVERIFY(f.create());BchFundingJob job;
        QVERIFY(job.configureSubmission(f.submissionPath));
        if (submit) {
            QVERIFY(job.refresh());QTRY_VERIFY_WITH_TIMEOUT(!job.status().value("busy").toBool(),10000);
            QVERIFY(job.review());QTRY_VERIFY_WITH_TIMEOUT(!job.status().value("busy").toBool(),10000);
        }
        const auto approved=job.status().value("review").toMap().value("commitment").toString();
        QVERIFY(save(f.mode,mode.toUtf8()));QVERIFY(submit?job.submit(approved):job.refresh());
        QTRY_VERIFY_WITH_TIMEOUT(!job.status().value("busy").toBool(),10000);
        QCOMPARE(job.status().value("state").toString(),QString("unknown"));
        QVERIFY(job.status().value("result").toMap().isEmpty());QVERIFY(!job.status().value("submitAvailable").toBool());
        if (submit) {QVERIFY(!job.submit(approved));QVERIFY(!job.review());}
    }
    void changedRpcDescriptorBlocksSendButNotStatus() {
        FundingFixture f;QVERIFY(f.create());BchFundingJob job;QVERIFY(job.configureSubmission(f.submissionPath));
        QVERIFY(job.refresh());QTRY_VERIFY_WITH_TIMEOUT(!job.status().value("busy").toBool(),10000);
        QVERIFY(job.review());QTRY_VERIFY_WITH_TIMEOUT(!job.status().value("busy").toBool(),10000);
        const auto digest=job.status().value("review").toMap().value("commitment").toString();
        QVERIFY(save(f.rpcPath,read(f.rpcPath)+" "));QVERIFY(!job.submit(digest));
        QVERIFY(!read(f.log).contains("funding-submit"));QVERIFY(job.refresh());
        QTRY_VERIFY_WITH_TIMEOUT(!job.status().value("busy").toBool(),10000);
        QVERIFY(job.status().value("statusKnown").toBool());QVERIFY(!job.status().value("submitAvailable").toBool());
    }
    void rejectsWrongRpcBindingBeforeEnablingSubmission() {
        FundingFixture f;QVERIFY(f.create());auto rpc=QJsonDocument::fromJson(read(f.rpcPath)).object();
        rpc["wallet"]="other";QVERIFY(save(f.rpcPath,QJsonDocument(rpc).toJson(QJsonDocument::Compact)));
        BchFundingJob job;QVERIFY(!job.configureSubmission(f.submissionPath));
        QVERIFY(!job.status().value("submissionConfigured").toBool());QVERIFY(!QFile::exists(f.log));
    }
    void reviewDoesNotLaunchOnConfigureAndCanRefresh() {
        FundingFixture f;QVERIFY(f.create());BchFundingJob job;
        QVERIFY(job.configure(f.fundingPath));QVERIFY(!job.configure(f.fundingPath));
        QVERIFY(!QFile::exists(f.log));QVERIFY(job.review());QVERIFY(!job.review());
        QTRY_VERIFY_WITH_TIMEOUT(!job.status().value("busy").toBool(),10000);
        QCOMPARE(job.status().value("state").toString(),QString("reviewed"));
        QVERIFY(!job.status().value("review").toMap().isEmpty());
        QVERIFY(!job.status().value("chainObserved").toBool());QVERIFY(!job.status().value("executable").toBool());
        QVERIFY(job.review());QVERIFY(job.status().value("review").toMap().isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(!job.status().value("busy").toBool(),10000);
        QCOMPARE(job.status().value("state").toString(),QString("reviewed"));
        QCOMPARE(read(f.log),QByteArray("funding-review\nfunding-review\n"));
    }
    void rejectsChangedResponses_data() {
        QTest::addColumn<QString>("mode");
        for (const auto mode:{"deployment","quote","network","expiry","fee","vout","principal","precision",
            "fraction","script","empty","pin","zero-txid","detail-extra","request","trade","digest","execute",
            "chain","flag","extra","stderr","oversized","replace","newline"}) QTest::newRow(mode)<<QString(mode);
    }
    void rejectsChangedResponses() {
        QFETCH(QString,mode);FundingFixture f;QVERIFY(f.create());QVERIFY(save(f.mode,mode.toUtf8()));
        BchFundingJob job;QVERIFY(job.configure(f.fundingPath));QVERIFY(job.review());
        QTRY_VERIFY_WITH_TIMEOUT(!job.status().value("busy").toBool(),10000);
        QCOMPARE(job.status().value("state").toString(),QString("unknown"));
        QVERIFY(job.status().value("review").toMap().isEmpty());
    }
    void rejectsChangedSourcesBeforeLaunch_data() {
        QTest::addColumn<QString>("source");
        for (const auto source:{"wrapper","admission","quote","helper"}) QTest::newRow(source)<<QString(source);
    }
    void rejectsChangedSourcesBeforeLaunch() {
        QFETCH(QString,source);FundingFixture f;QVERIFY(f.create());BchFundingJob job;QVERIFY(job.configure(f.fundingPath));
        const auto path=source=="wrapper"?f.fundingPath:source=="admission"?f.path:source=="quote"?
            f.config.value("quoteFile").toString():f.helper;
        QVERIFY(save(path,read(path)+" ",source=="helper"));QVERIFY(!job.review());
        QVERIFY(!QFile::exists(f.log));QVERIFY(!job.status().value("reviewAvailable").toBool());
    }
    void rejectsWrongRoleAndUnsafeWrapper() {
        FundingFixture f;QVERIFY(f.create());f.config["role"]="native-funder";QVERIFY(f.writeConfig());
        BchFundingJob wrong;QVERIFY(!wrong.configure(f.fundingPath));f.config["role"]="bch-funder";QVERIFY(f.writeConfig());
        QVERIFY(QFile::setPermissions(f.fundingPath,QFile::ReadOwner|QFile::WriteOwner|QFile::ReadOther));
        BchFundingJob unsafe;QVERIFY(!unsafe.configure(f.fundingPath));QVERIFY(!QFile::exists(f.log));
    }
};
QTEST_GUILESS_MAIN(BchFundingTests)
#include "test_bch_funding.moc"
