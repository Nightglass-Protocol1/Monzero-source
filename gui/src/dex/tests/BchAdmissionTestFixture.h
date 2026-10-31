#pragma once
#include "../BchAdmissionJob.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtTest/QtTest>

namespace {
bool save(const QString &path,const QByteArray &bytes,bool executable=false) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly|QIODevice::Truncate)
        && file.setPermissions(QFile::ReadOwner|QFile::WriteOwner|(executable?QFile::ExeOwner:QFile::Permissions()))
        && file.write(bytes)==bytes.size() && file.flush();
}
QByteArray read(const QString &path) {QFile file(path);if (!file.open(QIODevice::ReadOnly)) return {};return file.readAll();}
struct Fixture {
    QTemporaryDir directory;
    QString path,helper,mode,log;
    QJsonObject config;
    bool create() {
        if (!directory.isValid()) return false;
        path=directory.filePath("config.json");helper=directory.filePath("helper.py");
        mode=directory.filePath("mode");log=directory.filePath("calls");
        const QByteArray program=R"PY(#!/usr/bin/python3
import hashlib,json,pathlib,sys,time
request=json.load(sys.stdin)
c=json.loads(pathlib.Path(sys.argv[2]).read_text())
root=pathlib.Path(sys.argv[2]).parent
mode=(root/'mode').read_text()
action=request['schema'].split('-')[-3]
with (root/'calls').open('a') as f:f.write(action+'\n')
if mode=='lost' and action=='submit':sys.exit(1)
terms=json.loads(pathlib.Path(c['quoteFile']).read_text())['terms']
policy={}
for key,value in c['policy'].items():
 parts=key.split('_');camel=parts[0]+''.join(p[0].upper()+p[1:] for p in parts[1:])
 policy[camel]=str(value) if key.startswith(('maximum_','minimum_')) else value
details={'schema':'monzero-bch-execution-admission-review-v1','action':'admit-bch-execution',
 'role':c['role'],'quoteCommitment':c['quoteCommitment'],'terms':terms,
 'negotiationPins':{k:bytes(v).hex() for k,v in c['negotiationPins'].items()},'policy':policy,
 'chainObserved':False,'negotiationVerified':False,'executable':False}
compact=lambda v:json.dumps(v,sort_keys=True,separators=(',',':')).encode()
digest=hashlib.sha256(b'MONZERO-BCH-EXECUTION-ADMISSION-APPROVAL-V1\0'+compact(details)).hexdigest()
value={'schema':'monzero-bch-admission-'+action+'-response-v1','requestId':request['requestId'],
 'tradeId':c['tradeId'],'admitted':action!='review','executable':False,
 'review':{'details':details,'commitment':digest}}
if action=='status':value['admittedAt']=c['admittedAt']
if mode=='request':value['requestId']='00000000-0000-4000-8000-000000000001'
if mode=='trade':value['tradeId']='00000000-0000-4000-8000-000000000001'
if mode=='terms':value['review']['details']['terms']['bchAtoms']='2'
if mode=='digest':value['review']['commitment']='00'*32
if mode=='extra':value['extra']=True
if mode=='execute':value['executable']=True
if mode=='flag':value['admitted']='false'
if mode=='future':value['admittedAt']=int(time.time())+3600
if mode=='early':value['admittedAt']=c['admittedAt']-1
if mode=='stderr':sys.stderr.write('unexpected output')
if mode=='oversized':sys.stdout.write(' '*16385);sys.exit(0)
if mode=='replace':
 with pathlib.Path(__file__).open('a') as f:f.write('\n# changed\n')
sys.stdout.buffer.write(compact(value)+(b'\n' if mode=='newline' else b''))
)PY";
        if (!save(helper,program,true) || !save(mode,"normal")) return false;
        const auto now=QDateTime::currentSecsSinceEpoch();const auto trade=QUuid::createUuid().toString(QUuid::WithoutBraces);
        QJsonObject terms;
        for (const auto key:{"quoteId","bchAtoms","xmzAtoms","xmzPayoutAtoms","bchPayoutScript","bchRefundScript",
            "xmzPayout","xmzRefund","bchSigningKey","xmzSigningKey","bchFundingFeeMaxAtoms","bchLockFeeAtoms",
            "bchRecoveryFeeAtoms","xmzFundingFeeMaxAtoms","xmzClaimFeeMaxAtoms","xmzRefundFeeMaxAtoms",
            "bchConfirmations","xmzConfirmations","cancelBlocks","punishBlocks","bchMinimumOutputAtoms","appFeeXmzAtoms"}) terms[key]="1";
        terms["schema"]="monzero-native-bch-xmz-quote-v1";terms["swapId"]=trade;
        terms["bchFunder"]=QString::fromLatin1(QByteArray(32,1).toHex());
        terms["xmzFunder"]=QString::fromLatin1(QByteArray(32,2).toHex());
        terms["expiresAt"]=QString::number(now+3600);
        QJsonObject policy{{"bch_network","bch-regtest"},{"bch_genesis",QString(64,'b')},
            {"xmz_network","xmz-mainnet"},{"xmz_genesis",QString(64,'a')}};
        terms["bchNetwork"]=policy["bch_network"];terms["bchGenesis"]=policy["bch_genesis"];
        terms["xmzNetwork"]=policy["xmz_network"];terms["xmzGenesis"]=policy["xmz_genesis"];
        for (const auto key:{"bch_confirmations","xmz_confirmations","cancel_blocks","punish_blocks",
            "maximum_bch_atoms","maximum_xmz_atoms","maximum_bch_funding_fee","maximum_xmz_fee",
            "minimum_covenant_fee","maximum_covenant_fee","minimum_output_atoms"}) policy[key]=100;
        QByteArray binding("monzero-native-bch-xmz-quote-v1");binding.append('\0');binding+=QJsonDocument(terms).toJson(QJsonDocument::Compact);
        const auto commitment=QString::fromLatin1(QCryptographicHash::hash(binding,QCryptographicHash::Sha256).toHex());
        QJsonArray first,second;for (int i=0;i<32;++i) {first.append(1);second.append(2);}
        QJsonObject pins;for (const auto key:{"initial","selection","outgoing","received","agreement"}) pins[key]=first;
        const auto quote=directory.filePath("quote.json");
        const QJsonObject record{{"schema","monzero-bch-quote-record-v1"},{"admittedAt",now},
            {"commitment",commitment},{"terms",terms},{"bchSignature",QString(128,'a')},{"xmzSignature",QString(128,'b')}};
        config={{"schema","monzero-private-bch-admission-v1"},{"helper",helper},
            {"helperDigest",QString::fromLatin1(QCryptographicHash::hash(program,QCryptographicHash::Sha256).toHex())},
            {"quoteFile",quote},{"quoteCommitment",commitment},{"admittedAt",now},{"tradeId",trade},
            {"identityPins",QJsonArray{first,second}},{"policy",policy},{"role","native-funder"},
            {"metadataDirectory",directory.filePath("metadata")},{"snapshotDirectory",directory.filePath("snapshots")},
            {"negotiationPins",pins}};
        return save(quote,QJsonDocument(record).toJson(QJsonDocument::Compact)) && writeConfig();
    }
    bool writeConfig() {return save(path,QJsonDocument(config).toJson(QJsonDocument::Compact));}
};
}
