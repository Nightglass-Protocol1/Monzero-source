#include "BchAdmissionTestFixture.h"
#include "../BchPreparationJob.h"
namespace {
struct PreparationFixture: Fixture {
    QString preparationPath,rpcPath,recoveryPath,candidatePath;
    bool create() {
        if (!Fixture::create()) return false;
        const QByteArray program=R"PY(#!/usr/bin/python3
import json,sys,pathlib,hashlib,time
root=pathlib.Path(__file__).parent
w=json.loads(pathlib.Path(sys.argv[2]).read_text())
rw=w if 'preparationConfigFile' in w else None
if rw:w=json.loads(pathlib.Path(rw['preparationConfigFile']).read_text())
f=json.loads(pathlib.Path(w['fundingConfigFile']).read_text())
c=json.loads(pathlib.Path(f['admissionConfigFile']).read_text())
t=json.loads(pathlib.Path(c['quoteFile']).read_text())['terms']
r=json.load(sys.stdin);action=sys.argv[1][10:-9];mode=(root/'mode').read_text()
assert action in ('prepare','preparation-status','lock-review','lock-submit','lock-status','signing-review','signing-submit','signing-status','signing-recovery-review','signing-recovery-submit'),action
assert r['schema']=='monzero-bch-funding-'+action+'-request-v1'
assert set(r)==({'schema','requestId','commitment'} if action in ('lock-submit','signing-submit','signing-recovery-submit') else {'schema','requestId'})
with (root/'calls').open('a') as out:out.write(action+'\n')
compact=lambda v:json.dumps(v,sort_keys=True,separators=(',',':')).encode()
d={'schema':'monzero-bch-wallet-preparation-review-v1','action':'inspect-unsigned-preparation',
'swapId':c['tradeId'],'quote':c['quoteCommitment'],'admission':'11'*32,'deployment':f['deploymentCommitment'],
'network':t['bchNetwork'],'genesis':t['bchGenesis'],'expiresAt':t['expiresAt'],'preparedAt':str(c['admittedAt']),
'preparation':'22'*32,'policy':w['policy'],'inputs':[{'outpoint':'33'*32+':0','valueAtoms':'3','script':w['policy']['change_script']}],
'outputs':[{'vout':0,'valueAtoms':'1','script':'51'},{'vout':1,'valueAtoms':'1','script':w['policy']['change_script']}],
'feeAtoms':'1','contractVout':0}
if mode=='quote':d['quote']='44'*32
if mode=='deployment':d['deployment']='44'*32
if mode=='fee':d['feeAtoms']='2'
if mode=='sum':d['inputs'][0]['valueAtoms']='4'
if mode=='duplicate':d['inputs']*=2
if mode=='input-script':d['inputs'][0]['script']='51'
if mode=='outpoint':d['inputs'][0]['outpoint']='33'*32+':00'
if mode=='principal':d['outputs'][0]['valueAtoms']='2'
if mode=='change':d['outputs'][1]['script']='52'
if mode=='vout':d['contractVout']=1
if mode=='time':d['preparedAt']=str(int(time.time())+3600)
if mode=='number':d['feeAtoms']=1
if mode=='leading-zero':d['feeAtoms']='01'
if mode=='policy':d['policy']['minimum_confirmations']=4
if mode=='detail-extra':d['extra']=True
review={'details':d,'commitment':hashlib.sha256(b'monzero-bch-wallet-preparation-review-v1\0'+compact(d)).hexdigest()}
v={'schema':'monzero-bch-funding-'+action+'-response-v1','requestId':r['requestId'],'tradeId':c['tradeId'],
'executable':False,'chainObserved':False,'review':review}
if action.startswith('lock-'):
 if action!='lock-status':
  ld={'schema':'monzero-bch-wallet-lock-review-v1','action':'lock-retained-wallet-inputs',
      'preparationReview':d,'preparationReviewCommitment':review['commitment']}
  review={'details':ld,'commitment':hashlib.sha256(b'monzero-bch-wallet-lock-review-v1\0'+compact(ld)).hexdigest()}
  v['review']=review
  if action=='lock-submit':assert r['commitment']==review['commitment']
 if mode=='review-change' and action=='lock-submit':
  ld['preparationReview']['inputs'][0]['outpoint']='44'*32+':0'
  ld['preparationReviewCommitment']=hashlib.sha256(b'monzero-bch-wallet-preparation-review-v1\0'+compact(ld['preparationReview'])).hexdigest()
  review['commitment']=hashlib.sha256(b'monzero-bch-wallet-lock-review-v1\0'+compact(ld)).hexdigest()
 if action=='lock-submit':
  (root/'lock-state').write_text('recorded')
  if mode=='lost':sys.exit(1)
 v['lockIntent']={'state':'recorded','commitment':'55'*32,'recordedAt':str(c['admittedAt'])} if (root/'lock-state').exists() else None
 if mode=='intent-missing':v['lockIntent']=None
 if mode=='intent-time':v['lockIntent']['recordedAt']='1'
 if mode=='intent-state':v['lockIntent']['state']='locked'
 if mode=='intent-pin':v['lockIntent']['commitment']='00'*32
 if mode=='intent-extra':v['lockIntent']['extra']=True
 if action=='lock-submit':
  v['chainObserved']=True
  v['observation']={'tip':'88'*32,'templateHash':'99'*32,'observedAt':str(int(time.time())),'walletInputsLocked':True}
  if mode=='lock-tip':v['observation']['tip']='00'*32
  if mode=='lock-template':v['observation']['templateHash']='00'*32
  if mode=='lock-time':v['observation']['observedAt']='1'
  if mode=='lock-future':v['observation']['observedAt']=str(int(time.time())+3600)
  if mode=='lock-flag':v['observation']['walletInputsLocked']='true'
  if mode=='lock-false':v['observation']['walletInputsLocked']=False
  if mode=='lock-extra':v['observation']['extra']=True
  if mode=='lock-chain':v['chainObserved']=False
if action.startswith('signing-'):
 sd={'schema':'monzero-bch-funding-signing-review-v1','action':'sign-only','swapId':c['tradeId'],
 'quote':list(bytes.fromhex(d['quote'])),'admission':[17]*32,'preparation':[34]*32,'walletLockIntent':[85]*32,
 'deployment':list(bytes.fromhex(d['deployment'])),'network':t['bchNetwork'],'genesis':t['bchGenesis'],
 'expiresAt':t['expiresAt'],'inputs':[{'txid':i['outpoint'].split(':')[0],'vout':int(i['outpoint'].split(':')[1]),'atoms':int(i['valueAtoms']),'script':i['script']} for i in d['inputs']],
 'outputs':[{'vout':o['vout'],'atoms':int(o['valueAtoms']),'script':o['script']} for o in d['outputs']],
 'contractVout':d['contractVout'],'feeAtoms':int(d['feeAtoms']),'changeAddress':w['policy']['change_address'],'minimumConfirmations':w['policy']['minimum_confirmations']}
 if mode=='sign-array':sd['admission'][0]=256
 if mode=='sign-change' and action=='signing-submit':sd['inputs'][0]['txid']='44'*32
 review={'details':sd,'commitment':hashlib.sha256(b'monzero-bch-funding-signing-review-v1\0'+compact(sd)).hexdigest()};v['review']=review
 if action=='signing-submit':
  (root/'sign-state').write_text('recorded')
  if mode=='lost':sys.exit(1)
  (root/'signed-state').write_text('signed')
 v['signingIntent']={'commitment':'66'*32,'approvedAt':str(c['admittedAt'])} if (root/'sign-state').exists() else None
 v['signedFunding']={'txid':'77'*32,'contractVout':0} if (root/'signed-state').exists() else None
 if mode=='sign-no-intent':v['signingIntent']=None
 if mode=='sign-time':v['signingIntent']['approvedAt']='1'
 if mode=='sign-result':v['signedFunding']['txid']='00'*32
 if mode=='sign-vout':v['signedFunding']['contractVout']=1
 if mode=='sign-no-result':v['signedFunding']=None
if rw:
 raw=pathlib.Path(rw['recoveredTransactionFile']).read_bytes()
 txid=hashlib.sha256(hashlib.sha256(raw).digest()).digest()[::-1].hex()
 rd={'schema':'monzero-bch-signing-reconciliation-v1','action':'retain-recovered-signature-only',
 'swapId':c['tradeId'],'admission':sd['admission'],'originalSigningIntent':[102]*32,'originalApprovedAt':c['admittedAt'],
 'signedBytes':list(hashlib.sha256(b'monzero-bch-signed-funding-v1\0'+raw).digest()),'fundingTxid':txid,'contractVout':0,'signingReview':sd}
 if mode=='recovery-bytes':rd['signedBytes']=[0]*32
 if mode=='recovery-txid':rd['fundingTxid']='00'*32
 if mode=='recovery-intent':rd['originalSigningIntent']=[0]*32
 if mode=='recovery-time':rd['originalApprovedAt']=1
 if mode=='recovery-review-change' and action=='signing-recovery-submit':rd['originalApprovedAt']+=1
 review={'details':rd,'commitment':hashlib.sha256(b'monzero-bch-signing-reconciliation-v1\0'+compact(rd)).hexdigest()};v['review']=review
 if action=='signing-recovery-submit':
  (root/'signed-state').write_text('signed');(root/'recovered-txid').write_text(txid)
  if mode=='lost':sys.exit(1)
  v['signedFunding']={'txid':txid,'contractVout':0}
 if mode=='recovery-review-change':v['signingIntent']['approvedAt']=str(rd['originalApprovedAt'])
 if mode=='recovery-file-change':pathlib.Path(rw['recoveredTransactionFile']).write_bytes(raw+b'changed')
elif (root/'recovered-txid').exists() and action.startswith('signing-'):
 v['signedFunding']={'txid':(root/'recovered-txid').read_text(),'contractVout':0}
if mode=='digest':review['commitment']='00'*32
if mode=='request':v['requestId']='wrong'
if mode=='execute':v['executable']=True
if mode=='chain':v['chainObserved']=True
if mode=='flag':v['chainObserved']='false'
if mode=='extra':v['extra']=True
if mode=='stderr':sys.stderr.write('unexpected')
if mode=='oversized':sys.stdout.write(' '*65537);sys.exit(0)
if mode=='replace':
 with pathlib.Path(__file__).open('a') as out:out.write('\n# changed\n')
if mode=='rpc-replace':
 with pathlib.Path(w['rpcFile']).open('a') as out:out.write(' ')
sys.stdout.buffer.write(compact(v)+(b'\n' if mode=='newline' else b''))
)PY";
        if (!save(helper,program,true)) return false;
        config["role"]="bch-funder";config["helperDigest"]=QString::fromLatin1(QCryptographicHash::hash(program,QCryptographicHash::Sha256).toHex());
        if (!writeConfig()) return false;
        const auto funding=directory.filePath("funding.json");
        if (!save(funding,QJsonDocument(QJsonObject{{"schema","monzero-private-bch-funding-v1"},
            {"admissionConfigFile",path},{"deploymentCommitment",QString(64,'5')}}).toJson(QJsonDocument::Compact))) return false;
        rpcPath=directory.filePath("rpc.json");preparationPath=directory.filePath("preparation.json");
        const QJsonObject policy{{"change_address","bchreg:qchange"},{"change_script","76a914"+QString(40,'3')+"88ac"},
            {"fee_rate_atoms_per_kb",1},{"maximum_fee",1},{"minimum_change",1},{"minimum_confirmations",3}};
        return save(preparationPath,QJsonDocument(QJsonObject{{"schema","monzero-private-bch-preparation-v1"},
            {"fundingConfigFile",funding},{"rpcFile",rpcPath},{"policy",policy}}).toJson(QJsonDocument::Compact));
    }
    bool recovery() {
        recoveryPath=directory.filePath("recovery.json");candidatePath=directory.filePath("candidate.bin");
        return save(candidatePath,QByteArray(20000,'x')) && save(directory.filePath("sign-state"),"recorded")
            && save(recoveryPath,QJsonDocument(QJsonObject{{"schema","monzero-private-bch-signing-recovery-v1"},
                {"preparationConfigFile",preparationPath},{"recoveredTransactionFile",candidatePath}}).toJson(QJsonDocument::Compact));
    }
    bool rpc() {
        return save(rpcPath,QJsonDocument(QJsonObject{{"schema","monzero-private-bch-funding-rpc-v1"},{"port",12345},
            {"wallet","test"},{"anchor","bchreg:qanchor"},{"anchorScript","51"},{"cookieFile",directory.filePath("absent")},
            {"checkpointHeight",1},{"checkpoint",QString(64,'7')},{"subversion","/Bitcoin Cash Node:29.0.0/"}}).toJson(QJsonDocument::Compact));
    }
};
}
class BchPreparationTests: public QObject {
    Q_OBJECT
private slots:
    void recoveredSigningRequiresExactCandidateApproval() {
        PreparationFixture f;QVERIFY(f.create());QVERIFY(f.recovery());BchPreparationJob job;QVERIFY(job.configure(f.preparationPath));
        QVERIFY(job.configureSigningRecovery(f.recoveryPath));QVERIFY(!QFile::exists(f.rpcPath));
        QVERIFY(!job.importSigningRecovery(QString(64,'1')));QVERIFY(job.reviewSigningRecovery());
        QTRY_VERIFY_WITH_TIMEOUT(!job.status().value("busy").toBool(),10000);
        QCOMPARE(job.status().value("state").toString(),QString("recovery-reviewed"));
        const auto approved=job.status().value("recoveryReview").toMap().value("commitment").toString();
        QVERIFY(!job.importSigningRecovery(QString(64,'0')));QVERIFY(job.importSigningRecovery(approved));
        QTRY_VERIFY_WITH_TIMEOUT(!job.status().value("busy").toBool(),10000);
        QCOMPARE(job.status().value("state").toString(),QString("signing-recovered"));
        QVERIFY(!job.status().value("chainObserved").toBool());QVERIFY(!job.importSigningRecovery(approved));
        QVERIFY(job.checkSigning());QTRY_VERIFY_WITH_TIMEOUT(!job.status().value("busy").toBool(),10000);
        QVERIFY(!job.status().value("signedFunding").toMap().isEmpty());
    }
    void changedCandidateConsumesRecoveryReview() {
        PreparationFixture f;QVERIFY(f.create());QVERIFY(f.recovery());BchPreparationJob job;QVERIFY(job.configure(f.preparationPath));
        QVERIFY(job.configureSigningRecovery(f.recoveryPath));QVERIFY(job.reviewSigningRecovery());
        QTRY_VERIFY_WITH_TIMEOUT(!job.status().value("busy").toBool(),10000);
        const auto approved=job.status().value("recoveryReview").toMap().value("commitment").toString();
        const auto calls=read(f.log);QVERIFY(save(f.candidatePath,"different bytes"));
        QVERIFY(!job.importSigningRecovery(approved));QCOMPARE(read(f.log),calls);
        QVERIFY(!job.status().value("recoverySubmitAvailable").toBool());
        QVERIFY(save(f.candidatePath,QByteArray(1000001,'x')));QVERIFY(!job.reviewSigningRecovery());QCOMPARE(read(f.log),calls);
    }
    void rejectsRecoveryEvidence_data() {
        QTest::addColumn<QString>("mode");
        for (const auto mode:{"recovery-bytes","recovery-txid","recovery-intent","recovery-time","recovery-review-change",
            "recovery-file-change","sign-array","sign-no-intent","sign-time","chain","digest","lost"})
            QTest::newRow(mode)<<QString(mode);
    }
    void rejectsRecoveryEvidence() {
        QFETCH(QString,mode);PreparationFixture f;QVERIFY(f.create());QVERIFY(f.recovery());BchPreparationJob job;QVERIFY(job.configure(f.preparationPath));
        QVERIFY(job.configureSigningRecovery(f.recoveryPath));QVERIFY(job.reviewSigningRecovery());
        QTRY_VERIFY_WITH_TIMEOUT(!job.status().value("busy").toBool(),10000);
        const auto approved=job.status().value("recoveryReview").toMap().value("commitment").toString();
        QVERIFY(save(f.mode,mode.toUtf8()));QVERIFY(job.importSigningRecovery(approved));
        QTRY_VERIFY_WITH_TIMEOUT(!job.status().value("busy").toBool(),10000);
        QCOMPARE(job.status().value("state").toString(),QString("unknown"));
        QVERIFY(!job.status().value("recoverySubmitAvailable").toBool());QVERIFY(!job.status().value("signingStatusKnown").toBool());
    }
    void signingRequiresReviewAndConsumesApproval() {
        PreparationFixture f;QVERIFY(f.create());QVERIFY(f.rpc());BchPreparationJob job;QVERIFY(job.configure(f.preparationPath));
        QVERIFY(!job.sign(QString(64,'1')));QVERIFY(job.reviewSigning());
        QTRY_VERIFY_WITH_TIMEOUT(!job.status().value("busy").toBool(),10000);
        QVERIFY(job.status().value("signingSubmitAvailable").toBool());
        const auto approved=job.status().value("signingReview").toMap().value("commitment").toString();
        QVERIFY(!job.sign(QString(64,'0')));QVERIFY(job.sign(approved));
        QTRY_VERIFY_WITH_TIMEOUT(!job.status().value("busy").toBool(),10000);
        QCOMPARE(job.status().value("state").toString(),QString("signed"));
        QVERIFY(!job.status().value("chainObserved").toBool());QVERIFY(!job.sign(approved));
        QVERIFY(QFile::remove(f.rpcPath));QVERIFY(job.checkSigning());
        QVERIFY(!job.status().value("signingStatusKnown").toBool());
        QTRY_VERIFY_WITH_TIMEOUT(!job.status().value("busy").toBool(),10000);
        QVERIFY(job.status().value("signingStatusKnown").toBool());
        QVERIFY(!job.status().value("signedFunding").toMap().isEmpty());
    }
    void uncertainSigningNeverOffersRetry() {
        PreparationFixture f;QVERIFY(f.create());QVERIFY(f.rpc());BchPreparationJob job;QVERIFY(job.configure(f.preparationPath));
        QVERIFY(job.reviewSigning());QTRY_VERIFY_WITH_TIMEOUT(!job.status().value("busy").toBool(),10000);
        const auto approved=job.status().value("signingReview").toMap().value("commitment").toString();
        QVERIFY(save(f.mode,"lost"));QVERIFY(job.sign(approved));
        QTRY_VERIFY_WITH_TIMEOUT(!job.status().value("busy").toBool(),10000);
        QCOMPARE(job.status().value("state").toString(),QString("unknown"));QVERIFY(!job.sign(approved));
        QVERIFY(job.reviewSigning());QTRY_VERIFY_WITH_TIMEOUT(!job.status().value("busy").toBool(),10000);
        QVERIFY(job.status().value("signingStatusKnown").toBool());
        QVERIFY(!job.status().value("signingIntent").toMap().isEmpty());
        QVERIFY(job.status().value("signedFunding").toMap().isEmpty());
        QVERIFY(!job.status().value("signingSubmitAvailable").toBool());QVERIFY(!job.sign(approved));
    }
    void rejectsSigningEvidence_data() {
        QTest::addColumn<QString>("mode");
        for (const auto mode:{"sign-array","sign-change","sign-no-intent","sign-time","sign-result","sign-vout","sign-no-result",
            "quote","deployment","fee","sum","duplicate","input-script","change","principal","digest","chain","rpc-replace"})
            QTest::newRow(mode)<<QString(mode);
    }
    void rejectsSigningEvidence() {
        QFETCH(QString,mode);PreparationFixture f;QVERIFY(f.create());QVERIFY(f.rpc());BchPreparationJob job;QVERIFY(job.configure(f.preparationPath));
        QVERIFY(job.reviewSigning());QTRY_VERIFY_WITH_TIMEOUT(!job.status().value("busy").toBool(),10000);
        const auto approved=job.status().value("signingReview").toMap().value("commitment").toString();
        QVERIFY(save(f.mode,mode.toUtf8()));QVERIFY(job.sign(approved));
        QTRY_VERIFY_WITH_TIMEOUT(!job.status().value("busy").toBool(),10000);
        QCOMPARE(job.status().value("state").toString(),QString("unknown"));
        QVERIFY(!job.status().value("signingStatusKnown").toBool());QVERIFY(!job.sign(approved));
    }
    void readsWithoutRpcAndClearsFailure() {
        PreparationFixture f;QVERIFY(f.create());BchPreparationJob job;QVERIFY(job.configure(f.preparationPath));
        QVERIFY(!QFile::exists(f.log));QVERIFY(job.refresh());QVERIFY(!job.refresh());
        QTRY_VERIFY_WITH_TIMEOUT(!job.status().value("busy").toBool(),10000);
        QCOMPARE(job.status().value("state").toString(),QString("prepared"));
        QVERIFY(!job.status().value("result").toMap().isEmpty());QVERIFY(!QFile::exists(f.rpcPath));
        QVERIFY(save(f.mode,"digest"));QVERIFY(job.refresh());
        QVERIFY(job.status().value("result").toMap().isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(!job.status().value("busy").toBool(),10000);
        QCOMPARE(job.status().value("state").toString(),QString("unknown"));
    }
    void expiredQuoteAllowsOnlySavedStatus() {
        PreparationFixture f;QVERIFY(f.create());
        auto record=QJsonDocument::fromJson(read(f.config.value("quoteFile").toString())).object();
        auto terms=record.value("terms").toObject();const auto now=QDateTime::currentSecsSinceEpoch();
        terms["expiresAt"]=QString::number(now-3600);record["terms"]=terms;
        record["admittedAt"]=now-7200;f.config["admittedAt"]=now-7200;
        QByteArray binding("monzero-native-bch-xmz-quote-v1");binding.append('\0');binding+=QJsonDocument(terms).toJson(QJsonDocument::Compact);
        const auto digest=QString::fromLatin1(QCryptographicHash::hash(binding,QCryptographicHash::Sha256).toHex());
        record["commitment"]=digest;f.config["quoteCommitment"]=digest;
        QVERIFY(save(f.config.value("quoteFile").toString(),QJsonDocument(record).toJson(QJsonDocument::Compact)));QVERIFY(f.writeConfig());
        BchPreparationJob job;QVERIFY(job.configure(f.preparationPath));QVERIFY(!job.prepare());QVERIFY(!QFile::exists(f.log));
        QVERIFY(job.refresh());QTRY_VERIFY_WITH_TIMEOUT(!job.status().value("busy").toBool(),10000);
        QCOMPARE(job.status().value("state").toString(),QString("prepared"));
        QVERIFY(!job.reviewSigning());QVERIFY(job.checkSigning());
        QTRY_VERIFY_WITH_TIMEOUT(!job.status().value("busy").toBool(),10000);
        QCOMPARE(job.status().value("state").toString(),QString("signing-status"));
        QVERIFY(!job.reviewLocks());QVERIFY(job.checkLocks());
        QTRY_VERIFY_WITH_TIMEOUT(!job.status().value("busy").toBool(),10000);
        QCOMPARE(job.status().value("state").toString(),QString("lock-status"));
        QVERIFY(f.recovery());QVERIFY(job.configureSigningRecovery(f.recoveryPath));QVERIFY(job.reviewSigningRecovery());
        QTRY_VERIFY_WITH_TIMEOUT(!job.status().value("busy").toBool(),10000);
        QCOMPARE(job.status().value("state").toString(),QString("recovery-reviewed"));
        const auto approved=job.status().value("recoveryReview").toMap().value("commitment").toString();
        QVERIFY(job.importSigningRecovery(approved));QTRY_VERIFY_WITH_TIMEOUT(!job.status().value("busy").toBool(),10000);
        QCOMPARE(job.status().value("state").toString(),QString("signing-recovered"));
    }
    void prepareRequiresDescriptor() {
        PreparationFixture f;QVERIFY(f.create());BchPreparationJob job;QVERIFY(job.configure(f.preparationPath));
        QVERIFY(!job.prepare());QVERIFY(!QFile::exists(f.log));QVERIFY(f.rpc());QVERIFY(job.prepare());
        QTRY_VERIFY_WITH_TIMEOUT(!job.status().value("busy").toBool(),10000);
        QCOMPARE(job.status().value("state").toString(),QString("prepared"));
    }
    void locksRequireFreshReviewAndClearEvidence() {
        PreparationFixture f;QVERIFY(f.create());QVERIFY(f.rpc());BchPreparationJob job;QVERIFY(job.configure(f.preparationPath));
        QVERIFY(!job.lockInputs(QString(64,'1')));QVERIFY(!QFile::exists(f.log));
        QVERIFY(job.reviewLocks());QTRY_VERIFY_WITH_TIMEOUT(!job.status().value("busy").toBool(),10000);
        QVERIFY(job.status().value("lockSubmitAvailable").toBool());
        const auto commitment=job.status().value("lockReview").toMap().value("commitment").toString();
        QVERIFY(!job.lockInputs(QString(64,'0')));QVERIFY(job.lockInputs(commitment));
        QTRY_VERIFY_WITH_TIMEOUT(!job.status().value("busy").toBool(),10000);
        QCOMPARE(job.status().value("state").toString(),QString("locks-checked"));
        QVERIFY(job.status().value("chainObserved").toBool());QVERIFY(!job.lockInputs(commitment));
        QVERIFY(QFile::remove(f.rpcPath));QVERIFY(job.checkLocks());
        QVERIFY(job.status().value("lockObservation").toMap().isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(!job.status().value("busy").toBool(),10000);
        QVERIFY(job.status().value("lockStatusKnown").toBool());QVERIFY(!job.status().value("chainObserved").toBool());
        QVERIFY(f.rpc());QVERIFY(job.reviewLocks());QTRY_VERIFY_WITH_TIMEOUT(!job.status().value("busy").toBool(),10000);
        QVERIFY(save(f.mode,"lost"));QVERIFY(job.lockInputs(commitment));
        QTRY_VERIFY_WITH_TIMEOUT(!job.status().value("busy").toBool(),10000);
        QCOMPARE(job.status().value("state").toString(),QString("unknown"));QVERIFY(!job.lockInputs(commitment));
        QVERIFY(job.status().value("lockObservation").toMap().isEmpty());
        QVERIFY(!job.status().value("lockStatusKnown").toBool());
    }
    void rejectsLockEvidence_data() {
        QTest::addColumn<QString>("mode");
        for (const auto mode:{"intent-missing","intent-time","intent-state","intent-pin","intent-extra","lock-tip","lock-template",
            "lock-time","lock-future","lock-flag","lock-false","lock-extra","lock-chain","rpc-replace","review-change"}) QTest::newRow(mode)<<QString(mode);
    }
    void rejectsLockEvidence() {
        QFETCH(QString,mode);PreparationFixture f;QVERIFY(f.create());QVERIFY(f.rpc());BchPreparationJob job;QVERIFY(job.configure(f.preparationPath));
        QVERIFY(job.reviewLocks());QTRY_VERIFY_WITH_TIMEOUT(!job.status().value("busy").toBool(),10000);
        const auto commitment=job.status().value("lockReview").toMap().value("commitment").toString();
        QVERIFY(save(f.mode,mode.toUtf8()));QVERIFY(job.lockInputs(commitment));
        QTRY_VERIFY_WITH_TIMEOUT(!job.status().value("busy").toBool(),10000);
        QCOMPARE(job.status().value("state").toString(),QString("unknown"));QVERIFY(job.status().value("lockObservation").toMap().isEmpty());
        QVERIFY(!job.status().value("lockSubmitAvailable").toBool());
    }
    void rejectsResponse_data() {
        QTest::addColumn<QString>("mode");
        for (const auto mode:{"quote","deployment","fee","sum","duplicate","input-script","outpoint","principal","change", "vout",
            "time","number","leading-zero","policy","detail-extra","digest","request","execute","chain","flag","extra",
            "stderr","oversized","replace","newline","rpc-replace"}) QTest::newRow(mode)<<QString(mode);
    }
    void rejectsResponse() {
        QFETCH(QString,mode);PreparationFixture f;QVERIFY(f.create());QVERIFY(f.rpc());QVERIFY(save(f.mode,mode.toUtf8()));
        BchPreparationJob job;QVERIFY(job.configure(f.preparationPath));QVERIFY(job.prepare());
        QTRY_VERIFY_WITH_TIMEOUT(!job.status().value("busy").toBool(),10000);
        QCOMPARE(job.status().value("state").toString(),QString("unknown"));QVERIFY(job.status().value("result").toMap().isEmpty());
    }
    void rejectsChangedSource() {
        PreparationFixture f;QVERIFY(f.create());BchPreparationJob job;QVERIFY(job.configure(f.preparationPath));
        QVERIFY(save(f.preparationPath,read(f.preparationPath)+" "));QVERIFY(!job.refresh());QVERIFY(!QFile::exists(f.log));
        QVERIFY(!job.status().value("prepareAvailable").toBool());
    }
};
QTEST_GUILESS_MAIN(BchPreparationTests)
#include "test_bch_preparation.moc"
