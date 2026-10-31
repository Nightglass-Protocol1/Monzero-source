#include "../BchFundingReceiptJob.h"
#include "../LitecoinJournalProtocol.h"
#include "../BchFundingMessageFile.h"
#include "BchAdmissionTestFixture.h"
namespace {
struct ReceiptFixture:Fixture {
    QString wrapper,message;
    bool create() {
        if (!Fixture::create()) return false;
        wrapper=directory.filePath("funding-receipt.json");message=directory.filePath("peer.json");
        const QByteArray program=R"PY(#!/usr/bin/python3
import hashlib,json,pathlib,sys,time
compact=lambda x:json.dumps(x,sort_keys=True,separators=(',',':')).encode()
digest=lambda domain,x:hashlib.sha256(domain.encode()+b'\0'+x).hexdigest()
root=pathlib.Path(sys.argv[2]).parent
wrapper=json.loads(pathlib.Path(sys.argv[2]).read_text())
c=json.loads(pathlib.Path(wrapper['admissionConfigFile']).read_text())
t=json.loads(pathlib.Path(c['quoteFile']).read_text())['terms']
mode=(root/'mode').read_text()
r=json.load(sys.stdin);action=r['schema'].split('-')[-3]
with (root/'calls').open('a') as f:f.write(action+'\n')
review=json.loads((root/'saved-review').read_text()) if (root/'saved-review').exists() else None
if action!='status':
 m=json.loads(pathlib.Path(wrapper['peerMessageFile']).read_text())
 raw=bytes(m['frame']);frame=json.loads(raw);signed=bytes(frame['signed'])
 txid=hashlib.sha256(hashlib.sha256(signed).digest()).digest()[::-1].hex()
 d={'schema':'monzero-bch-funding-receipt-review-v1','action':'accept-bch-funding-message',
 'swapId':c['tradeId'],'quote':list(bytes.fromhex(c['quoteCommitment'])),'admission':[170]*32,
 'messageCommitment':list(bytes.fromhex(digest('monzero-bch-authenticated-funding-v1',raw+bytes(m['signature'])))),
 'bchNetwork':t['bchNetwork'],'bchGenesis':t['bchGenesis'],'fundingOutpoint':txid+':0',
 'contractScript':'aa20'+'bb'*32+'87','principalAtoms':t['bchAtoms'],'feeAtoms':'1'}
 review={'details':d,'commitment':digest(d['schema'],compact(d))}
if action=='submit':
 if r.get('commitment')!=review['commitment']:sys.exit(1)
 (root/'saved').write_text(str(int(time.time())))
 (root/'saved-review').write_bytes(compact(review))
 if mode=='lost':sys.exit(1)
receipt={'commitment':'bb'*32,'receivedAt':(root/'saved').read_text()} if (root/'saved').exists() else None
value={'schema':'monzero-bch-funding-receipt-'+action+'-response-v1','requestId':r['requestId'],
 'tradeId':c['tradeId'],'executable':False,'chainObserved':False,'review':review,'receipt':receipt}
if mode=='request':value['requestId']='00000000-0000-4000-8000-000000000001'
if mode=='trade':value['tradeId']='00000000-0000-4000-8000-000000000001'
if mode=='extra':value['extra']=1
if mode=='execute':value['executable']=True
if mode=='chain':value['chainObserved']=True
if mode=='flag':value['chainObserved']='false'
if mode=='frame':value['review']['details']['messageCommitment']=[0]*32
if mode=='amount':value['review']['details']['principalAtoms']='2'
if mode in ('frame','amount'):
 value['review']['commitment']=digest(value['review']['details']['schema'],compact(value['review']['details']))
if mode=='digest':value['review']['commitment']='00'*32
if mode=='future':value['receipt']={'commitment':'bb'*32,'receivedAt':str(int(time.time())+3600)}
if mode=='early':value['receipt']={'commitment':'bb'*32,'receivedAt':str(c['admittedAt']-1)}
if mode=='stderr':sys.stderr.write('unexpected')
if mode=='oversized':sys.stdout.write(' '*16385);sys.exit(0)
sys.stdout.buffer.write(compact(value)+(b'\n' if mode=='newline' else b''))
)PY";
        if (!save(helper,program,true)) return false;
        config["role"]="native-funder";
        config["helperDigest"]=QString::fromLatin1(QCryptographicHash::hash(program,QCryptographicHash::Sha256).toHex());
        QJsonArray quote,agreement,signature;
        for (auto b:QByteArray::fromHex(config.value("quoteCommitment").toString().toLatin1())) quote.append(int(static_cast<unsigned char>(b)));
        for (int i=0;i<32;++i) agreement.append(7);
        for (int i=0;i<64;++i) signature.append(8);
        QJsonObject frame{{"schema","monzero-bch-funding-message-v1"},{"quote",quote},{"agreement",agreement},
            {"swap",config.value("tradeId")},{"signed",QJsonArray{1,2,3}},{"unsigned",QJsonArray{4,5,6}},
            {"sources",QJsonArray{QJsonArray{7,8,9}}},{"change",QJsonArray{81}},{"maximum_fee",1},{"minimum_change",1}};
        QJsonArray raw;for (auto b:QJsonDocument(frame).toJson(QJsonDocument::Compact)) raw.append(int(static_cast<unsigned char>(b)));
        return writeConfig() && save(wrapper,QJsonDocument(QJsonObject{{"schema","monzero-private-bch-funding-receipt-v1"},
            {"admissionConfigFile",path},{"peerMessageFile",message}}).toJson(QJsonDocument::Compact))
            && save(message,QJsonDocument(QJsonObject{{"schema","monzero-bch-peer-funding-receipt-v1"},
                {"frame",raw},{"signature",signature}}).toJson(QJsonDocument::Compact));
    }
};
}
#define WAIT_JOB(job) QTRY_VERIFY_WITH_TIMEOUT(!(job).status().value("busy").toBool(),10000)
class BchFundingReceiptTests:public QObject {
    Q_OBJECT
private slots:
    void realHelperRequiresSavedNegotiation() {
        const auto path=qEnvironmentVariable("MONZERO_BCH_QT_FIXTURE");
        if (path.isEmpty()) QSKIP("Requires the independent signed-quote subprocess fixture");
        QTemporaryDir directory;QVERIFY(directory.isValid());
        auto config=QJsonDocument::fromJson(read(path)).object();
        QVERIFY(!config.isEmpty());config["role"]="native-funder";
        const auto admission=directory.filePath("admission.json");
        const auto wrapper=directory.filePath("funding-receipt.json");
        QVERIFY(save(admission,QJsonDocument(config).toJson(QJsonDocument::Compact)));
        QVERIFY(save(wrapper,QJsonDocument(QJsonObject{
            {"schema","monzero-private-bch-funding-receipt-v1"},
            {"admissionConfigFile",admission},
            {"peerMessageFile",directory.filePath("absent-peer.json")}
        }).toJson(QJsonDocument::Compact)));
        BchFundingReceiptJob job;QVERIFY(job.configure(wrapper));
        QVERIFY(!job.review());QVERIFY(job.refresh());WAIT_JOB(job);
        QCOMPARE(job.status().value("state").toString(),QString("unknown"));
        QVERIFY(!job.status().value("checked").toBool());
        QVERIFY(!job.status().value("submitAvailable").toBool());
        QVERIFY(job.status().value("receipt").toMap().isEmpty());
        QVERIFY(!job.review());QVERIFY(!job.submit(QString(64,'0')));
        QVERIFY(!QFileInfo::exists(config.value("metadataDirectory").toString()));
        QVERIFY(!QFileInfo::exists(config.value("snapshotDirectory").toString()));
    }
    void explicitStatusReviewAndAcceptance() {
        ReceiptFixture f;QVERIFY(f.create());BchFundingReceiptJob job;QVERIFY(job.configure(f.wrapper));
        QVERIFY(!job.review());QVERIFY(!job.submit(QString(64,'0')));QVERIFY(read(f.log).isEmpty());
        QVERIFY(job.refresh());WAIT_JOB(job);QCOMPARE(job.status().value("state").toString(),QString("not-saved"));
        QVERIFY(job.review());WAIT_JOB(job);QCOMPARE(job.status().value("state").toString(),QString("reviewed"));
        const auto digest=job.status().value("review").toMap().value("commitment").toString();
        QVERIFY(!job.submit(QString(64,'0')));QVERIFY(job.submit(digest));WAIT_JOB(job);
        QCOMPARE(job.status().value("state").toString(),QString("saved"));QVERIFY(!job.review());QVERIFY(!job.submit(digest));
        QVERIFY(QFile::remove(f.message));QVERIFY(job.refresh());WAIT_JOB(job);
        QCOMPARE(job.status().value("state").toString(),QString("saved"));
        QCOMPARE(read(f.log),QByteArray("status\nreview\nsubmit\nstatus\n"));
    }
    void invalidReview_data() {
        QTest::addColumn<QString>("mode");
        for (const auto value:{"request","trade","extra","execute","chain","flag","frame","amount","digest","future","early","stderr","oversized","newline"})
            QTest::newRow(value)<<QString(value);
    }
    void invalidReview() {
        QFETCH(QString,mode);ReceiptFixture f;QVERIFY(f.create());BchFundingReceiptJob job;QVERIFY(job.configure(f.wrapper));
        QVERIFY(job.refresh());WAIT_JOB(job);QVERIFY(save(f.mode,mode.toUtf8()));QVERIFY(job.review());WAIT_JOB(job);
        QCOMPARE(job.status().value("state").toString(),QString("unknown"));
        QVERIFY(job.status().value("review").toMap().isEmpty());QVERIFY(job.status().value("receipt").toMap().isEmpty());
        QVERIFY(!job.status().value("submitAvailable").toBool());QVERIFY(!job.review());
    }
    void changedPeerMessageInvalidatesApproval() {
        ReceiptFixture f;QVERIFY(f.create());BchFundingReceiptJob job;QVERIFY(job.configure(f.wrapper));
        QVERIFY(job.refresh());WAIT_JOB(job);QVERIFY(job.review());WAIT_JOB(job);
        const auto digest=job.status().value("review").toMap().value("commitment").toString();
        QVERIFY(save(f.message,"{}"));QVERIFY(!job.submit(digest));
        QCOMPARE(read(f.log),QByteArray("status\nreview\n"));QVERIFY(job.status().value("review").toMap().isEmpty());
        QVERIFY(job.refresh());WAIT_JOB(job);QVERIFY(!job.review());
    }
    void lostReplyRequiresSavedStatusWithoutRetry() {
        ReceiptFixture f;QVERIFY(f.create());BchFundingReceiptJob job;QVERIFY(job.configure(f.wrapper));
        QVERIFY(job.refresh());WAIT_JOB(job);QVERIFY(job.review());WAIT_JOB(job);
        const auto digest=job.status().value("review").toMap().value("commitment").toString();
        QVERIFY(save(f.mode,"lost"));QVERIFY(job.submit(digest));WAIT_JOB(job);
        QCOMPARE(job.status().value("state").toString(),QString("unknown"));QVERIFY(!job.review());QVERIFY(!job.submit(digest));
        QVERIFY(QFile::remove(f.message));QVERIFY(save(f.mode,"normal"));QVERIFY(job.refresh());WAIT_JOB(job);
        QCOMPARE(job.status().value("state").toString(),QString("saved"));
    }
    void changedConfigurationRefusesHelper() {
        ReceiptFixture f;QVERIFY(f.create());BchFundingReceiptJob job;QVERIFY(job.configure(f.wrapper));
        QVERIFY(save(f.wrapper,"{}"));QVERIFY(!job.refresh());QVERIFY(read(f.log).isEmpty());
        QCOMPARE(job.status().value("state").toString(),QString("unknown"));
    }
    void boundedPrivateReader() {
        ReceiptFixture f;QVERIFY(f.create());const auto path=f.directory.filePath("large");
        QVERIFY(save(path,QByteArray(32000,'a')));QVERIFY(LitecoinJournalProtocol::privateConfig(path).isEmpty());
        QCOMPARE(LitecoinJournalProtocol::privateConfig(path,65536).size(),32000);
        QVERIFY(LitecoinJournalProtocol::privateConfig(path,65537).isEmpty());
        QVERIFY(save(path,QByteArray(65537,'a')));QVERIFY(LitecoinJournalProtocol::privateConfig(path,65536).isEmpty());
        QCOMPARE(BchFundingMessageFile::read(path).size(),65537);
        QVERIFY(save(path,QByteArray(6004097,'a')));QVERIFY(BchFundingMessageFile::read(path).isEmpty());
        QVERIFY(save(path,"private-message"));
        QVERIFY(QFile::setPermissions(path,QFile::ReadOwner|QFile::WriteOwner|QFile::ReadOther));
        QVERIFY(BchFundingMessageFile::read(path).isEmpty());
    }
};
QTEST_GUILESS_MAIN(BchFundingReceiptTests)
#include "test_bch_funding_receipt.moc"
