#!/usr/bin/env python3
"""Real LTC regtest funding through the light DEX helper; local disposable keys only."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import uuid
import test_light_wallet_chain as chain

TEST = 'litecoin_light::funding_tests::helper_fixture::actual_light_dex_helper_context'

def digest(path): return hashlib.sha256(path.read_bytes()).hexdigest()
def write(path, value): chain.write_private(path, value)
def context_digest(dex, wallet):
    value = dict(schema='monzero-ltc-light-dex-context-v1', dexDigest=digest(dex), walletDigest=digest(wallet))
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(',', ':')).encode()).hexdigest()

def run(args, encoding, foreign_refund=False):
    with tempfile.TemporaryDirectory(prefix='monzero-light-dex-helper-') as temporary:
        root = Path(temporary).resolve()
        node_dir = root/'node'; node_dir.mkdir(mode=0o700)
        node = chain.Node(args.node, node_dir)
        password = 'disposable-' + uuid.uuid4().hex
        env = dict(os.environ, SSL_CERT_FILE=str(args.tls/'root.pem'))
        def engine(stage):
            result = subprocess.run([str(args.engine), '--exact', TEST, '--ignored'],
                env=dict(env, MONZERO_LIGHT_DEX_FIXTURE_ROOT=str(root), MONZERO_LIGHT_DEX_FIXTURE_STAGE=stage),
                capture_output=True, timeout=240)
            assert result.returncode == 0, result.stdout.decode()+result.stderr.decode()
        def call(mode, config, request, reject=False, force_direct=False, wrong_quote=False):
            print(encoding, mode, 'reject' if reject else 'accept', flush=True)
            command = [str(args.helper), '--'+mode+'-stdio-v1', str(config)]
            if args.qt_worker and not force_direct and mode.startswith('dex-funding-'):
                command = [str(args.qt_worker),str(config),str(args.helper),digest(args.helper),mode.removeprefix('dex-funding-')]
                if args.qt_bound: command.append(str(root/'launcher.json'))
            controller_call=args.qt_controller and not force_direct and mode.startswith('dex-funding-')
            if controller_call:
                context=json.loads((root/'context.json').read_text())
                claim=json.loads((root/'bob-meta'/f"{context['trade']}.ltc-execution-claim").read_text())
                quote='00'*32 if wrong_quote else bytes(claim['quote']).hex()
                command=[str(args.qt_controller),str(config),str(args.helper),digest(args.helper),mode.removeprefix('dex-funding-'),
                    str(root/'launcher.json'),context['trade'],quote]
            result = subprocess.run(command,
                input=json.dumps(request).encode(), env=env, capture_output=True, timeout=300)
            assert password.encode() not in result.stdout+result.stderr
            if reject:
                assert result.returncode != 0 and not result.stdout, (mode, 'unexpected acceptance')
                return None
            assert result.returncode == 0, (mode, result.returncode, result.stderr.decode())
            value = json.loads(result.stdout)
            assert (controller_call or value['requestId'] == request['requestId']) and value['executable'] is False
            assert value['schema'] == request['schema'].replace('-request-v1','-response-v1')
            if mode.startswith('dex-funding-'):
                assert value['configurationDigest'] == request['approvedConfigDigest']
            return value
        def ordinary(mode, config, **extra):
            return call(mode, config, dict(schema=f'monzero-ltc-light-{mode}-request-v1', requestId=str(uuid.uuid4()), **extra))
        try:
            node.start()
            with chain.ElectrumPeer(node, args.tls, root) as peer:
                settings = dict(schema='monzero-private-ltc-setup-v1', network='regtest', walletId=str(uuid.uuid4()),
                    accountIndex=0, maximumHeight=1000, seedEncoding=encoding, endpoint=dict(host='localhost', port=peer.port))
                for key in ['setupDirectory','vaultDirectory','addressBookDirectory','backupDirectory','spendDirectory','cacheDirectory']:
                    folder=root/key; folder.mkdir(mode=0o700); settings[key]=str(folder)
                setup=root/'setup.json'; write(setup, settings)
                created=ordinary('wallet-create', setup, createApproval=dict(approvedConfigDigest=digest(setup),password=password))['wallet']
                wallet=Path(created['configurationFile'])
                receive=ordinary('wallet-receive',wallet)['wallet']['address']
                node.rpc('createwallet','fixture'); anchor=node.rpc('getnewaddress',wallet='fixture')
                node.mine(101,anchor); node.rpc('sendtoaddress',receive,'2',wallet='fixture'); node.mine(1,anchor)
                write(root/'refund-address.json',node.rpc('getnewaddress',wallet='fixture') if foreign_refund else receive)
                engine('init'); context=json.loads((root/'context.json').read_text()); payment=json.loads((root/'payment.json').read_text())
                config=root/'dex.json'; write(config, dict(schema='monzero-private-ltc-light-dex-v1', walletConfigFile=str(wallet),
                    metadataDirectory=str(root/'bob-meta'), snapshotDirectory=str(root/'bob-secrets'), tradeId=context['trade'],
                    pins=context['pins'], policy=context['policy']))
                if args.qt_bound:
                    launcher=root/'launcher.json'
                    launcher.write_text(json.dumps(dict(schema='monzero-private-ltc-light-launcher-v1',configFile=str(wallet),helper=str(args.helper),helperDigest=digest(args.helper)),sort_keys=True,separators=(',',':')))
                    launcher.chmod(0o600)
                retained=None
                def request(mode, **extra):
                    value=dict(schema=f'monzero-ltc-light-dex-funding-{mode}-request-v1', requestId=str(uuid.uuid4()),
                        approvedConfigDigest=context_digest(config,wallet), maximumDownloadHeaders=1000, payment=payment)
                    if retained is not None: value['retainedReview']=retained
                    if mode in ['observe','retry-review','retry']: value['maximumHistoryEntries']=50
                    else: value['walletScan']=dict(gap=2, addressesPerBranch=10, historyEntriesPerBranch=50)
                    return dict(value,**extra)
                def dex(mode, reject=False, **extra):
                    return call('dex-funding-'+mode,config,request(mode,**extra),reject)
                terms_request=dict(schema='monzero-ltc-light-dex-funding-terms-request-v1',requestId=str(uuid.uuid4()),
                    approvedConfigDigest=context_digest(config,wallet))
                def local_files():
                    return {str(p.relative_to(root)):digest(p) for name in ['bob-meta','bob-secrets','spendDirectory','addressBookDirectory','vaultDirectory']
                        for p in (root/name).rglob('*') if p.is_file()}
                initial_files=local_files();initial_requests=len(peer.requests)
                if foreign_refund:
                    call('dex-funding-terms',config,terms_request,True,force_direct=True)
                    call('dex-funding-prepare',config,request('prepare'),True,force_direct=True)
                    assert local_files()==initial_files and len(peer.requests)==initial_requests
                    return dict(seedEncoding=encoding,foreignRefundRejected=True,noWalletMutation=True,noNetworkQuery=True)
                call('dex-funding-terms',config,dict(terms_request,requestId=str(uuid.UUID(int=0))),True)
                call('dex-funding-terms',config,dict(terms_request,password=password),True)
                call('dex-funding-terms',config,dict(terms_request,payment=payment),True)
                call('dex-funding-terms',config,dict(terms_request,approvedConfigDigest='00'*32),True)
                if args.qt_controller: call('dex-funding-terms',config,terms_request,True,wrong_quote=True)
                terms=call('dex-funding-terms',config,terms_request)['result']
                direct=call('dex-funding-terms',config,terms_request,force_direct=True)['result']
                assert direct==terms
                call('dex-funding-terms',config,dict(terms_request,password=password),True,True)
                call('dex-funding-terms',config,dict(terms_request,requestId=str(uuid.UUID(int=0))),True,True)
                assert terms['contractAddress']==payment['address'] and terms['amountAtomic']==payment['amountAtomic']
                assert int(terms['maximumFeeAtomic'])>=int(payment['feeAtomic'])
                assert terms['minimumChangeAtomic']==payment['minimumChangeAtomic'] and terms['lockConfirmations']==context['policy']['ltc_confirmations']
                assert terms['watchOnly'] is True and terms['broadcastAuthorized'] is False
                assert local_files()==initial_files and len(peer.requests)==initial_requests
                payment=dict(address=terms['contractAddress'],amountAtomic=terms['amountAtomic'],feeAtomic=payment['feeAtomic'],
                    minimumChangeAtomic=terms['minimumChangeAtomic'],minimumConfirmations=payment['minimumConfirmations'])
                before=len(peer.requests)
                dex('prepare',reject=True,approvedConfigDigest='00'*32)
                dex('prepare',reject=True,password=password)
                assert len(peer.requests)==before
                bad=dict(payment,address=receive)
                if not args.qt_bound:
                    dex('prepare',reject=True,payment=bad)
                    assert len(peer.requests)==before
                prepared=dex('prepare'); write(root/'prepared.json',prepared)
                retained=dict(signingCommitment=prepared['result']['signingCommitment'],changeAddress=prepared['result']['changeAddress'])
                again=dex('prepare'); assert again['result']==prepared['result']
                if args.qt_bound:
                    dex('prepare',reject=True,payment=bad)  # The retained intent cannot change.
                    retained=None  # Subsequent fresh Qt processes load their saved review.
                    again=dex('prepare'); assert again['result']==prepared['result']
                dex('review',reject=True)  # Peer recovery must arrive first.
                assert not list(Path(settings['spendDirectory']).glob('ltc-signed-*'))
                engine('peer')
                if args.qt_controller: call('dex-funding-review',config,request('review'),True,wrong_quote=True)
                review=dex('review')['result']['review']; approve=review['commitment']
                dex('sign',reject=True,approval='00'*32,password=password)
                dex('sign',reject=True,approval=approve,password='wrong')
                assert not list(Path(settings['spendDirectory']).glob('ltc-signed-*'))
                signed=dex('sign',approval=approve,password=password)['result']
                retry_signed=dex('sign',approval=approve,password=password)['result']; assert retry_signed==signed
                assert signed['signed'] is True and signed['broadcast'] is False and signed['watchOnly'] is False
                reviewed_send=dex('send-review')['result']; assert reviewed_send['signed'] is True
                send=reviewed_send['review']
                before=peer.requests.count('blockchain.transaction.broadcast')
                dex('send',reject=True,approval=approve)  # Sign consent cannot authorize send.
                assert peer.requests.count('blockchain.transaction.broadcast')==before
                sent=dex('send',approval=send['commitment'])['result']; txid=sent['txid']
                assert txid==signed['txid'] and txid in node.rpc('getrawmempool') and sent['watchOnly'] is False
                decoded=node.rpc('getrawtransaction',txid,True)
                assert chain.sats(decoded['vout'][0]['value'])==int(payment['amountAtomic'])
                dex('send',reject=True,approval=send['commitment'])
                observed=dex('observe')['result']; assert observed['observation']['position']=='reported-pending'
                retry=dex('retry-review')['result']['review']
                dex('retry',reject=True,approval=send['commitment'])
                repeated=dex('retry',approval=retry['commitment'])['result']; assert repeated['txid']==txid and repeated['watchOnly'] is False
                block=node.mine(1,anchor)[0]
                observed=dex('observe')['result']; assert observed['observation']['position']=='included'
                assert observed['observation']['blockHash']==block and observed['observation']['confirmations']>=1
                before=peer.requests.count('blockchain.transaction.broadcast')
                dex('retry-review',reject=True)
                dex('retry',reject=True,approval=retry['commitment'])
                assert peer.requests.count('blockchain.transaction.broadcast')==before
                assert not peer.errors,peer.errors
                return dict(seedEncoding=encoding,fundingTxid=txid,confirmationBlock=block,
                    peerRecoveryRequired=True,signAndSendApprovalsSeparated=True,exactRetry=True,nodeAcceptedSignatures=True,
                    includedRetryRejected=True,helperProcessRestart=True)
        finally: node.stop()

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ['helper','engine','node','tls','report']: parser.add_argument(name,type=Path)
    parser.add_argument("--qt-worker",type=Path,help="Exercise the Qt funding worker for every DEX operation")
    parser.add_argument("--qt-bound",action="store_true",help="Bind each Qt funding worker to an active desktop wallet")
    parser.add_argument("--qt-controller",type=Path,help="Drive the trade-facing controller with independently pinned fixture quote")
    args=parser.parse_args()
    if args.qt_controller:
        args.qt_controller=args.qt_controller.resolve(strict=True);args.qt_bound=True
    assert not args.qt_bound or args.qt_worker or args.qt_controller
    if args.qt_worker: args.qt_worker=args.qt_worker.resolve(strict=True)
    for name in ['helper','engine','node','tls']: setattr(args,name,getattr(args,name).resolve(strict=True))
    assert not args.report.exists()
    chain.CHAIN='ltc';chain.CHAINS['ltc']=dict(genesis='530827f38f93b43ed12af0b3ad25a288dc02ed74d6d7857862df51fc56c416f9')
    sources=[args.helper,args.engine,args.node,Path(__file__),Path(chain.__file__)]
    if args.qt_worker: sources.append(args.qt_worker)
    if args.qt_controller: sources.append(args.qt_controller)
    pins={str(p):digest(p) for p in sources}
    cases=[run(args,encoding) for encoding in ['raw-bip32-seed-hex-v1','bip39-english-v1']]
    negative_cases=[run(args,encoding,True) for encoding in ['raw-bip32-seed-hex-v1','bip39-english-v1']]
    assert all(digest(Path(p))==h for p,h in pins.items())
    args.report.write_text(json.dumps(dict(schema='monzero-light-dex-helper-acceptance-v1',sha256=pins,cases=cases,negativeCases=negative_cases,
        fullCrossChainSettlement=False,desktopIntegration=False,qtFundingWorker=bool(args.qt_worker),activeWalletBound=args.qt_bound,qtTradeController=bool(args.qt_controller)),indent=2)+'\n')
    print('PASS: real LTC funding, local keys, peer recovery, separate consent, retry and confirmation for both vault formats')
if __name__=='__main__': main()
