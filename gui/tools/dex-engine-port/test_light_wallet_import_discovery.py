#!/usr/bin/env python3
"""Pending import discovery against isolated real regtest nodes and local TLS.
No public peers, user keys, real funds or XMZ mining. Nodes are stopped in finally.
"""
import argparse
import json
import os
from pathlib import Path
import subprocess
import shutil
import tempfile
import uuid
import test_light_wallet_chain as chain
from test_light_wallet_import_helper import PHRASE, digest, write


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--qt-upgrade',action='store_true',help='migrate activated wallet to a relocated trusted helper before payment')
    parser.add_argument('--qt-payment',action='store_true',help='verify recovered wallet payment and refreshed backup')
    parser.add_argument('--qt-activate',action='store_true',help='verify desktop activation and guarded reopening')
    parser.add_argument('--activate',action='store_true',help='verify explicit helper wallet activation after recovery')
    parser.add_argument('--qt-recover',action='store_true',help='verify Qt recovery and fresh-process resume')
    parser.add_argument('--probe',type=Path,help='also verify discovery through the real Qt worker')
    parser.add_argument('--recover',action='store_true',help='also verify retained recovery and encrypted backup')
    parser.add_argument('coin',choices=['ltc','btc','bch','doge']);parser.add_argument('helper',type=Path)
    parser.add_argument('node',type=Path);parser.add_argument('tls',type=Path);parser.add_argument('report',type=Path)
    args=parser.parse_args();assert not args.report.exists()
    assert not args.qt_recover or (args.probe and not args.recover)
    assert not args.activate or args.recover
    assert not args.qt_activate or args.qt_recover
    assert not args.qt_payment or args.qt_activate
    assert not args.qt_upgrade or args.qt_payment
    payment_evidence = None
    upgrade_evidence = None
    coin=args.coin;helper=args.helper.resolve(strict=True);node_binary=args.node.resolve(strict=True);tls=args.tls.resolve(strict=True)
    pins=dict(helper=digest(helper),node=digest(node_binary),test=digest(Path(__file__)),chainFixture=digest(Path(chain.__file__)))
    chain.CHAIN=coin
    if coin=='ltc':chain.CHAINS['ltc']=dict(genesis='530827f38f93b43ed12af0b3ad25a288dc02ed74d6d7857862df51fc56c416f9')
    with tempfile.TemporaryDirectory(prefix='monzero-import-discovery-'+coin+'-') as temporary:
        root=Path(temporary).resolve();node_directory=root/'node';node_directory.mkdir(mode=0o700)
        node=chain.Node(node_binary,node_directory)
        settings=dict(schema=f'monzero-private-{coin}-setup-v1',network='regtest',walletId=str(uuid.uuid4()),
                      accountIndex=0,maximumHeight=1000,seedEncoding='bip39-english-v1')
        for key in ['setupDirectory','vaultDirectory','addressBookDirectory','backupDirectory','spendDirectory','cacheDirectory']:
            folder=root/key;folder.mkdir(mode=0o700);settings[key]=str(folder)
        setup=root/'setup.json';password='disposable-'+uuid.uuid4().hex
        env=dict(os.environ,SSL_CERT_FILE=str(tls/'root.pem'))
        def call(mode,configuration,request,reject=False):
            result=subprocess.run([str(helper),'--'+mode+'-stdio-v1',str(configuration)],input=json.dumps(request).encode(),
                                  capture_output=True,env=env,timeout=180)
            assert password.encode() not in result.stdout+result.stderr and PHRASE.encode() not in result.stdout+result.stderr
            if reject:
                assert result.returncode!=0 and not result.stdout,(coin,mode,'unexpected acceptance')
                return None
            assert result.returncode==0,(coin,mode,result.returncode,result.stderr.decode())
            return json.loads(result.stdout)
        try:
            node.start()
            with chain.ElectrumPeer(node,tls,root) as peer:
                settings['endpoint']=dict(host='localhost',port=peer.port);write(setup,settings)
                request=dict(schema=f'monzero-{coin}-light-wallet-import-preview-request-v1',requestId=str(uuid.uuid4()),
                    approvedConfigDigest=digest(setup),seedEncoding='bip39-english-v1',phrase=PHRASE,recoveryPassphrase='')
                preview=call('wallet-import-preview',setup,request)
                imported=call('wallet-import',setup,dict(request,schema=f'monzero-{coin}-light-wallet-import-request-v1',
                    password=password,approvedReview=preview['reviewDigest'],importApproved=True))
                pending=Path(imported['wallet']['configurationFile'])
                wallet=None
                if coin in ['btc','ltc']:node.rpc('createwallet','fixture');wallet='fixture'
                anchor=node.rpc('getnewaddress',wallet=wallet)
                node.mine(160 if coin=='doge' else 101,anchor)
                amount='100' if coin=='doge' else '0.01'
                credit=node.rpc('sendtoaddress',preview['review']['receiveAddress'],amount,wallet=wallet);node.mine(1,anchor)
                retained={p:digest(p) for key in ['setupDirectory','vaultDirectory','addressBookDirectory','backupDirectory','spendDirectory']
                          for p in Path(settings[key]).rglob('*') if p.is_file()}
                discovery=dict(schema=f'monzero-{coin}-light-wallet-import-discover-request-v1',requestId=str(uuid.uuid4()),
                    approvedConfigDigest=digest(pending),discoveryApproved=True,maximumDownloadHeaders=1000,
                    knownMinimum=[2,1],walletScan=dict(gap=2,addressesPerBranch=10,historyEntriesPerBranch=50))
                for invalid in [dict(discovery,discoveryApproved=False),dict(discovery,approvedConfigDigest='0'*64),
                                dict(discovery,walletScan=dict(gap=2,addressesPerBranch=2,historyEntriesPerBranch=50))]:
                    before=len(peer.requests);call('wallet-import-discover',pending,invalid,reject=True);assert len(peer.requests)==before
                found=call('wallet-import-discover',pending,discovery)
                review=found['review'];coverage=review['coverage']
                assert review['observedConfirmedAtomic']==str(chain.sats(amount))
                assert review['balanceStatus']=='provisional' and review['historyCompleteness']=='unproven'
                assert not review['spendable'] and not review['allocationsRetained'] and review['activationRequired']
                assert coverage['allocatedAfter']==[4,3] and coverage['knownMinimum']==[2,1]
                assert coverage['historyCompleteness']=='unproven' and coverage['backupRefreshRequired']
                assert 'blockchain.transaction.get_merkle' in peer.requests
                assert retained=={p:digest(p) for p in retained}
                assert json.loads(pending.read_text())['schema'].endswith('light-recovery-pending-v1')
                repeated=call('wallet-import-discover',pending,discovery)
                assert repeated['reviewDigest']==found['reviewDigest']
                assert repeated['synchronization']['downloadedHeaders']==0
                assert retained=={p:digest(p) for p in retained}
                if args.probe:
                    probe=args.probe.resolve(strict=True)
                    installed=root/'pinned-helper';shutil.copyfile(helper,installed);installed.chmod(0o500)
                    wrapper=root/'launcher.json';write(wrapper,dict(schema=f'monzero-private-{coin}-light-launcher-v1',
                        helper=str(installed),helperDigest=digest(installed),configFile=str(setup)))
                    options=dict(knownMinimum=[2,1],gap=2,addresses=10,entries=50,approved=True)
                    def qt_scan(options,reject=False):
                        completed=subprocess.run([str(probe),str(wrapper),'wallet-import-discover','1000',json.dumps(options)],
                            capture_output=True,env=dict(env,MONZERO_TEST_LIGHT_COIN=coin),timeout=180)
                        assert password.encode() not in completed.stdout+completed.stderr and PHRASE.encode() not in completed.stdout+completed.stderr
                        if reject:
                            assert completed.returncode!=0 and not completed.stdout
                            return
                        assert completed.returncode==0,(coin,'Qt discovery',completed.returncode,completed.stderr.decode())
                        return json.loads(completed.stdout)
                    for invalid in [dict(options,approved=False),dict(options,addresses=2)]:
                        before=len(peer.requests);qt_scan(invalid,True);assert len(peer.requests)==before
                    qt=qt_scan(options)
                    assert qt['importDiscovery']['reviewDigest']==found['reviewDigest'] and qt['walletImportPending']
                    assert not qt['walletReceiveAvailable'] and not qt['walletSignAvailable']
                    assert retained=={p:digest(p) for p in retained}
                    pins['probe']=digest(probe)
                    if args.qt_recover:
                        def qt_recover(operation,options,secret,reject=False):
                            completed=subprocess.run([str(probe),str(wrapper),operation,'1000',json.dumps(options)],
                                input=secret.encode(),capture_output=True,env=dict(env,MONZERO_TEST_LIGHT_COIN=coin),timeout=180)
                            assert password.encode() not in completed.stdout+completed.stderr and PHRASE.encode() not in completed.stdout+completed.stderr
                            if reject:
                                assert completed.returncode!=0 and not completed.stdout
                                return
                            assert completed.returncode==0,(coin,'Qt recovery',completed.returncode,completed.stderr.decode())
                            return json.loads(completed.stdout)
                        for invalid in [dict(options,approved=False),dict(options,approvedReview='0'*64)]:
                            qt_recover('wallet-import-recover-flow',invalid,password,True)
                            assert not list(Path(settings['setupDirectory']).glob('import-recovery-*'))
                        qt_recover('wallet-import-recover-flow',options,'wrong password',True)
                        assert retained=={p:digest(p) for p in retained}
                        sessions=list(Path(settings['setupDirectory']).glob('import-recovery-*'));assert len(sessions)==1
                        assert (sessions[0]/'review.json').stat().st_mode&0o777==0o600
                        assert not (sessions[0]/'journal/light-recovery-allocation-intent.json').exists()
                        resumed=qt_recover('wallet-import-resume',{},password)
                        receipt=resumed['importRecovery'];assert receipt['backupVerified'] and not receipt['activated']
                        assert receipt['allocated']==[4,3] and resumed['walletImportPending']
                        assert not resumed['walletReceiveAvailable'] and not resumed['walletSignAvailable']
                        before=len(peer.requests)
                        again=qt_recover('wallet-import-resume',{},password)
                        assert again['importRecovery']==receipt and len(peer.requests)==before
                        snapshot={p:digest(p) for p in sessions[0].rglob('*') if p.is_file()}
                        qt_recover('wallet-import-resume',{},'wrong password',True)
                        assert snapshot=={p:digest(p) for p in snapshot} and len(peer.requests)==before
                        for path in sessions[0].rglob('*'):
                            if path.is_file():
                                assert password.encode() not in path.read_bytes() and PHRASE.encode() not in path.read_bytes()
                        immutable={p:value for p,value in retained.items() if p.parent!=Path(settings['addressBookDirectory'])}
                        assert immutable=={p:digest(p) for p in immutable}
                        if args.qt_activate:
                            def activate_qt(options,activation_password,reject=False):
                                credentials=dict(recoveryPassword=password,activationPassword=activation_password)
                                completed=subprocess.run([str(probe),str(wrapper),'wallet-import-activate','1000',json.dumps(options)],
                                    input=json.dumps(credentials).encode(),capture_output=True,env=dict(env,MONZERO_TEST_LIGHT_COIN=coin),timeout=180)
                                assert password.encode() not in completed.stdout+completed.stderr and PHRASE.encode() not in completed.stdout+completed.stderr
                                if reject:
                                    assert completed.returncode!=0 and not completed.stdout
                                    return
                                assert completed.returncode==0,(coin,'Qt activation',completed.returncode,completed.stderr.decode())
                                return json.loads(completed.stdout)
                            setup_folder=Path(settings['setupDirectory']);active=setup_folder/'litecoin-wallet-active.json'
                            for options,secret in [({'approved':False},password),({'approvedActivation':'0'*64},password),({},'wrong password')]:
                                activate_qt(options,secret,True);assert not active.exists()
                            before=len(peer.requests);enabled=activate_qt({},password)
                            assert enabled['importActivated'] and not enabled['walletImportPending'] and enabled['walletReceiveAvailable']
                            assert not enabled['executable'] and not enabled['importRecovery'] and len(peer.requests)==before
                            def attach():
                                completed=subprocess.run([str(probe),'--attach-local',coin,str(root),str(installed),digest(installed)],
                                    capture_output=True,env=dict(env,MONZERO_TEST_LIGHT_COIN=coin),timeout=90)
                                assert completed.returncode==0,(coin,'activated reopen',completed.stderr.decode())
                                return json.loads(completed.stdout)
                            reopened=attach();assert reopened['importActivated'] and reopened['walletReceiveAvailable'] and not reopened['importRecovery']
                            complete=setup_folder/'litecoin-wallet-activated.json';held=root/'held-activation.json';complete.rename(held)
                            incomplete=attach();assert incomplete['state']=='wallet-import-activation-incomplete' and not incomplete['walletReceiveAvailable']
                            held.rename(complete)
                            original=active.read_bytes();changed=json.loads(original);changed['maximumHeight']=999;write(active,changed)
                            invalid=attach();assert invalid['state']=='wallet-import-activation-invalid' and not invalid['walletReceiveAvailable']
                            active.write_bytes(original)
                            received=subprocess.run([str(probe),str(wrapper),'wallet-receive','10'],capture_output=True,
                                env=dict(env,MONZERO_TEST_LIGHT_COIN=coin),timeout=90)
                            assert received.returncode==0,(coin,'activated Qt receive',received.stderr.decode())
                            receiving=json.loads(received.stdout)['wallet'];assert receiving['derivationPath'].endswith('/0/4') and receiving['receivingAllocated']==5
                            later=attach();assert later['walletReceiveAvailable'] and not later['importRecovery']
                            assert len(peer.requests)==before
                            if args.qt_upgrade:
                                from test_imported_wallet_upgrade import migrate
                                wrapper,installed,upgrade_evidence=migrate(probe,wrapper,installed,root,settings,coin,env,peer)
                                reopened=attach();assert reopened['importActivated'] and reopened['walletReceiveAvailable']
                            if args.qt_payment:
                                from test_imported_wallet_payment import verify
                                payment_evidence=verify(probe,wrapper,coin,node,wallet,anchor,root,settings,password,env,attach)
                if args.recover:
                    journal=root/'recovery-journal';journal.mkdir(mode=0o700)
                    destination=root/'recovery-backup';destination.mkdir(mode=0o700)
                    recovery=dict(schema=f'monzero-{coin}-light-wallet-import-recover-request-v1',requestId=str(uuid.uuid4()),
                        approvedConfigDigest=digest(pending),recoveryApproved=True,approvedReview=review,
                        approvedReviewDigest=found['reviewDigest'],resume=False,journalDirectory=str(journal),
                        backupDirectory=str(destination),password=password,maximumDownloadHeaders=1000,
                        knownMinimum=[2,1],walletScan=discovery['walletScan'])
                    for invalid in [dict(recovery,recoveryApproved=False),dict(recovery,approvedConfigDigest='0'*64),
                                    dict(recovery,approvedReviewDigest='0'*64),dict(recovery,password='wrong password')]:
                        before=len(peer.requests);call('wallet-import-recover',pending,invalid,reject=True)
                        assert len(peer.requests)==before
                        assert retained=={p:digest(p) for p in retained}
                        assert not list(journal.iterdir()) and not list(destination.iterdir())
                    recovered=call('wallet-import-recover',pending,recovery)
                    receipt=recovered['recovery']
                    assert receipt['backupVerified'] and receipt['allocationsRetained'] and receipt['allocated']==[4,3]
                    assert not receipt['activated'] and recovered['activationRequired'] and not recovered['executable']
                    assert receipt['historyCompleteness']=='unproven' and not receipt['activeSwapRecovery']
                    assert receipt['backupDigest']==digest(destination/'litecoin-wallet.backup')
                    immutable={p:checksum for p,checksum in retained.items() if p.parent!=Path(settings['addressBookDirectory'])}
                    assert immutable=={p:digest(p) for p in immutable}
                    before=len(peer.requests)
                    repeat=call('wallet-import-recover',pending,dict(recovery,resume=True))
                    assert repeat['recovery']==receipt and len(peer.requests)==before
                    snapshot={p:digest(p) for folder in [journal,destination,Path(settings['addressBookDirectory'])]
                              for p in folder.iterdir() if p.is_file()}
                    call('wallet-import-recover',pending,dict(recovery,resume=True,password='wrong password'),reject=True)
                    assert snapshot=={p:digest(p) for p in snapshot} and len(peer.requests)==before
                    # A new scan after allocations and a chain extension cannot
                    # reuse the original review; retained recovery still resumes.
                    node.mine(1,anchor)
                    call('wallet-import-recover',pending,recovery,reject=True)
                    assert snapshot=={p:digest(p) for p in snapshot}
                    assert call('wallet-import-recover',pending,dict(recovery,resume=True))['recovery']==receipt
                    assert json.loads(pending.read_text())['schema'].endswith('light-recovery-pending-v1')
                    call('wallet-init',pending,dict(schema=f'monzero-{coin}-light-wallet-init-request-v1',requestId=str(uuid.uuid4())),reject=True)
                    for folder in [journal,destination]:
                        for path in folder.iterdir():
                            if path.is_file():
                                assert password.encode() not in path.read_bytes() and PHRASE.encode() not in path.read_bytes()
                if args.activate:
                    activation=dict(schema=f'monzero-{coin}-light-wallet-import-activate-request-v1',requestId=str(uuid.uuid4()),
                        approvedConfigDigest=digest(pending),approvedCoverage=review['coverageCommitment'],
                        approvedRecoveryDigest=digest(journal/'light-recovery-backup-complete.json'),activationApproved=True,
                        journalDirectory=str(journal),backupDirectory=str(destination),password=password)
                    setup_folder=Path(settings['setupDirectory']);active=setup_folder/'litecoin-wallet-active.json'
                    original_pending=digest(pending)
                    for invalid in [dict(activation,activationApproved=False),dict(activation,approvedRecoveryDigest='0'*64),
                                    dict(activation,password='wrong password')]:
                        before=len(peer.requests);call('wallet-import-activate',pending,invalid,reject=True)
                        assert len(peer.requests)==before and not active.exists()
                        assert not (setup_folder/'litecoin-wallet-activation.json').exists()
                    activated=call('wallet-import-activate',pending,activation)
                    assert activated['activation']['walletActivated'] and not activated['tradingEnabled']
                    assert not activated['activation']['tradingEnabled'] and not activated['activation']['activeSwapRecovery']
                    assert Path(activated['activation']['configurationFile'])==active
                    assert digest(active)==activated['activation']['configurationDigest'] and digest(pending)==original_pending
                    assert call('wallet-import-activate',pending,activation)['activation']==activated['activation']
                    received=call('wallet-receive',active,dict(schema=f'monzero-{coin}-light-wallet-receive-request-v1',requestId=str(uuid.uuid4())))
                    assert received['wallet']['derivationPath'].endswith('/0/4') and received['wallet']['receivingAllocated']==5
                    assert received['wallet']['changeAllocated']==3 and not received['wallet']['spendable']
                    # Later allocations make the activation-time backup historical.
                    call('wallet-import-activate',pending,activation,reject=True)
                    assert digest(active)==activated['activation']['configurationDigest'] and digest(pending)==original_pending
                assert node.rpc('getconnectioncount')==0
                result=dict(coin=coin,passed=True,creditTxid=credit,observedAtomic=review['observedConfirmedAtomic'],
                    coverage=coverage['allocatedAfter'],knownMinimum=coverage['knownMinimum'],approvalAndBudgetRejectedBeforeNetwork=True,
                    allocationsAndVaultUnchangedBeforeRecovery=True,stableReviewAfterHeaderSync=True,discoveryPending=True,
                    recoveryAndBackupVerified=args.recover or args.qt_recover,qtRecoveryVerified=args.qt_recover,
                    qtDiscoveryVerified=bool(args.probe),qtActivationVerified=args.qt_activate,payment=payment_evidence,helperMigration=upgrade_evidence,
                    activationEnabled=args.activate or args.qt_activate,tradingEnabled=False)
        finally:node.stop()
    assert pins['helper']==digest(helper) and pins['node']==digest(node_binary)
    args.report.write_text(json.dumps(dict(schema='monzero-import-discovery-node-test-v1',evidence=pins,result=result,
        scope='Local regtest discovery, optional recovery and explicit wallet activation; no public-chain or DEX trading or active-trade recovery.'),indent=2)+'\n')
    print('PASS '+coin+': funded pending import discovery'+(' and recovery' if args.recover or args.qt_recover else '')+(' and wallet activation' if args.activate or args.qt_activate else ''))


if __name__=='__main__':main()
