#!/usr/bin/env python3
"""PUBLIC light-wallet fixture through TLS and an isolated actual Litecoin node.

Usage: test_litecoin_light_chain.py HELPER TLS_FIXTURE LITECOIND REPORT [--drop-ack] [--qt-probe PROBE] [--reconcile] [--reorg] [--restore-before-sign] [--qt-restore] [--create-wallet] [--qt-create]
No public peers, user wallet data, live funds or XMZ mining are involved.
"""
import argparse,hashlib,json,os,subprocess,time,uuid
from pathlib import Path
import test_litecoin_signing_helper as signing
from test_litecoin_settlement import Node

def digest(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def merkle(txids,target,height):
 position=txids.index(target);index=position;branch=[]
 level=[bytes.fromhex(txid)[::-1] for txid in txids]
 while len(level)>1:
  if len(level)%2:level.append(level[-1])
  branch.append(level[index^1][::-1].hex())
  level=[signing.sha256d(level[i]+level[i+1]) for i in range(0,len(level),2)];index//=2
 return {'block_height':height,'pos':position,'merkle':branch}

def run_qt_probe(test,probe,helper,fixture,mode,packet,budget,password=b''):
 test.config.write_text(json.dumps(test.value,sort_keys=True,separators=(',',':')));test.config.chmod(0o600)
 wrapper=test.path/'launcher.json';wrapper.write_text(json.dumps({'schema':'monzero-private-ltc-light-launcher-v1',
  'helper':str(helper),'helperDigest':digest(helper),'configFile':str(test.config)},sort_keys=True,separators=(',',':')));wrapper.chmod(0o600)
 env=os.environ.copy();env['SSL_CERT_FILE']=str(fixture/'root.pem');env['XDG_CONFIG_HOME']=str(test.path/'qt-settings')
 result=subprocess.run([str(probe),str(wrapper),mode,str(budget),json.dumps(packet)],env=env,input=password,capture_output=True,timeout=20)
 assert result.returncode==0,(result.returncode,result.stderr.decode())
 if password:assert password not in result.stdout+result.stderr
 return json.loads(result.stdout)

def run():
 parser=argparse.ArgumentParser();parser.add_argument('litecoind',type=Path);parser.add_argument('report',type=Path)
 parser.add_argument('--drop-ack',action='store_true');parser.add_argument('--qt-probe',type=Path)
 parser.add_argument('--reconcile',action='store_true');parser.add_argument('--reorg',action='store_true')
 parser.add_argument('--restore-before-sign',action='store_true');parser.add_argument('--qt-restore',action='store_true');parser.add_argument('--create-wallet',action='store_true');parser.add_argument('--qt-create',action='store_true');args=parser.parse_args()
 if args.qt_create and not (args.qt_probe and args.create_wallet):parser.error('--qt-create requires --qt-probe and --create-wallet')
 if args.qt_restore and not (args.qt_probe and args.restore_before_sign):parser.error('--qt-restore requires --qt-probe and --restore-before-sign')
 if args.reorg and not args.reconcile:parser.error('--reorg requires --reconcile')
 drop_ack=args.drop_ack
 executable=args.litecoind.resolve();report=args.report.resolve();probe=args.qt_probe.resolve() if args.qt_probe else None
 if report.exists():raise RuntimeError('Report already exists')
 helper=signing.h.PROGRAM;fixture=signing.h.FIXTURE
 evidence={str(path):digest(path) for path in [Path(__file__).resolve(),Path(signing.__file__).resolve(),
  Path(signing.w.__file__).resolve(),Path(signing.h.__file__).resolve(),Path(__file__).with_name('test_litecoin_settlement.py'),
  Path(__file__).with_name('litecoin-signing-fixture.json'),
  Path(__file__).with_name('mnemonic_fixture.py'),Path(__file__).with_name('bip39-english.txt'),
  Path(__file__).with_name('generate_litecoin_account_vectors.py'),Path(__file__).with_name('generate_litecoin_watch_descriptor.py'),
  helper,executable,*fixture.iterdir()]}
 if probe is not None:
  gui=Path(__file__).resolve().parents[2]
  for path in [probe,gui/'src/dex/LitecoinLightJob.cpp',gui/'src/dex/LitecoinLightJob.h',gui/'src/dex/tests/litecoin_light_probe.cpp']:
   evidence[str(path)]=digest(path)
 test=signing.SigningHelperTests(methodName='runTest');test.setUp();node=None;started=time.time();creation=None;addresses=signing.FIXTURE['addresses']
 try:
  if args.create_wallet:
   # These random keys are disposable regtest-only data, never user wallet material.
   from nacl.bindings import crypto_aead_xchacha20poly1305_ietf_decrypt
   from generate_litecoin_account_vectors import generate as account_vectors
   from generate_litecoin_watch_descriptor import descriptor
   password=signing.FIXTURE['vault']['password']
   directories={}
   for key,name in [('setupDirectory','created-setup'),('vaultDirectory','created-vault'),
    ('addressBookDirectory','created-book'),('backupDirectory','created-backup'),('spendDirectory','created-spends')]:
    destination=test.path/name;destination.mkdir(mode=0o700);directories[key]=str(destination)
   test.value=dict(directories,schema='monzero-private-ltc-setup-v1',network='regtest',
    cacheDirectory=str(test.cache),walletId=str(uuid.uuid4()),accountIndex=0,maximumHeight=1000)
   if os.environ.get('MONZERO_TEST_LIGHT_SEED_ENCODING')=='bip39-english-v1':test.value['seedEncoding']='bip39-english-v1'
   test.save_config()
   opened_creation=None
   if args.qt_create:
    created=run_qt_probe(test,probe,helper,fixture,'wallet-create',{},3,password.encode())
    assert created['state']=='wallet-created' and created['setupConfigured'] and not created['walletConfigured']
    creation=created['wallet']
    opened_creation=run_qt_probe(test,probe,helper,fixture,'wallet-created-receive',{},3)
    assert opened_creation['state']=='wallet-address' and opened_creation['walletConfigured'] and not opened_creation['setupConfigured']
   else:creation=test.wallet_call('wallet-create',extra={'createApproval':{
    'approvedConfigDigest':digest(test.config),'password':password}})['wallet']
   published=Path(creation['configurationFile'])
   assert digest(published)==creation['configurationDigest']
   test.value=json.loads(published.read_bytes())
   # Configure the later loopback peer in a separate runtime config, preserving
   # the exact initial configuration whose digest the creation receipt records.
   test.config=test.path/'created-runtime.json';test.save_config()
   test.vault=Path(directories['vaultDirectory']);test.vault_file=test.vault/'litecoin-seed.vault'
   test.book=Path(directories['addressBookDirectory']);test.spends=Path(directories['spendDirectory'])
   encrypted=test.vault_file.read_bytes()
   key=hashlib.scrypt(password.encode(),salt=encrypted[88:104],n=2**17,r=8,p=1,dklen=32,maxmem=256*1024*1024)
   seed=crypto_aead_xchacha20poly1305_ietf_decrypt(encrypted[128:],encrypted[:128],encrypted[104:128],key)
   if os.environ.get('MONZERO_TEST_LIGHT_SEED_ENCODING')=='bip39-english-v1':
    from mnemonic_fixture import seed_from_payload
    assert encrypted[:8]==b'MZMNEM01'
    seed=seed_from_payload(seed)
   else:assert encrypted[:8]==b'MZLTCV01'
   assert descriptor(seed)==test.value['wallet']['account']
   addresses=[v for v in account_vectors(seed)['vectors'] if v['network']=='regtest' and v['account']==0 and v['index']==0]
   del seed,key
   test.address=next(v for v in addresses if v['branch']==0);test.payment['address']=test.address['address']
   receiving=opened_creation['wallet'] if opened_creation is not None else test.wallet_call('wallet-receive')['wallet']
   assert receiving['address']==test.address['address']
  node_dir=test.path/'node';node_dir.mkdir(mode=0o700);node=Node(executable,node_dir);node.start()
  node.rpc('createwallet','fixture');anchor=node.rpc('getnewaddress','','bech32',wallet='fixture')
  node.mine(440,anchor)
  credit=node.rpc('sendtoaddress',test.address['address'],0.001,wallet='fixture')
  credit_block=node.mine(1,anchor)[0];height=node.rpc('getblockcount')
  block=node.rpc('getblock',credit_block,1);raw_credit=node.rpc('getrawtransaction',credit)
  headers=[bytes.fromhex(node.rpc('getblockheader',node.rpc('getblockhash',i),False)) for i in range(1,height+1)]
  scripts={a['branch']:hashlib.sha256(bytes.fromhex(a['script'])).digest()[::-1].hex() for a in addresses}
  sends=[];phase=['credit'];observations=[]
  def wallet(method,params):
   if method=='blockchain.scripthash.get_history':
    if params[0]!=scripts[0]:return []
    history=[{'tx_hash':credit,'height':height}]
    if phase[0] in ['pending','included','stale-proof']:history.append({'tx_hash':signed['txid'],'height':included_height if phase[0]!='pending' else 0})
    return history
   if method=='blockchain.transaction.get':
    if params==[credit,False]:return raw_credit
    assert params==[signed['txid'],False];return signed_raw.hex()
   if method=='blockchain.transaction.get_merkle':
    if params==[credit,height]:return merkle(block['tx'],credit,height)
    assert params==[signed['txid'],included_height]
    return merkle(node.rpc('getblock',confirmed_block,1)['tx'],signed['txid'],included_height)
   if method=='blockchain.transaction.broadcast':
    assert len(params)==1
    raw=bytes.fromhex(params[0]);sends.append(raw)
    assert raw==(test.spends/f'ltc-signed-{signed["signingCommitment"]}.tx').read_bytes()
    assert list(test.spends.glob('ltc-submission-*')) and list(test.spends.glob('ltc-broadcast-*-intent.json'))
    # Real node policy and consensus-script verification, not a fabricated ack.
    txid=node.rpc('sendrawtransaction',params[0])
    phase[0]='pending'
    return signing.h.DROP_RESPONSE if drop_ack else txid
   raise AssertionError('Unexpected fixture method '+method)
  test.value['maximumHeight']=1000;test.save_config()
  with signing.h.Peer(test.path,headers,wallet=wallet) as peer:
   test.endpoint(peer);synced=test.call(sync=True,budget=1000)
   assert synced['chain']['height']==height and synced['chain']['tip']==credit_block
   request=test.prepare();restore_info=None
   if args.restore_before_sign:
    backup_directory=test.path/'recovery-backup';backup_directory.mkdir(mode=0o700)
    password=signing.FIXTURE['vault']['password']
    approval={'directory':str(backup_directory),'password':password}
    saved=test.wallet_call('wallet-backup-save',extra={'backupApproval':approval})['wallet']
    verified=test.wallet_call('wallet-backup-verify',extra={'backupApproval':approval})['wallet']
    assert saved['backupDigest']==verified['backupDigest']
    original_vault=test.vault_file.read_bytes();original_book=(test.book/'litecoin-address-book.json').read_bytes()
    original_claims=test.records()
    restored_vault=test.path/'recovered-vault';restored_book=test.path/'recovered-book'
    restored_vault.mkdir(mode=0o700);restored_book.mkdir(mode=0o700)
    test.value['wallet']['vault']['directory']=str(restored_vault)
    test.value['wallet']['addressBookDirectory']=str(restored_book);test.save_config()
    if args.qt_restore:
     restored=run_qt_probe(test,probe,helper,fixture,'wallet-restore',{'directory':str(backup_directory)},3,password.encode())
     assert restored['state']=='wallet-restored' and not restored['walletRestoreAvailable']
     assert restored['restoreDirectories']=={'vault':str(restored_vault),'addressBook':str(restored_book)}
     restore_info=restored['wallet']
    else:restore_info=test.wallet_call('wallet-restore',extra={'restoreApproval':dict(approval,approvedDigest=verified['backupDigest'])})['wallet']
    assert restore_info['restored'] and restore_info['backupDigest']==saved['backupDigest']
    assert (restored_vault/'litecoin-seed.vault').read_bytes()==original_vault
    assert (restored_book/'litecoin-address-book.json').read_bytes()==original_book
    assert test.records()==original_claims
    # Make the original key/allocation copies unavailable before proving use of the restored files.
    test.vault_file.unlink();(test.book/'litecoin-address-book.json').unlink()
   signed=test.wallet_call('wallet-sign',extra=request)['wallet']
   signed_raw=(test.spends/f'ltc-signed-{signed["signingCommitment"]}.tx').read_bytes()
   manifest=json.loads((test.spends/f'ltc-spend-{test.operation}.json').read_bytes())
   assert signing.verify_signed(signed_raw,manifest,test.address)==signed['txid']
   test.value['wallet'].pop('vault');test.save_config()
   send=test.send_request(request,signed)
   def qt_call(mode):
    review=dict(signed,watchOnly=True,signed=False);del review['broadcast']
    packet=dict(test.status_request(send),savedReview=review)
    return run_qt_probe(test,probe,helper,fixture,mode,packet,1000 if mode=='wallet-reconcile' else 3)
   def reconcile(expected):
    records=test.records()
    result=qt_call('wallet-reconcile') if probe is not None else test.wallet_call('wallet-reconcile',extra=dict(test.status_request(send),maximumDownloadHeaders=1000,maximumHistoryEntries=10))
    if probe is not None:
     assert result['state']=='wallet-reconciled'
     assert not result['walletBroadcastAvailable'] and not result['walletSignAvailable']
    observation=result['wallet']['paymentObservation']
    assert result['wallet']['confirmationStatus']==expected
    assert result['chain']['height']==len(peer.branch) and result['chain']['tip']==signing.h.block_hash(peer.branch[-1])
    assert observation['selectedHeight']==result['chain']['height'] and observation['selectedTip']==result['chain']['tip']
    assert test.records()==records
    if expected=='included':
     assert observation['height']==included_height and observation['blockHash']==confirmed_block
     assert observation['confirmations']==1
    else:assert observation['height'] is None and observation['blockHash'] is None and observation['confirmations']==0
    observations.append(dict(observation,status=expected))
   if args.reconcile:reconcile('not-reported');assert sends==[]
   if probe is not None:
    result=qt_call('wallet-broadcast')
    assert result['state']==('unknown' if drop_ack else 'wallet-broadcast-acknowledged')
    if drop_ack:assert result['wallet']=={} and result['chain']=={}
    else:assert result['wallet']['broadcastStatus']=='server-acknowledged' and result['wallet']['confirmationStatus']=='unassessed'
   elif drop_ack:test.wallet_call('wallet-broadcast',success=False,extra=send)
   else:
    receipt=test.wallet_call('wallet-broadcast',extra=send)['wallet']
    assert receipt['broadcastStatus']=='server-acknowledged' and receipt['confirmationStatus']=='unassessed'
   assert sends==[signed_raw] and signed['txid'] in node.rpc('getrawmempool')
   assert node.rpc('getrawtransaction',signed['txid'])==signed_raw.hex()
   if args.reconcile:reconcile('reported-pending')
   confirmed_block=node.mine(1,anchor)[0]
   included_height=node.rpc('getblockcount');phase[0]='included'
   confirmed=node.rpc('getrawtransaction',signed['txid'],True)
   assert confirmed['blockhash']==confirmed_block and confirmed['confirmations']==1
   assert signed['txid'] in node.rpc('getblock',confirmed_block,1)['tx']
   if args.reconcile:
    peer.branch.append(bytes.fromhex(node.rpc('getblockheader',confirmed_block,False)))
    reconcile('included')
   if args.reorg:
    node.rpc('invalidateblock',confirmed_block);node.stop()
    # Only this disposable node's mempool is removed, after it has stopped.
    # The transaction must be absent from the replacement branch for this case.
    (node_dir/'regtest/mempool.dat').unlink();node.start()
    assert signed['txid'] not in node.rpc('getrawmempool')
    replacements=node.mine(2,anchor)
    assert all(signed['txid'] not in node.rpc('getblock',block_hash,1)['tx'] for block_hash in replacements)
    peer.branch=[bytes.fromhex(node.rpc('getblockheader',node.rpc('getblockhash',i),False)) for i in range(1,node.rpc('getblockcount')+1)]
    phase[0]='missing';reconcile('not-reported')
    assert observations[-1]['selectedHeight']>included_height and observations[-1]['selectedTip']!=confirmed_block
    records=test.records();phase[0]='stale-proof'
    if probe is not None:
     failed=qt_call('wallet-reconcile')
     assert failed['state']=='unknown' and failed['wallet']=={} and failed['chain']=={}
    else:test.wallet_call('wallet-reconcile',success=False,extra=dict(test.status_request(send),maximumDownloadHeaders=1000,maximumHistoryEntries=10))
    assert test.records()==records;phase[0]='missing'
  # The helper's offline status must not invent the confirmation just proved by RPC.
  recovered=(qt_call('wallet-broadcast-status') if probe is not None else test.wallet_call('wallet-broadcast-status',extra=test.status_request(send)))['wallet']
  expected_status='outcome-unknown' if drop_ack else 'server-acknowledged'
  assert recovered['broadcastStatus']==expected_status and recovered['confirmationStatus']=='unassessed'
  assert all(digest(path)==value for path,value in evidence.items())
  report.write_text(json.dumps({'status':'passed','fixture':'actual-isolated-litecoin-light-send-v1',
   'network':'regtest','nodeVersion':node.rpc('getnetworkinfo')['subversion'],
   'creditTxid':credit,'selectedHeight':height,'selectedTip':credit_block,
   'txid':signed['txid'],'signedSha256':hashlib.sha256(signed_raw).hexdigest(),
   'mempoolAccepted':True,'initialConfirmationBlock':confirmed_block,'initialConfirmations':1,
   'confirmationBlock':None if args.reorg else confirmed_block,'confirmations':0 if args.reorg else 1,
   'helperConfirmationStatus':recovered['confirmationStatus'],'helperBroadcastStatus':expected_status,
   'seedEncoding':os.environ.get('MONZERO_TEST_LIGHT_SEED_ENCODING','raw-bip32-seed-hex-v1'),'createdWallet':args.create_wallet,'createdAndOpenedByQt':args.qt_create,'creationReceipt':creation,
   'restoredBeforeSigning':args.restore_before_sign,'restoredByQt':args.qt_restore,'restoreReceipt':restore_info,
   'replyDroppedAfterNodeAcceptance':drop_ack,'broadcastCalls':len(sends),'qtWorker':probe is not None,
   'reconciliation':observations,'reorganizationChecked':args.reorg,'staleProofRejected':args.reorg,
   'elapsedSeconds':time.time()-started,'evidence':evidence},indent=2)+'\n')
  print('PASS: actual Litecoin payment accepted; '+('reconciliation removed inclusion and rejected stale proof after reorg' if args.reorg else 'confirmed in a block')+'; helper send status '+expected_status)
 finally:
  if node is not None:node.stop()
  test.tearDown()

if __name__=='__main__':run()
