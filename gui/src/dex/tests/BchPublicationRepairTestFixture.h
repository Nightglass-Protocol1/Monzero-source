#pragma once
#include "BchCancellationTestFixture.h"
namespace {
struct PublicationRepairFixture:Context {
    QString wrapper,rpc;
    bool createRepair() {
        if (!Context::create()) return false;
        const QByteArray program=R"PY(#!/usr/bin/python3
import hashlib,json,pathlib,sys,time
root=pathlib.Path(sys.argv[2]).parent
request=json.load(sys.stdin)
mode=(root/'mode').read_text()
action='submit' if 'submit' in sys.argv[1] else 'review'
with (root/'calls').open('a') as f:f.write(action+'\n')
if mode=='lost' and action=='submit':sys.exit(1)
if mode=='slow':time.sleep(1)
details=json.loads((root/'repair-details').read_text())
details['stage']=request['stage']
compact=lambda x:json.dumps(x,sort_keys=True,separators=(',',':')).encode()
digest=hashlib.sha256(details['schema'].encode()+b'\0'+compact(details)).hexdigest()
result={'schema':'monzero-bch-success-publication-repair-'+action+'-response-v1',
 'requestId':request['requestId'],'tradeId':details['swapId'],'executable':False,
 'chainObserved':False,'releasesPrivateMaterial':False,'state':'repaired' if action=='submit' else 'reviewed',
 'review':{'details':details,'commitment':digest}}
if mode=='private':result['frame']='SECRET-MUST-NOT-BE-EXPOSED'
if mode=='request':result['requestId']='00000000-0000-0000-0000-000000000000'
if mode=='chain':result['chainObserved']=True
sys.stdout.buffer.write(compact(result))
)PY";
        if (!save(helper,program,true)) return false;
        config["helperDigest"]=QString::fromLatin1(QCryptographicHash::hash(program,QCryptographicHash::Sha256).toHex());
        if (!save(path,QJsonDocument(config).toJson(QJsonDocument::Compact))
            || !save(config.value("quoteFile").toString(),QJsonDocument(record).toJson(QJsonDocument::Compact))) return false;
        QJsonObject repairDetails{{"schema","monzero-bch-success-publication-repair-v1"},{"action","repair-private-success-publication"},
            {"swapId",config.value("tradeId")},{"quote",details.value("quote")},{"admission",pinBytes(1)},
            {"nativeReceipt",pinBytes(2)},{"candidate",pinBytes(3)},{"recordedAt",config.value("admittedAt")},
            {"bchDeployment",pinBytes(0x77)},{"fundingSource","local-wallet"},{"stage","prepared"},
            {"releasesPrivateMaterial",false},{"chainApproval",false}};
        if (!save(directory.filePath("repair-details"),QJsonDocument(repairDetails).toJson(QJsonDocument::Compact))) return false;
        rpc=directory.filePath("rpc");wrapper=directory.filePath("repair");
        return save(rpc,"{\"schema\":\"monzero-private-bch-observer-rpc-v1\"}")
            && save(wrapper,QJsonDocument(QJsonObject{{"schema","monzero-private-bch-success-publication-repair-v1"},
                {"admissionConfigFile",path},{"rpcFile",rpc},{"deploymentCommitment",deployment},{"fundingSource","local-wallet"}}).toJson(QJsonDocument::Compact))
            && save(mode,"valid") && save(log,{});
    }
};
}
