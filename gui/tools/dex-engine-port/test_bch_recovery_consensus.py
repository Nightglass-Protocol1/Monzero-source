#!/usr/bin/env python3
"""Owned BCHN regtest acceptance of production two-stage recovery, not a full swap."""
import argparse, base64, hashlib, json, os, pathlib, shutil, socket, subprocess, tempfile, time, urllib.request, urllib.error

def run(args):
    assert not args.report.exists()
    root=pathlib.Path(tempfile.mkdtemp(prefix='bch-consensus-',dir=args.temp_parent));process=None
    evidence={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in [args.node,args.probe,pathlib.Path(__file__).resolve()]}
    if args.observer: evidence[str(args.observer)]=hashlib.sha256(args.observer.read_bytes()).hexdigest()
    observation_calls=0
    try:
        with socket.socket() as s:s.bind(('127.0.0.1',0));port=s.getsockname()[1]
        nodeargs=[str(args.node),'-regtest','-server=1','-connect=0','-listen=0','-dnsseed=0','-discover=0','-upnp=0','-txindex=1', '-fallbackfee=0.00001','-rpcbind=127.0.0.1','-rpcallowip=127.0.0.1',f'-rpcport={port}',f'-datadir={root}']
        process=subprocess.Popen(nodeargs,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
        transport=urllib.request.build_opener(urllib.request.ProxyHandler({}))
        def rpc(method,*params):
            auth=base64.b64encode((root/'regtest/.cookie').read_bytes().strip()).decode()
            endpoint=f'http://127.0.0.1:{port}'+('/wallet/fixture' if method in {'getnewaddress','sendtoaddress','getreceivedbyaddress'} else '')
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
            r=subprocess.run([str(args.probe)],input=json.dumps(request).encode(),capture_output=True,timeout=30)
            assert r.returncode==0,r.stderr.decode()[:500]
            d=json.loads(r.stdout);assert d['schema']=='monzero-bch-recovery-consensus-fixture-v1' and d['executable'] is False;return d
        def observe(txid,block,raw,vout,confirmed,unspent,indexed=False,refund_safe=None,punish_mature=None,**overrides):
            nonlocal observation_calls
            if not args.observer:return
            request=dict(deployment,cookie=(root/'regtest/.cookie').read_text().strip(),port=port,
                txid=txid,block=block,expectedHex=raw,vout=vout,minimum=1,indexed=indexed)
            request.update(overrides)
            result=subprocess.run([str(args.observer)],input=json.dumps(request).encode(),capture_output=True,timeout=60)
            assert result.returncode==0,'BCH observer adapter failed'
            observed=json.loads(result.stdout)
            assert observed['schema']=='monzero-bch-observation-fixture-v1' and observed['executable'] is False
            assert observed['confirmed'] is confirmed and observed['unspent'] is unspent,'BCH observer disagrees with node fixture'
            if confirmed:assert observed['block']==block and observed['confirmations']>=request['minimum']
            if refund_safe is not None:assert observed['refundSafe'] is refund_safe
            if punish_mature is not None:assert observed['punishMature'] is punish_mature
            observation_calls+=1
        ready();rpc('createwallet','fixture');anchor=rpc('getnewaddress');rpc('generatetoaddress',110,anchor)
        deployment={'anchor':anchor,'anchorScript':rpc('validateaddress',anchor)['scriptPubKey'],
            'checkpoint':rpc('getblockhash',1),'subversion':rpc('getnetworkinfo')['subversion']}
        results={}
        def reject_early(tx):
            try:rpc('sendrawtransaction',tx['hex'])
            except RuntimeError as error:assert 'non-BIP68-final' in str(error) or 'non-final' in str(error),str(error)
            else:raise AssertionError('Premature recovery spend accepted')
            assert tx['txid'] not in rpc('getrawmempool')
        for branch in ['success','refund','punish']:
            payout=rpc('getnewaddress');refund_address=rpc('getnewaddress')
            context={'payoutDestination':rpc('validateaddress',payout)['scriptPubKey'],
                'refundDestination':rpc('validateaddress',refund_address)['scriptPubKey']}
            descriptor=probe(context)
            address=rpc('decodescript',descriptor['lockScriptPubKey'])['addresses'][0]
            assert rpc('validateaddress',address)['scriptPubKey']==descriptor['lockScriptPubKey']
            funding=rpc('sendtoaddress',address,0.001);rpc('generatetoaddress',1,anchor)
            parent=rpc('getrawtransaction',funding,True)
            outputs=[o for o in parent['vout'] if o['scriptPubKey']['hex']==descriptor['lockScriptPubKey']]
            assert len(outputs)==1;vout=outputs[0]['n']
            context.update(funding=parent['hex'],vout=vout);prepared=probe(context)
            observe(funding,parent['blockhash'],parent['hex'],vout,True,True,indexed=True)
            observe(funding,parent['blockhash'],parent['hex'],vout,False,False,minimum=2)
            observe(funding,parent['blockhash'],parent['hex'],vout,False,False,checkpoint=rpc('getblockhash',2))
            observe(funding,parent['blockhash'],parent['hex'],vout,False,False,subversion='/Bitcoin Cash Node:0.0.0/')
            previous=funding;previous_vout=vout
            if branch!='success':
                reject_early(prepared['cancel'])
                # Age the original lock beyond both delays. The punishment delay
                # must still start from cancellation, not original funding.
                rpc('generatetoaddress',6,anchor)
                assert rpc('sendrawtransaction',prepared['cancel']['hex'])==prepared['cancel']['txid']
                rpc('generatetoaddress',1,anchor)
                cancel=rpc('getrawtransaction',prepared['cancel']['txid'],True)
                assert cancel['hex']==prepared['cancel']['hex'] and cancel['vout'][0]['scriptPubKey']['hex']==descriptor['recoveryScriptPubKey']
                from decimal import Decimal
                assert Decimal(str(cancel['vout'][0]['value']))==Decimal('0.00099')
                previous=prepared['cancel']['txid'];previous_vout=0
                observe(funding,parent['blockhash'],parent['hex'],vout,True,False)
                observe(previous,cancel['blockhash'],cancel['hex'],0,True,True,refund_safe=True,punish_mature=False)
                reject_early(prepared['punish'])
                if branch=='punish':
                    rpc('generatetoaddress',3,anchor);reject_early(prepared['punish'])
                    observe(previous,cancel['blockhash'],cancel['hex'],0,True,True,refund_safe=False,punish_mature=False)
                    rpc('generatetoaddress',1,anchor)
                    observe(previous,cancel['blockhash'],cancel['hex'],0,True,True,refund_safe=False,punish_mature=True)
            tx=prepared[branch]
            assert rpc('sendrawtransaction',tx['hex'])==tx['txid']
            spent_parent=parent if branch=='success' else cancel
            observe(previous,spent_parent['blockhash'],spent_parent['hex'],previous_vout,True,False)
            block=rpc('generatetoaddress',2,anchor)[0]
            expected_atoms=99000 if branch=='success' else 97000
            recipient=refund_address if branch=='refund' else payout
            other=payout if branch=='refund' else refund_address
            def check(expected_block):
                observed=rpc('getrawtransaction',tx['txid'],True)
                assert observed['hex']==tx['hex'] and observed['confirmations']>=2 and observed['blockhash']==expected_block
                assert len(observed['vin'])==len(observed['vout'])==1
                assert observed['vin'][0]['txid']==previous and observed['vin'][0]['vout']==previous_vout
                from decimal import Decimal
                assert Decimal(str(observed['vout'][0]['value']))*100000000==expected_atoms
                assert observed['vout'][0]['scriptPubKey']['hex']==context['refundDestination' if branch=='refund' else 'payoutDestination']
                assert Decimal(str(rpc('getreceivedbyaddress',recipient,2)))*100000000==expected_atoms
                assert rpc('getreceivedbyaddress',other,0)==0
                assert rpc('gettxout',previous,previous_vout) is None
                recovered=probe(dict(context,observed=observed['hex'],branch=branch))
                assert recovered['observedVerified'] is True
                observe(tx['txid'],expected_block,observed['hex'],0,True,True)
                observe(tx['txid'],expected_block,observed['hex'],0,True,True,indexed=True)
            check(block)
            rpc('stop');assert process.wait(timeout=20)==0
            process=subprocess.Popen(nodeargs,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
            ready();rpc('loadwallet','fixture');check(block)
            replacement_time=rpc('getblockheader',block)['time']+1
            rpc('invalidateblock',block);rpc('setmocktime',replacement_time)
            assert rpc('getrawtransaction',tx['txid'],True).get('confirmations',0)<=0
            observe(tx['txid'],block,tx['hex'],0,False,False)
            assert rpc('gettxout',previous,previous_vout,False) is not None
            if tx['txid'] not in rpc('getrawmempool'):assert rpc('sendrawtransaction',tx['hex'])==tx['txid']
            replacement=rpc('generatetoaddress',2,anchor)[0];assert replacement!=block;check(replacement)
            observe(tx['txid'],block,tx['hex'],0,False,False)
            results[branch]={'txid':tx['txid'],'accepted':True,'restartVerified':True,'reorgRevoked':True,
                'exactBytesReconfirmed':True,'recipientAtoms':expected_atoms,'otherRecipientAtoms':0,
                'adaptorRecoveryFromObservedBytes':branch!='punish','prematureCancellationRejected':branch!='success',
                'punishmentRelativeToCancellation':branch=='punish'}
        rpc('stop');assert process.wait(timeout=20)==0
        for name,digest in evidence.items():assert hashlib.sha256(pathlib.Path(name).read_bytes()).hexdigest()==digest
        with args.report.open('x') as f:json.dump({'status':'passed','scope':'BCH two-stage recovery primitives only',
            'fullCrossChainSettlement':False,'executable':False,'productionObserverVerified':bool(args.observer),'observationCalls':observation_calls,'results':results,'evidence':evidence},f,indent=2)
        print('PASS: BCH two-stage success/refund/punishment, adaptor recovery, restart and reorganisation')
    finally:
        if process and process.poll() is None:
            process.terminate()
            try:process.wait(timeout=20)
            except subprocess.TimeoutExpired:process.kill();process.wait()
        shutil.rmtree(root)

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ['node','probe','temp-parent','report']:p.add_argument('--'+name,required=True,type=lambda v:pathlib.Path(v).resolve())
    p.add_argument('--observer',type=lambda v:pathlib.Path(v).resolve())
    run(p.parse_args())
