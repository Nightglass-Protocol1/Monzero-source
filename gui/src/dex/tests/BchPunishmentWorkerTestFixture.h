#pragma once
#include "BchPunishmentTestFixture.h"
namespace {
struct PunishmentWorkerContext:PunishContext {
    QString wrapper;
    QString observationPath,rpcPath,submissionPath,nativeRpcPath;
    bool submissionFiles() {
        if (!observationFiles()) return false;
        nativeRpcPath=directory.filePath("native-rpc.json");submissionPath=directory.filePath("submit.json");
        return save(nativeRpcPath,QJsonDocument(QJsonObject{{"schema","monzero-private-bch-native-rpc-v1"},
            {"daemonPort",28081},{"walletPort",28082},{"loginFile",directory.filePath("absent-login")}}).toJson(QJsonDocument::Compact))
            && save(submissionPath,QJsonDocument(QJsonObject{{"schema","monzero-private-bch-punish-submission-v1"},
                {"punishConfigFile",wrapper},{"rpcFile",rpcPath},{"nativeRpcFile",nativeRpcPath},{"bchObserverKind","owned-node"}}).toJson(QJsonDocument::Compact));
    }
    bool observationFiles() {
        const QString genesis="0f9188f13cb7b2c71f2a335e3a4fc328bf5beb436012afca590b1a11466e2206";
        auto terms=record.value("terms").toObject();terms["bchGenesis"]=genesis;record["terms"]=terms;
        auto policy=config.value("policy").toObject();policy["bch_genesis"]=genesis;config["policy"]=policy;
        details["bchGenesis"]=genesis;bindQuote();
        const QJsonObject backend{{"schema","monzero-bch-chain-observer-v1"},{"host","127.0.0.1"},{"port",18443},
            {"network","bch-regtest"},{"genesis",genesis},{"checkpointHeight",1},{"checkpoint",QString(64,'7')},
            {"subversion","/Bitcoin Cash Node:29.0.0/"}};
        const auto digest=QCryptographicHash::hash(QJsonDocument(backend).toJson(QJsonDocument::Compact),QCryptographicHash::Sha256);
        deployment=QString::fromLatin1(digest.toHex());QJsonArray pin;
        for (const auto byte:digest) pin.append(int(static_cast<unsigned char>(byte)));details["bchDeployment"]=pin;
        rpcPath=directory.filePath("rpc.json");observationPath=directory.filePath("observe.json");
        const QJsonObject rpc{{"schema","monzero-private-bch-observer-rpc-v1"},{"port",18443},
            {"cookieFile",directory.filePath("absent-cookie")},{"checkpointHeight",1},{"checkpoint",QString(64,'7')},
            {"subversion","/Bitcoin Cash Node:29.0.0/"}};
        return writeConfig() && save(config.value("quoteFile").toString(),QJsonDocument(record).toJson(QJsonDocument::Compact))
            && save(directory.filePath("review.json"),QJsonDocument(bound(details)).toJson(QJsonDocument::Compact))
            && save(wrapper,QJsonDocument(QJsonObject{{"schema","monzero-private-bch-punish-v1"},
                {"admissionConfigFile",path},{"deploymentCommitment",deployment}}).toJson(QJsonDocument::Compact))
            && save(rpcPath,QJsonDocument(rpc).toJson(QJsonDocument::Compact))
            && save(observationPath,QJsonDocument(QJsonObject{{"schema","monzero-private-bch-punish-observation-v1"},
                {"punishConfigFile",wrapper},{"rpcFile",rpcPath},{"bchObserverKind","owned-node"}}).toJson(QJsonDocument::Compact));
    }
    bool create() {
        if (!PunishContext::create()) return false;
        const QByteArray program=R"PY(#!/usr/bin/python3
import hashlib,json,pathlib,sys,time
r=json.load(sys.stdin);root=pathlib.Path(sys.argv[2]).parent
mode=(root/'mode').read_text();review=json.loads((root/'review.json').read_text())
c=json.loads((root/'config.json').read_text())
action=sys.argv[1].split('-')[3]
with (root/'calls').open('a') as f:f.write(sys.argv[1]+'\n')
v={'schema':'monzero-bch-punish-'+action+'-response-v1','requestId':r['requestId'],'tradeId':c['tradeId'],
'executable':False,'chainObserved':False,'review':review,'relay':{'state':'not-attempted','txid':review['details']['txid'],'confirmed':False},'observation':None}
if action=='submit':
 assert r['commitment']==review['commitment']
 v['relay']['state']='acknowledged'
 if mode=='lost':sys.exit(1)
 if mode=='overflow-hang':sys.stdout.write(' '*16385);sys.stdout.flush();time.sleep(10);sys.exit(0)
 if mode=='rpc-replace':
  with (root/'native-rpc.json').open('a') as f:f.write(' ')
if action=='observe':
 v['chainObserved']=True;v['relay']['state']='attempted'
 v['observation']={'state':'confirmed','txid':review['details']['txid'],'block':'a'*64,'tip':'b'*64,'height':'12','confirmations':'3','observedAt':str(int(time.time()))}
 if mode=='stale':v['observation']['observedAt']='1'
 if mode=='wrongtx':v['observation']['txid']='e'*64
 if mode=='shallow':v['observation']['confirmations']='1'
 if mode=='unsent':v['relay']['state']='not-attempted'
 if mode=='rpc-replace':
  with (root/'rpc.json').open('a') as f:f.write(' ')
if mode in ('attempted','acknowledged'):v['relay']['state']=mode
if mode=='request':v['requestId']='bad'
if mode=='trade':v['tradeId']='bad'
if mode=='chain':v['chainObserved']=True
if mode=='execute':v['executable']=True
if mode=='observation':v['observation']={}
if mode=='fee':v['review']['details']['feeAtoms']='3'
if mode=='changed':
 v['review']['details']['txid']='e'*64;v['relay']['txid']='e'*64
if mode in ('fee','changed'):
 d=v['review']['details'];v['review']['commitment']=hashlib.sha256(d['schema'].encode()+b'\0'+json.dumps(d,sort_keys=True,separators=(',',':')).encode()).hexdigest()
if mode=='stderr':sys.stderr.write('error')
if mode=='replace':
 with (root/'config.json').open('a') as f:f.write(' ')
if mode=='oversized':sys.stdout.write(' '*16385);sys.exit(0)
sys.stdout.write(json.dumps(v,sort_keys=True,separators=(',',':'))+('\n' if mode=='newline' else ''))
)PY";
        if (!save(helper,program,true)) return false;
        config["helperDigest"]=QString::fromLatin1(QCryptographicHash::hash(program,QCryptographicHash::Sha256).toHex());
        wrapper=directory.filePath("punish.json");
        return writeConfig() && save(config.value("quoteFile").toString(),QJsonDocument(record).toJson(QJsonDocument::Compact))
            && save(directory.filePath("review.json"),QJsonDocument(bound(details)).toJson(QJsonDocument::Compact))
            && save(wrapper,QJsonDocument(QJsonObject{{"schema","monzero-private-bch-punish-v1"},
                {"admissionConfigFile",path},{"deploymentCommitment",deployment}}).toJson(QJsonDocument::Compact));
    }
};
}
