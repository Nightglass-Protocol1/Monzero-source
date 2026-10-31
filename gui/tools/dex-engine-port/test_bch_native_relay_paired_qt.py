#!/usr/bin/env python3
"""Owned BCHN regtest acceptance of production controller locking, signing and funding relay, not a full swap."""
from bch_native_funding_fixture_v2 import NativeFundingFixture
import argparse, base64, hashlib, json, os, pathlib, shutil, socket, subprocess, tempfile, time, urllib.request, urllib.error

def run(args):
    assert not args.report.exists()
    root=pathlib.Path(tempfile.mkdtemp(prefix='bch-consensus-',dir=args.temp_parent));process=None;child=None;native=None
    evidence={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in [args.node,args.machine,args.daemon,args.wallet_rpc,args.helper,args.qt_probe,pathlib.Path(__file__).resolve(),pathlib.Path(__file__).with_name("bch_native_funding_fixture_v2.py")]}
    try:
        with socket.socket() as s:s.bind(('127.0.0.1',0));port=s.getsockname()[1]
        nodeargs=[str(args.node),'-regtest','-server=1','-connect=0','-listen=0','-dnsseed=0','-discover=0','-upnp=0','-txindex=1', '-fallbackfee=0.00001','-rpcbind=127.0.0.1','-rpcallowip=127.0.0.1',f'-rpcport={port}',f'-datadir={root}']
        process=subprocess.Popen(nodeargs,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
        transport=urllib.request.build_opener(urllib.request.ProxyHandler({}))
        def rpc(method,*params):
            auth=base64.b64encode((root/'regtest/.cookie').read_bytes().strip()).decode()
            endpoint=f'http://127.0.0.1:{port}'+('/wallet/fixture' if method in {'getnewaddress','sendtoaddress','listunspent','listlockunspent','lockunspent','signrawtransactionwithwallet'} else '')
            req=urllib.request.Request(endpoint,data=json.dumps({'id':'fixture','method':method,'params':params}).encode(),headers={'Authorization':'Basic '+auth,'Content-Type':'application/json'})
            try:response=transport.open(req,timeout=30)
            except urllib.error.HTTPError as error:response=error
            with response:d=json.load(response)
            if d.get('error'):raise RuntimeError(str(d['error']))
            return d['result']
        def ready():
            deadline=time.monotonic()+30
            while True:
                assert process.poll() is None,'Owned BCHN exited'
                try:info=rpc('getblockchaininfo');break
                except (OSError,ValueError,RuntimeError):
                    assert time.monotonic()<deadline;time.sleep(.1)
            assert info['chain']=='regtest' and rpc('getconnectioncount')==0
            assert rpc('getblockhash',0)=='0f9188f13cb7b2c71f2a335e3a4fc328bf5beb436012afca590b1a11466e2206'
        ready();rpc('createwallet','fixture');anchor=rpc('getnewaddress');rpc('generatetoaddress',110,anchor)
        ordinary=rpc('getnewaddress');rpc('sendtoaddress',ordinary,1.0);rpc('generatetoaddress',2,anchor)
        # Preparation intentionally excludes coinbase inputs. Make the owned
        # wallet select its ordinary self-payment or change instead.
        coinbase=[{'txid':o['txid'],'vout':o['vout']} for o in rpc('listunspent',0,9999999,[anchor])]
        assert coinbase and rpc('lockunspent',False,coinbase)
        change=rpc('getnewaddress')
        config={'schema':'monzero-owned-bch-node-executor-test-v1','cookie':(root/'regtest/.cookie').read_text().strip(),'port':port,
            'checkpoint':rpc('getblockhash',1),'anchor':anchor,'anchorScript':rpc('validateaddress',anchor)['scriptPubKey'],
            'subversion':rpc('getnetworkinfo')['subversion'],'changeAddress':change,'changeScript':rpc('validateaddress',change)['scriptPubKey'],
            'report':str(root/'controller-result.json'),'eventFile':str(root/'event.json'),'continueFile':str(root/'continue')}
        native=NativeFundingFixture(root,args.daemon,args.wallet_rpc,port)
        config['native']=native.context
        path=root/'controller-config.json';path.write_text(json.dumps(config,sort_keys=True,separators=(',',':')));path.chmod(0o600)
        env=os.environ.copy();env.update(MONZERO_BCH_QT_NATIVE_RELAY_PROBE=str(args.qt_probe),MONZERO_BCH_QT_NATIVE_RELAY_PROBE_SHA256=hashlib.sha256(args.qt_probe.read_bytes()).hexdigest(),MONZERO_BCH_NATIVE_PREPARATION_PAIRED='1',MONZERO_BCH_ADMISSION_HELPER=str(args.helper),MONZERO_BCH_ADMISSION_HELPER_SHA256=hashlib.sha256(args.helper.read_bytes()).hexdigest(),MONZERO_BCH_NODE_EXECUTOR_CONFIG=str(path),MONZERO_BCH_NODE_EXECUTOR_CONFIG_SHA256=hashlib.sha256(path.read_bytes()).hexdigest())
        with args.log.open('x') as log:
            child=subprocess.Popen([str(args.machine),'bch_paired_nodes_fund_native_contract','--ignored','--nocapture'],env=env,stdout=log,stderr=subprocess.STDOUT)
            phase=0;deadline=time.monotonic()+1800;funding_txid=None
            while child.poll() is None:
                assert time.monotonic()<deadline,'Funding handoff fixture timed out'
                event_path=root/'event.json'
                if event_path.exists():
                    event=json.loads(event_path.read_text())
                    if phase==0 and event['phase']=='funded':
                        funding_txid=event['txid'];assert rpc('getrawmempool')==[funding_txid]
                        rpc('generatetoaddress',3,anchor)
                        pending=root/'continue.pending';pending.write_text('confirmed');os.replace(pending,root/'continue');phase=1
                    elif phase==1 and event['phase']=='native-funded':
                        assert event['txid']==funding_txid and event['nativeAmountAtoms']==200000000000
                        native_fee=native.confirm(event['nativeTxid']);assert native_fee==event['nativeFeeAtoms']
                        pending=root/'continue.pending';pending.write_text('native-confirmed');os.replace(pending,root/'continue');phase=2
                time.sleep(.1)
            assert child.wait()==0 and phase==2,'Production handoff fixture failed; inspect retained log'
        report=json.loads((root/'controller-result.json').read_text())
        assert report['schema']=='monzero-bch-node-executor-result-v1' and report['productionWalletLockSignRelay'] is True
        assert report['restartSavedBytes'] is True and report['executable'] is False
        txid=report['txid'];assert txid==funding_txid and rpc('getrawmempool')==[]
        assert report['nativeRelayHelperVerified'] and report['nativeRelayQtVerified']
        assert report['nativePreparationHelperVerified']
        assert report['authenticatedHandoff'] and report['nativeFundingWindowVerified'] and report['pairedNativeFunding']
        signed=rpc('getrawtransaction',txid,True)
        output=signed['vout'][report['lockVout']]
        from decimal import Decimal
        assert output['scriptPubKey']['hex']==report['lockScript']
        assert Decimal(str(output['value']))*100000000==report['principalAtoms']==100000
        assert all(v['scriptSig']['hex'] for v in signed['vin'])
        inputs=Decimal(0)
        for i in report['inputs']:
            parent=rpc('getrawtransaction',i['txid'],True)
            inputs+=Decimal(str(parent['vout'][i['vout']]['value']))
        fee=(inputs-sum(Decimal(str(v['value'])) for v in signed['vout']))*100000000
        assert fee==report['feeAtoms'] and 0<fee<=1000
        confirmed=rpc('gettxout',txid,report['lockVout']);assert confirmed and confirmed['confirmations']==3
        rpc('stop');assert process.wait(timeout=20)==0
        process=subprocess.Popen(nodeargs,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL);ready()
        confirmed=rpc('gettxout',txid,report['lockVout']);assert confirmed and confirmed['confirmations']==3
        assert rpc('getrawtransaction',txid,False)==signed['hex']
        rpc('stop');assert process.wait(timeout=20)==0
        for name,h in evidence.items():assert hashlib.sha256(pathlib.Path(name).read_bytes()).hexdigest()==h
        with args.report.open('x') as f:json.dump(dict(status='passed',scope='Production helper prepares and broadcasts the approved XMZ deposit; resumed controller verifies confirmation across both owned chains',nativePreparationHelperVerified=True,nativeRelayHelperVerified=True,nativeRelayQtVerified=True,
            fullCrossChainSettlement=False,executable=False,productionWalletLockSignRelay=True,restartSavedBytes=True,
            nodeRestartVerified=True,nodeConfirmed=True,authenticatedHandoff=True,nativeFundingWindowVerified=True,pairedNativeFunding=True,nativeFeeAtoms=native_fee,nativeAmountAtoms=200000000000,feeAtoms=report['feeAtoms'],principalAtoms=100000,evidence=evidence),f,indent=2)
        print('PASS: production BCH and XMZ contract funding, authenticated proof and actual confirmation')
    finally:
        if child and child.poll() is None:
            child.kill();child.wait()
        if process and process.poll() is None:
            process.terminate()
            try:process.wait(timeout=20)
            except subprocess.TimeoutExpired:process.kill();process.wait()
        if native:native.close()
        shutil.rmtree(root)

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ['node','machine','temp-parent','report','log','daemon','wallet-rpc','helper','qt-probe']:p.add_argument('--'+name,required=True,type=lambda v:pathlib.Path(v).resolve())
    run(p.parse_args())
