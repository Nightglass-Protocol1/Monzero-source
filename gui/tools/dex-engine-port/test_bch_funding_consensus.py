#!/usr/bin/env python3
"""Owned BCHN regtest acceptance of wallet-selected funding preparation, not a full swap."""
import argparse, base64, hashlib, json, os, pathlib, shutil, socket, subprocess, tempfile, time, urllib.request, urllib.error

def run(args):
    assert not args.report.exists()
    root=pathlib.Path(tempfile.mkdtemp(prefix='bch-consensus-',dir=args.temp_parent));process=None
    evidence={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in [args.node,args.probe,args.covenant,pathlib.Path(__file__).resolve()]}
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
        def probe(request,rejected=False):
            result=subprocess.run([str(args.probe)],input=json.dumps(request).encode(),capture_output=True,timeout=60)
            if rejected:
                assert result.returncode!=0 and not result.stdout,'Invalid funding context accepted';return
            assert result.returncode==0,result.stderr.decode(errors='replace')[:1000]
            data=json.loads(result.stdout);assert data['schema']=='monzero-bch-funding-consensus-fixture-v1' and data['executable'] is False
            return data
        ready();rpc('createwallet','fixture');anchor=rpc('getnewaddress');rpc('generatetoaddress',110,anchor)
        ordinary=rpc('getnewaddress');rpc('sendtoaddress',ordinary,1.0);rpc('generatetoaddress',2,anchor)
        # Preparation intentionally excludes coinbase inputs. Make the owned
        # wallet select its ordinary self-payment or change instead.
        coinbase=[{'txid':o['txid'],'vout':o['vout']} for o in rpc('listunspent',0,9999999,[anchor])]
        assert coinbase and rpc('lockunspent',False,coinbase)
        change=rpc('getnewaddress')
        context={'action':'prepare','cookie':(root/'regtest/.cookie').read_text().strip(),'port':port,
            'checkpoint':rpc('getblockhash',1),'anchor':anchor,'anchorScript':rpc('validateaddress',anchor)['scriptPubKey'],
            'subversion':rpc('getnetworkinfo')['subversion'],'changeAddress':change,'changeScript':rpc('validateaddress',change)['scriptPubKey'],
            'maximumFee':10000,'reference':json.loads(args.covenant.read_text())}
        def snapshots():return (rpc('listlockunspent'),rpc('getrawmempool'))
        before=snapshots();candidate=probe(context);assert snapshots()==before
        assert 0<candidate['feeAtoms']<=10000 and candidate['lockVout']==0 and candidate['inputs']
        decoded=rpc('decoderawtransaction',candidate['unsigned'])
        from decimal import Decimal
        assert decoded['vout'][0]['scriptPubKey']['hex']==context['reference']['scriptPubKey']
        assert Decimal(str(decoded['vout'][0]['value']))==Decimal('0.001')
        assert all(i['scriptSig']['hex']=='' and i['sequence']==4294967295 for i in decoded['vin'])
        input_total=Decimal(0)
        for i in candidate['inputs']:
            output=rpc('gettxout',i['txid'],i['vout'])
            assert output and not output['coinbase'] and output['confirmations']>=2
            input_total+=Decimal(str(output['value']))
        assert (input_total-sum(Decimal(str(o['value'])) for o in decoded['vout']))*100000000==candidate['feeAtoms']
        probe(dict(context,maximumFee=1),rejected=True);assert snapshots()==before
        probe(dict(context,anchorScript='76a914'+'11'*20+'88ac'),rejected=True);assert snapshots()==before
        probe(dict(context,checkpoint=rpc('getblockhash',2)),rejected=True);assert snapshots()==before
        # Node signing here is an independent fixture action. Production signing
        # and relay remain executor-private and are not exercised by this adapter.
        signed=rpc('signrawtransactionwithwallet',candidate['unsigned'],[],'ALL|FORKID')
        assert signed['complete'] and snapshots()==before
        verify=dict(context,action='verify-signed',unsigned=candidate['unsigned'],sources=candidate['sources'],signed=signed['hex'])
        checked=probe(verify);assert checked['signedVerified'] and checked['lockVout']==0
        bad=signed['hex'][:-2]+('01' if signed['hex'][-2:]!='01' else '02')
        probe(dict(verify,signed=bad),rejected=True)
        assert rpc('sendrawtransaction',signed['hex'])==checked['txid']
        block=rpc('generatetoaddress',2,anchor)[0]
        def check(expected_block):
            observed=rpc('getrawtransaction',checked['txid'],True)
            assert observed['hex']==signed['hex'] and observed['confirmations']>=2 and observed['blockhash']==expected_block
            assert observed['vout']==decoded['vout']
            assert probe(verify)['txid']==checked['txid']
        check(block)
        rpc('stop');assert process.wait(timeout=20)==0
        process=subprocess.Popen(nodeargs,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL);ready();rpc('loadwallet','fixture');check(block)
        replacement_time=rpc('getblockheader',block)['time']+1
        rpc('invalidateblock',block);rpc('setmocktime',replacement_time)
        assert rpc('getrawtransaction',checked['txid'],True).get('confirmations',0)<=0
        if checked['txid'] not in rpc('getrawmempool'):assert rpc('sendrawtransaction',signed['hex'])==checked['txid']
        replacement=rpc('generatetoaddress',2,anchor)[0];assert replacement!=block;check(replacement)
        rpc('stop');assert process.wait(timeout=20)==0
        for name,h in evidence.items():assert hashlib.sha256(pathlib.Path(name).read_bytes()).hexdigest()==h
        with args.report.open('x') as f:json.dump({'status':'passed','scope':'Production unsigned preparation and signed verification; fixture-only node signing/relay',
            'fullCrossChainSettlement':False,'executable':False,'feeAtoms':candidate['feeAtoms'],'principalAtoms':100000,
            'preparationNoLockSignOrBroadcast':True,'wrongWalletAnchorRejected':True,'wrongCheckpointRejected':True,'feeCapRejected':True,
            'signedFundingVerified':True,'changedSignedTransactionRejected':True,'nodeAccepted':True,'restartVerified':True,'reorgReconfirmed':True,'evidence':evidence},f,indent=2)
        print('PASS: BCH wallet preparation, independent signature verification and actual funding acceptance')
    finally:
        if process and process.poll() is None:
            process.terminate()
            try:process.wait(timeout=20)
            except subprocess.TimeoutExpired:process.kill();process.wait()
        shutil.rmtree(root)

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ['node','probe','covenant','temp-parent','report']:p.add_argument('--'+name,required=True,type=lambda v:pathlib.Path(v).resolve())
    run(p.parse_args())
