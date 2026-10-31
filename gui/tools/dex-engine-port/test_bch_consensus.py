#!/usr/bin/env python3
"""Owned BCHN regtest acceptance of production covenant spends, not a full swap."""
import argparse, base64, hashlib, json, os, pathlib, shutil, socket, subprocess, tempfile, time, urllib.request, urllib.error

def run(args):
    assert not args.report.exists()
    root=pathlib.Path(tempfile.mkdtemp(prefix='bch-consensus-',dir=args.temp_parent));process=None
    evidence={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in [args.node,args.probe,args.covenant,args.signature,pathlib.Path(__file__).resolve()]}
    try:
        with socket.socket() as s:s.bind(('127.0.0.1',0));port=s.getsockname()[1]
        nodeargs=[str(args.node),'-regtest','-server=1','-connect=0','-listen=0','-dnsseed=0','-discover=0','-upnp=0','-txindex=1', '-fallbackfee=0.00001','-rpcbind=127.0.0.1','-rpcallowip=127.0.0.1',f'-rpcport={port}',f'-datadir={root}']
        process=subprocess.Popen(nodeargs,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
        transport=urllib.request.build_opener(urllib.request.ProxyHandler({}))
        def rpc(method,*params):
            auth=base64.b64encode((root/'regtest/.cookie').read_bytes().strip()).decode()
            endpoint=f'http://127.0.0.1:{port}'+('/wallet/fixture' if method in {'getnewaddress','sendtoaddress'} else '')
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
        def probe(request):
            r=subprocess.run([str(args.probe),str(args.covenant),str(args.signature)],input=json.dumps(request).encode(),capture_output=True,timeout=30)
            assert r.returncode==0,r.stderr.decode()[:500]
            d=json.loads(r.stdout);assert d['schema']=='monzero-bch-consensus-fixture-v1' and d['executable'] is False;return d
        ready();rpc('createwallet','fixture');anchor=rpc('getnewaddress');rpc('generatetoaddress',110,anchor)
        descriptor=probe({});decoded=rpc('decodescript',descriptor['scriptPubKey']);address=decoded['addresses'][0]
        assert rpc('validateaddress',address)['scriptPubKey']==descriptor['scriptPubKey']
        results={}
        for branch in ['redemption','timeout']:
            funding=rpc('sendtoaddress',address,0.001);rpc('generatetoaddress',1,anchor)
            parent=rpc('getrawtransaction',funding,True)
            matches=[o for o in parent['vout'] if o['scriptPubKey']['hex']==descriptor['scriptPubKey']]
            assert len(matches)==1;vout=matches[0]['n']
            prepared=probe({'funding':parent['hex'],'vout':vout})[branch]
            if branch=='timeout':
                try:rpc('sendrawtransaction',prepared['hex'])
                except RuntimeError as error:assert 'non-BIP68-final' in str(error) or 'non-final' in str(error),str(error)
                else:raise AssertionError('Premature timeout accepted')
                assert prepared['txid'] not in rpc('getrawmempool')
                rpc('generatetoaddress',1,anchor)
            for field in ['invalidAmount','invalidDestination']:
                try:rpc('sendrawtransaction',prepared[field])
                except RuntimeError as error:
                    assert 'script' in str(error).lower(),str(error)
                else:raise AssertionError('Covenant accepted '+field)
            assert not rpc('getrawmempool'), 'Rejected covenant mutation entered mempool'
            assert rpc('sendrawtransaction',prepared['hex'])==prepared['txid']
            block=rpc('generatetoaddress',2,anchor)[0]
            def check(expected_block):
                tx=rpc('getrawtransaction',prepared['txid'],True)
                assert tx['hex']==prepared['hex'] and tx['confirmations']>=2 and tx['blockhash']==expected_block
                assert len(tx['vin'])==len(tx['vout'])==1 and tx['vin'][0]['txid']==funding and tx['vin'][0]['vout']==vout
                from decimal import Decimal
                assert Decimal(str(tx['vout'][0]['value']))==Decimal('0.00099')
                destination=descriptor['redeemDestination' if branch=='redemption' else 'timeoutDestination']
                assert tx['vout'][0]['scriptPubKey']['hex']==destination
                assert rpc('gettxout',funding,vout) is None
            check(block)
            rpc('stop');assert process.wait(timeout=20)==0
            process=subprocess.Popen(nodeargs,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL);ready();rpc('loadwallet','fixture');check(block)
            # Distinct timestamp prevents deterministic regtest mining from recreating
            # the exact block explicitly marked invalid above.
            replacement_time=rpc('getblockheader',block)['time']+1
            rpc('invalidateblock',block)
            rpc('setmocktime',replacement_time)
            assert rpc('getrawtransaction',prepared['txid'],True).get('confirmations',0)<=0
            assert rpc('gettxout',funding,vout,False) is not None
            if prepared['txid'] not in rpc('getrawmempool'):assert rpc('sendrawtransaction',prepared['hex'])==prepared['txid']
            replacement=rpc('generatetoaddress',2,anchor)[0];assert replacement!=block;check(replacement)
            results[branch]={'txid':prepared['txid'],'accepted':True,'restartVerified':True,'reorgRevoked':True,'exactBytesReconfirmed':True,'prematureRejected':branch=='timeout','alteredAmountRejected':True,'alteredDestinationRejected':True,'outputAtoms':99000,'feeAtoms':1000}
        rpc('stop');assert process.wait(timeout=20)==0
        for name,digest in evidence.items():assert hashlib.sha256(pathlib.Path(name).read_bytes()).hexdigest()==digest
        with args.report.open('x') as f:json.dump({'status':'passed','scope':'BCH covenant primitives only','fullCrossChainSettlement':False,'executable':False,'results':results,'evidence':evidence},f,indent=2)
        print('PASS: BCHN covenant redemption/timeout, restart and reorganisation; not full settlement')
    finally:
        if process and process.poll() is None:
            process.terminate()
            try:process.wait(timeout=20)
            except subprocess.TimeoutExpired:process.kill();process.wait()
        shutil.rmtree(root)

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ['node','probe','covenant','signature','temp-parent','report']:p.add_argument('--'+name,required=True,type=lambda v:pathlib.Path(v).resolve())
    run(p.parse_args())
