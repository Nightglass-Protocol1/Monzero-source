#pragma once
#include "BchRefundWalletTestFixture.h"
namespace {
struct RefundWalletWorkerContext:Context {
    QString wrapper,mode,log;
    bool create() {
        if (!Context::create(true)) return false;
        const QByteArray program=R"PY(#!/usr/bin/python3
import hashlib,json,pathlib,sys,time
root=pathlib.Path(sys.argv[2]).parent;r=json.load(sys.stdin)
assert sys.argv[1]=='--native-refund-wallet-status-stdio-v1'
assert set(r)=={'schema','requestId'} and r['schema']=='monzero-bch-native-refund-wallet-status-request-v1'
mode=(root/'mode').read_text();v=json.loads((root/'status.json').read_text())
with (root/'calls').open('a') as f:f.write(sys.argv[1]+'\n')
if mode in ('attempted','ready'):v['state']=mode;v['walletIntent']=[2]*32
if mode=='changed':
 v['address']='5'*95
 binding={k:v[k] for k in ('quote','admission','address','nativePayment')};binding.update(wallet=v['walletFile'],refund=v['refundHint'])
 v['walletBinding']=list(hashlib.sha256(b'monzero-bch-native-refund-wallet-intent-v1\0'+json.dumps(binding,sort_keys=True,separators=(',',':')).encode()).digest())
a={'schema':'monzero-bch-native-refund-wallet-status-response-v1','requestId':r['requestId'],'tradeId':v['swapId'],'wallet':v,'chainObserved':False,'executable':False}
if mode=='request':a['requestId']='wrong'
if mode=='authority':a['executable']=True
if mode=='chain':a['chainObserved']=True
if mode=='private':v['privateKey']='unexpected'
if mode=='stderr':sys.stderr.write('failure')
if mode=='source':
 with (root/'config.json').open('a') as f:f.write(' ')
if mode=='overflow':sys.stdout.write(' '*16385);sys.stdout.flush();time.sleep(10);sys.exit(0)
sys.stdout.write(json.dumps(a,sort_keys=True,separators=(',',':')))
)PY";
        if (!save(helper,program,true)) return false;
        config["helperDigest"]=QString::fromLatin1(QCryptographicHash::hash(program,QCryptographicHash::Sha256).toHex());
        wrapper=directory.filePath("wallet-status.json");mode=directory.filePath("mode");log=directory.filePath("calls");
        return writeConfig() && save(config.value("quoteFile").toString(),QJsonDocument(record).toJson(QJsonDocument::Compact))
            && save(directory.filePath("status.json"),QJsonDocument(refundWalletStatusValue(*this)).toJson(QJsonDocument::Compact))
            && save(mode,"normal") && save(wrapper,QJsonDocument(QJsonObject{{"schema","monzero-private-bch-native-refund-wallet-v1"},
                {"admissionConfigFile",path},{"refundTxid",QString(64,'b')},{"deploymentCommitment",deployment}}).toJson(QJsonDocument::Compact));
    }
};
}
