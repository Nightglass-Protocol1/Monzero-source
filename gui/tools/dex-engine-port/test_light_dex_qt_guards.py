#!/usr/bin/env python3
"""Fault injection for the Qt light DEX process boundary; no keys or network."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
import uuid


def sha(data): return hashlib.sha256(data).hexdigest()
def compact(value): return json.dumps(value,sort_keys=True,separators=(',',':')).encode()
def private(path,value):
    path.write_bytes(compact(value));path.chmod(0o600)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('probe',type=Path);parser.add_argument('report',type=Path)
    args=parser.parse_args();probe=args.probe.resolve(strict=True)
    assert not args.report.exists()
    cases=[]
    with tempfile.TemporaryDirectory(prefix='monzero-light-dex-qt-guards-') as temporary:
        root=Path(temporary).resolve();wallet=root/'wallet.json';config=root/'dex.json';helper=root/'helper';mode=root/'mode';marker=root/'called'
        trade=str(uuid.uuid4())
        private(wallet,dict(schema='monzero-private-ltc-light-v1',network='regtest',wallet={},endpoint=dict(host='localhost',port=12345)))
        private(config,dict(schema='monzero-private-ltc-light-dex-v1',walletConfigFile=str(wallet),metadataDirectory=str(root),
            snapshotDirectory=str(root),tradeId=trade,pins=[[0]*32,[1]*32],policy=dict(ltc_network='ltc-regtest')))
        helper.write_text('''#!/usr/bin/python3
import json,sys,time
from pathlib import Path
root=Path(__file__).parent
(root/'called').touch()
request=json.load(sys.stdin);request.setdefault('payment',dict(address='fixture',amountAtomic='1000',feeAtomic='100'));mode=(root/'mode').read_text();config=Path(sys.argv[2]);c=json.loads(config.read_text())
result=dict(watchOnly=True,spendable=False,prepared=True,signed=True,paymentAddress=request['payment']['address'],
    paymentAtomic=request['payment']['amountAtomic'],feeAtomic=request['payment']['feeAtomic'],changeAddress=None,changeAtomic='0',
    inputCount=1,txid='ab'*32,signingCommitment='cd'*32,observation=dict(position='reported-pending'),selectedHeight=10,selectedTip='ef'*32)
if mode.startswith('terms-'):
    result=dict(schema='monzero-ltc-light-funding-terms-v1',tradeId=c['tradeId'],quoteCommitment='cd'*32,
        network='530827f38f93b43ed12af0b3ad25a288dc02ed74d6d7857862df51fc56c416f9',contractAddress='rltc1qfixture',
        refundAddress='rltc1qrefund',amountAtomic='1000',maximumFeeAtomic='100',minimumChangeAtomic='100',
        lockConfirmations=2,expiresAt='2000000000',watchOnly=True,broadcastAuthorized=False)
    if mode=='terms-wrong-network':result['network']='00'*32
    if mode=='terms-wrong-trade':result['tradeId']='00000000-0000-0000-0000-000000000001'
    if mode=='terms-secret':result['password']='must-not-escape'
r=dict(schema=request['schema'].replace('-request-v1','-response-v1'),requestId=request['requestId'],tradeId=c['tradeId'],
    configurationDigest=request['approvedConfigDigest'],executable=False,result=result)
if mode=='wrong-trade':r['tradeId']='00000000-0000-0000-0000-000000000001'
if mode=='wrong-request':r['requestId']='00000000-0000-0000-0000-000000000001'
if mode=='wrong-context':r['configurationDigest']='00'*32
if mode=='wrong-amount':result['paymentAtomic']='999'
if mode=='wrong-claim':result['signingCommitment']='00'*32
if mode=='wrong-change':result['changeAddress']='changed'
if mode=='false-confirmation':result['observation']=dict(position='included',confirmations=0,blockHash='00'*32)
if mode=='secret-field':result['password']='must-not-escape'
if mode=='stderr':sys.stderr.write('discard-this-private-error')
if mode=='changed-config':config.write_bytes(config.read_bytes()+b' ')
if mode=='oversized':sys.stdout.write('x'*262145);sys.exit(0)
if mode=='crashed':sys.exit(1)
print(json.dumps(r))
''');helper.chmod(0o700)
        helper_hash=sha(helper.read_bytes());original=config.read_bytes()
        context=sha(compact(dict(schema='monzero-ltc-light-dex-context-v1',dexDigest=sha(original),walletDigest=sha(wallet.read_bytes()))))
        request=dict(schema='monzero-ltc-light-dex-funding-observe-request-v1',requestId=str(uuid.uuid4()),approvedConfigDigest=context,
            maximumDownloadHeaders=100,payment=dict(address='fixture',amountAtomic='1000',feeAtomic='100',minimumChangeAtomic='100',minimumConfirmations=1),
            retainedReview=dict(signingCommitment='cd'*32,changeAddress=None),maximumHistoryEntries=50)
        for case in ['valid','wrong-trade','wrong-request','wrong-context','wrong-amount','wrong-claim','wrong-change',
                     'false-confirmation','secret-field','stderr','changed-config','oversized','crashed','request-secret','wrong-helper-pin','public-config',
                     'terms-valid','terms-wrong-network','terms-wrong-trade','terms-secret','terms-has-payment']:
            config.write_bytes(original);config.chmod(0o600);mode.write_text(case);marker.unlink(missing_ok=True)
            sent=dict(request);pin=helper_hash;operation='observe'
            if case.startswith('terms-'):
                operation='terms';sent={k:request[k] for k in ['requestId','approvedConfigDigest']}
                sent['schema']='monzero-ltc-light-dex-funding-terms-request-v1'
                if case=='terms-has-payment':sent['payment']=request['payment']
            if case=='request-secret':sent['password']='must-not-launch'
            if case=='wrong-helper-pin':pin='00'*32
            if case=='public-config':config.chmod(0o644)
            result=subprocess.run([str(probe),str(config),str(helper),pin,operation],input=compact(sent),capture_output=True,timeout=15)
            if case in ['valid','terms-valid']:assert result.returncode==0 and json.loads(result.stdout)['tradeId']==trade
            else:assert result.returncode!=0 and not result.stdout,(case,result.returncode,result.stdout)
            assert b'must-not' not in result.stdout+result.stderr and b'discard-this' not in result.stdout+result.stderr
            if case in ['request-secret','wrong-helper-pin','public-config','terms-has-payment']:assert not marker.exists(),case
            cases.append(case)
    args.report.write_text(json.dumps(dict(schema='monzero-light-dex-qt-guards-v1',probeSha256=sha(probe.read_bytes()),passed=cases),indent=2)+'\n')
    print('PASS:',len(cases),'Qt funding boundary cases')

if __name__=='__main__':main()
