#!/usr/bin/env python3
"""Qt explicit signing over stdin, using PUBLIC keys and a loopback peer only."""
import json,os,subprocess,unittest
from pathlib import Path
import test_litecoin_wallet_qt as w
q=w.q
FIXTURE=json.loads(Path(__file__).with_name('litecoin-signing-fixture.json').read_text())
PASSWORD=FIXTURE['vault']['password']
ESCAPED='PUBLIC " \\ \n\x00 Ω TEST PASSWORD'
class SigningQtTests(unittest.TestCase):
 tearDown=w.WalletQtTests.tearDown
 save=w.WalletQtTests.save
 call=w.WalletQtTests.call
 spend_config=w.WalletQtTests.spend_config
 def setUp(self):
  w.WalletQtTests.setUp(self);self.spend_config()
  self.f.value['wallet']['account']=FIXTURE['account']
  directory=self.path/'vault';directory.mkdir(mode=0o700)
  vault=directory/'litecoin-seed.vault';vault.write_bytes(bytes.fromhex(FIXTURE['vault']['vault']));vault.chmod(0o600)
  self.metadata={'directory':str(directory),'walletId':FIXTURE['vault']['wallet']}
  self.f.value['wallet']['vault']=self.metadata;self.save()
  self.recovery={'requestId':'11111111-2222-4333-8444-555555555555','payment':self.payment(),
   'retainedReview':{'signingCommitment':'2'*64,'changeAddress':next(a['address'] for a in FIXTURE['addresses'] if a['branch']==1)}}
 def payment(self):
  return {'address':next(a['address'] for a in FIXTURE['addresses'] if a['branch']==0),'amountAtomic':'40000','feeAtomic':'1000','minimumChangeAtomic':'1000','minimumConfirmations':1}
 def sign(self,password=PASSWORD,mode='wallet-sign',success=True,packet=None):
  env=os.environ.copy();env['SSL_CERT_FILE']=str(q.FIXTURE/'root.pem')
  result=subprocess.run([str(q.PROBE),str(self.wrapper),mode,'3',json.dumps(packet or self.recovery)],input=password.encode(),capture_output=True,env=env,timeout=20)
  self.assertNotIn(password.encode(),result.stdout+result.stderr) if password else None
  if not success:self.assertEqual(result.returncode,5);self.assertEqual(result.stdout,b'');return
  self.assertEqual(result.returncode,0,result.stderr.decode());return json.loads(result.stdout)
 def recover(self,saved):
  packet=dict(self.recovery,savedReview=saved)
  result=subprocess.run([str(q.PROBE),str(self.wrapper),'wallet-signed','3',json.dumps(packet)],capture_output=True,timeout=20)
  self.assertEqual(result.returncode,0,result.stderr.decode());return json.loads(result.stdout)
 def broadcast(self,saved,mode='wallet-broadcast',success=True,**overrides):
  packet=dict(self.recovery,savedReview=saved,**overrides)
  env=os.environ.copy();env['SSL_CERT_FILE']=str(q.FIXTURE/'root.pem')
  result=subprocess.run([str(q.PROBE),str(self.wrapper),mode,'3',json.dumps(packet)],capture_output=True,env=env,timeout=20)
  if not success:self.assertEqual(result.returncode,5);self.assertEqual(result.stdout,b'');return
  self.assertEqual(result.returncode,0,result.stderr.decode());return json.loads(result.stdout)
 def exercise_broadcast(self,drop_ack):
  import hashlib
  credit=FIXTURE['blocks'][0];script=next(a['script'] for a in FIXTURE['addresses'] if a['branch']==0)
  script_hash=hashlib.sha256(bytes.fromhex(script)).digest()[::-1].hex();sent=[]
  def wallet(method,params):
   if method=='blockchain.scripthash.get_history':return [{'tx_hash':credit['txid'],'height':1}] if params[0]==script_hash else []
   if method=='blockchain.transaction.get':return credit['transaction']
   if method=='blockchain.transaction.get_merkle':return {'block_height':1,'pos':0,'merkle':[]}
   if method=='blockchain.transaction.broadcast':
    self.assertEqual(bytes.fromhex(params[0]),(self.path/'spends'/('ltc-signed-'+prepared['signingCommitment']+'.tx')).read_bytes())
    sent.append(params[0]);return q.base.DROP_RESPONSE if drop_ack else prepared['txid']
   raise AssertionError(method)
  self.call('wallet-init')
  with q.base.Peer(self.path,[bytes.fromhex(credit['header'])],wallet=wallet) as peer:
   self.f.value['endpoint']['port']=peer.port;self.save();self.call('sync')
   prepared=self.call('wallet-prepare-current')['wallet'];saved=self.call('wallet-requests')['paymentRequests'][0]
   self.recovery={'requestId':saved['requestId'],'payment':saved['payment'],
    'retainedReview':{'signingCommitment':prepared['signingCommitment'],'changeAddress':prepared['changeAddress']}}
   self.assertEqual(self.sign()['state'],'wallet-signed')
   self.f.value['wallet'].pop('vault');self.save()
   before=len(peer.requests);status=self.broadcast(prepared,'wallet-broadcast-status')
   self.assertEqual(len(peer.requests),before);self.assertEqual(status['wallet']['broadcastStatus'],'no-local-attempt')
   self.assertTrue(status['walletBroadcastAvailable']);self.assertFalse(status['walletSignConfigured'])
   self.broadcast(prepared,'wallet-broadcast-direct',success=False)
   result=self.broadcast(prepared)
   self.assertEqual(result['state'],'unknown' if drop_ack else 'wallet-broadcast-acknowledged')
   self.assertFalse(result['walletBroadcastAvailable']);self.assertEqual(len(sent),1)
   if drop_ack:self.assertEqual(result['wallet'],{});self.assertEqual(result['chain'],{})
   else:
    self.assertEqual(result['wallet']['txid'],prepared['txid']);self.assertEqual(result['wallet']['confirmationStatus'],'unassessed')
    self.assertEqual(result['wallet']['broadcastEndpoint'],self.f.value['endpoint'])
  records={p.name:p.read_bytes() for p in (self.path/'spends').iterdir()}
  recovered=self.broadcast(prepared,'wallet-broadcast-status')
  self.assertEqual(recovered['state'],'wallet-broadcast-status');self.assertTrue(recovered['wallet']['recovered'])
  self.assertEqual(recovered['wallet']['broadcastStatus'],'outcome-unknown' if drop_ack else 'server-acknowledged')
  self.assertEqual(recovered['walletBroadcastAvailable'],drop_ack)
  self.assertEqual({p.name:p.read_bytes() for p in (self.path/'spends').iterdir()},records)
 def test_real_broadcast_requires_status_and_recovers_ack_without_vault(self):self.exercise_broadcast(False)
 def test_real_lost_broadcast_reply_clears_display_and_recovers_unknown(self):self.exercise_broadcast(True)
 def test_real_signing_and_vault_addition_preserve_requests(self):
  del self.f.value['wallet']['vault'];self.save();self.call('wallet-init')
  receiving=self.call('wallet-current')['wallet']
  credit=FIXTURE['blocks'][0]
  script=next(a['script'] for a in FIXTURE['addresses'] if a['branch']==0)
  import hashlib
  script_hash=hashlib.sha256(bytes.fromhex(script)).digest()[::-1].hex()
  def wallet(method,params):
   self.assertNotIn(PASSWORD,json.dumps(params))
   if method=='blockchain.scripthash.get_history':return [{'tx_hash':credit['txid'],'height':1}] if params[0]==script_hash else []
   if method=='blockchain.transaction.get':return credit['transaction']
   if method=='blockchain.transaction.get_merkle':return {'block_height':1,'pos':0,'merkle':[]}
   raise AssertionError(method)
  with q.base.Peer(self.path,[bytes.fromhex(credit['header'])],wallet=wallet) as peer:
   self.f.value['endpoint']['port']=peer.port;self.save();self.call('sync')
   prepared=self.call('wallet-prepare-current')['wallet'];saved=self.call('wallet-requests')['paymentRequests'][0]
   self.f.value['wallet']['vault']=self.metadata;self.save()
   self.assertEqual(self.call('wallet-current')['wallet']['address'],receiving['address'])
   self.assertEqual(self.call('wallet-requests')['paymentRequests'][0],saved)
   self.recovery={'requestId':saved['requestId'],'payment':saved['payment'],'retainedReview':{'signingCommitment':prepared['signingCommitment'],'changeAddress':prepared['changeAddress']}}
   before={p.name:p.read_bytes() for p in (self.path/'spends').iterdir()}
   self.assertEqual(self.sign(password='WRONG PUBLIC PASSWORD')['state'],'unknown')
   self.assertEqual({p.name:p.read_bytes() for p in (self.path/'spends').iterdir()},before)
   result=self.sign();self.assertEqual(result['state'],'wallet-signed');self.assertTrue(result['wallet']['signed']);self.assertFalse(result['wallet']['broadcast'])
   self.assertFalse(result['walletSignAvailable']);self.assertFalse(result['wallet']['watchOnly']);self.assertFalse(result['executable'])
   self.assertEqual(result['wallet']['txid'],prepared['txid']);self.assertTrue((self.path/'spends'/('ltc-signed-'+prepared['signingCommitment']+'.tx')).exists())
   self.assertEqual(self.sign()['wallet'],result['wallet'])
   self.assertEqual(self.call('wallet-requests')['paymentRequests'][0],saved)
  self.f.value.pop('endpoint');self.f.value['wallet'].pop('vault');self.save()
  records={p.name:p.read_bytes() for p in (self.path/'spends').iterdir()}
  recovered=self.recover(prepared);self.assertEqual(recovered['state'],'wallet-signed-recovered')
  self.assertEqual(recovered['wallet']['txid'],prepared['txid']);self.assertEqual(recovered['wallet']['broadcastStatus'],'unassessed')
  self.assertTrue(recovered['wallet']['recovered']);self.assertFalse(recovered['walletSignConfigured']);self.assertTrue(recovered['walletSignedRecoveryAvailable'])
  self.assertEqual({p.name:p.read_bytes() for p in (self.path/'spends').iterdir()},records)
  signed_path=next((self.path/'spends').glob('ltc-signed-*.tx'));signed_path.write_bytes(b'corrupt')
  failed=self.recover(prepared);self.assertEqual(failed['state'],'unknown');self.assertEqual(failed['wallet'],{})
 def fake(self,mode):
  self.program=self.path/'sign-helper.py'
  source='''#!/usr/bin/python3
import json,sys
request=json.load(sys.stdin);operation=sys.argv[1][2:-9];mode=MODE
wallet={'watchOnly':True,'spendable':False,'prepared':True,'signed':False,'paymentAddress':request['payment']['address'],'paymentAtomic':'40000','feeAtomic':'1000','changeAddress':request['retainedReview']['changeAddress'],'changeAtomic':'59000','inputCount':1,'txid':'1'*64,'signingCommitment':request['retainedReview']['signingCommitment']}
if operation=='wallet-sign':
 assert request['signingApproval']['password']==PASSWORD
 assert all(PASSWORD not in arg for arg in sys.argv)
 assert request['signingApproval']['approvedCommitment']==wallet['signingCommitment']
 wallet.update(watchOnly=False,signed=True,broadcast=False)
 if mode=='commitment':wallet['signingCommitment']='3'*64
 if mode=='txid':wallet['txid']='3'*64
 if mode=='fee':wallet['feeAtomic']='1001'
 if mode=='broadcast':wallet['broadcast']=True
 if mode=='unsigned':wallet['signed']=False
 if mode=='extra':wallet['password']=PASSWORD
if operation=='wallet-signed':
 wallet.update(signed=True,recovered=True,broadcastStatus='unassessed')
 if mode=='recovery-status':wallet['broadcastStatus']='confirmed'
 if mode=='recovery-txid':wallet['txid']='3'*64
 if mode=='recovery-intent':wallet['recovered']=False
 if mode=='recovery-extra':wallet['broadcast']=False
if operation in ['wallet-broadcast-status','wallet-broadcast']:
 endpoint=json.load(open(sys.argv[2]))['endpoint']
 assert 'signingApproval' not in request
 wallet.update(signed=True,broadcastStatus='no-local-attempt',confirmationStatus='unassessed',broadcastEndpoint=endpoint)
 if operation=='wallet-broadcast-status':
  assert 'walletScan' not in request and 'broadcastApproval' not in request
  wallet['recovered']=True
  if mode=='prior-ack':wallet['broadcastStatus']='server-acknowledged'
  if mode=='prior-unknown':wallet['broadcastStatus']='outcome-unknown'
  if mode=='status-confirmed':wallet['confirmationStatus']='confirmed'
  if mode=='status-invalid':wallet['broadcastStatus']='not-sent'
  if mode=='status-endpoint':wallet['broadcastEndpoint']={'host':'other.invalid','port':1}
  if mode=='status-txid':wallet['txid']='3'*64
  if mode=='status-extra':wallet['broadcast']=False
 else:
  assert request['broadcastApproval']==dict(endpoint,approvedCommitment=wallet['signingCommitment'],approvedTxid=wallet['txid'])
  assert request['walletScan']=={'gap':1,'addressesPerBranch':3,'historyEntriesPerBranch':10}
  wallet.update(watchOnly=False,broadcastStatus='server-acknowledged')
  if mode=='send-confirmed':wallet['confirmationStatus']='confirmed'
  if mode=='send-invalid':wallet['broadcastStatus']='outcome-unknown'
  if mode=='send-endpoint':wallet['broadcastEndpoint']={'host':'other.invalid','port':1}
  if mode=='send-txid':wallet['txid']='3'*64
  if mode=='send-fee':wallet['feeAtomic']='999'
  if mode=='send-extra':wallet['broadcast']=True
  if mode=='send-cancel':
   import time
   time.sleep(2)
if operation=='wallet-reconcile':
 import time
 assert request['maximumDownloadHeaders']==3 and request['maximumHistoryEntries']==10
 assert all(k not in request for k in ['walletScan','signingApproval','broadcastApproval'])
 endpoint=json.load(open(sys.argv[2]))['endpoint']
 observation=dict(selectedHeight=1,selectedTip='1'*64,height=1,blockHash='1'*64,confirmations=1,observedAtUnix=int(time.time()))
 wallet.update(signed=True,recovered=True,broadcastStatus='outcome-unknown',broadcastEndpoint=endpoint,confirmationStatus='included',paymentObservation=observation)
 if mode in ['pending','absent']:
  wallet['confirmationStatus']='reported-pending' if mode=='pending' else 'not-reported'
  observation.update(height=None,blockHash=None,confirmations=0)
 if mode=='depth':observation['confirmations']=2
 if mode=='height':observation['height']=2
 if mode=='tip':observation['selectedTip']='3'*64
 if mode=='block':observation['blockHash']='3'*64
 if mode=='old':observation['observedAtUnix']-=100
 if mode=='future':observation['observedAtUnix']+=100
 if mode=='time-type':observation['observedAtUnix']=str(observation['observedAtUnix'])
 if mode=='txid':wallet['txid']='3'*64
 if mode=='endpoint':wallet['broadcastEndpoint']={'host':'other.invalid','port':1}
 if mode=='extra':observation['extra']=True
 if mode=='missing':del observation['height']
 if mode=='unknown':wallet['confirmationStatus']='confirmed'
 if mode=='pending-proof':wallet['confirmationStatus']='reported-pending'
 if mode=='cancel':time.sleep(2)
response={'schema':'monzero-ltc-light-'+operation+'-response-v1','requestId':request['requestId'],'executable':False,'chain':{'network':'regtest','height':1,'tip':'1'*64,'chainWork':'4','freshness':'unassessed'},'wallet':wallet}
print(json.dumps(response,sort_keys=True,separators=(',',':')),end='')
'''.replace('MODE',repr(mode)).replace('PASSWORD',repr(ESCAPED))
  self.program.write_text(source);self.program.chmod(0o700);self.save()
 def test_signed_response_and_stdin_escaping_boundaries(self):
  for mode in ['normal','commitment','txid','fee','broadcast','unsigned','extra']:
   with self.subTest(mode=mode):
    self.fake(mode);result=self.sign(password=ESCAPED)
    self.assertEqual(result['state'],'wallet-signed' if mode=='normal' else 'unknown')
    if mode!='normal':self.assertEqual(result['wallet'],{});self.assertEqual(result['chain'],{})
 def test_recovered_result_matches_full_saved_review_and_unknown_chain_status(self):
  self.f.value.pop('endpoint');self.f.value['wallet'].pop('vault');self.save()
  saved={'watchOnly':True,'spendable':False,'prepared':True,'signed':False,'paymentAddress':self.payment()['address'],'paymentAtomic':'40000','feeAtomic':'1000',
   'changeAddress':self.recovery['retainedReview']['changeAddress'],'changeAtomic':'59000','inputCount':1,'txid':'1'*64,'signingCommitment':'2'*64}
  for mode in ['normal','recovery-status','recovery-txid','recovery-intent','recovery-extra']:
   with self.subTest(mode=mode):
    self.fake(mode);result=self.recover(saved)
    self.assertEqual(result['state'],'wallet-signed-recovered' if mode=='normal' else 'unknown')
    if mode!='normal':self.assertEqual(result['wallet'],{})
 def test_signing_requires_same_process_refreshed_review_and_commitment(self):
  self.fake('normal')
  self.sign(password=ESCAPED,mode='wallet-sign-direct',success=False)
  self.sign(password=ESCAPED,packet=dict(self.recovery,approvedCommitment='0'*64),success=False)
  self.sign(password='',success=False);self.sign(password='x'*1025,success=False)
  del self.f.value['wallet']['vault'];self.save();self.sign(password=ESCAPED,success=False)
 def saved_review(self):
  return {'watchOnly':True,'spendable':False,'prepared':True,'signed':False,'paymentAddress':self.payment()['address'],
   'paymentAtomic':'40000','feeAtomic':'1000','changeAddress':self.recovery['retainedReview']['changeAddress'],
   'changeAtomic':'59000','inputCount':1,'txid':'1'*64,'signingCommitment':'2'*64}
 def test_send_status_and_receipts_reject_wrong_context_and_confirmation_claims(self):
  saved=self.saved_review()
  for mode in ['normal','prior-unknown','prior-ack','status-confirmed','status-invalid','status-endpoint','status-txid','status-extra']:
   with self.subTest(mode=mode):
    self.fake(mode);result=self.broadcast(saved,'wallet-broadcast-status')
    valid=mode in ['normal','prior-unknown','prior-ack']
    self.assertEqual(result['state'],'wallet-broadcast-status' if valid else 'unknown')
    self.assertEqual(result['walletBroadcastAvailable'],mode in ['normal','prior-unknown'])
    if not valid:self.assertEqual(result['wallet'],{})
  for mode in ['normal','send-confirmed','send-invalid','send-endpoint','send-txid','send-fee','send-extra','send-cancel']:
   with self.subTest(mode=mode):
    self.fake(mode);result=self.broadcast(saved,'wallet-broadcast-cancel' if mode=='send-cancel' else 'wallet-broadcast')
    self.assertEqual(result['state'],'wallet-broadcast-acknowledged' if mode=='normal' else 'unknown')
    if mode!='normal':self.assertEqual(result['wallet'],{});self.assertEqual(result['chain'],{})
 def test_reconciliation_receipts_require_current_time_exact_payment_and_chain(self):
  self.f.value['wallet'].pop('vault');self.save()
  for mode in ['normal','pending','absent','depth','height','tip','block','old','future','time-type','txid','endpoint','extra','missing','unknown','pending-proof','cancel']:
   with self.subTest(mode=mode):
    self.fake(mode);result=self.broadcast(self.saved_review(),'wallet-reconcile-cancel' if mode=='cancel' else 'wallet-reconcile')
    valid=mode in ['normal','pending','absent']
    self.assertEqual(result['state'],'wallet-reconciled' if valid else 'unknown')
    self.assertFalse(result['walletBroadcastAvailable']);self.assertFalse(result['walletSignAvailable'])
    if not valid:self.assertEqual(result['wallet'],{});self.assertEqual(result['chain'],{})
 def test_send_requires_same_worker_status_exact_approval_and_endpoint(self):
  saved=self.saved_review();self.fake('normal')
  self.broadcast(saved,'wallet-broadcast-direct',success=False)
  self.broadcast(saved,success=False,approvedCommitment='0'*64)
  self.broadcast(saved,success=False,approvedTxid='0'*64)
  self.broadcast(saved,success=False,approvedEndpoint={'host':'other.invalid','port':1})
  self.fake('prior-ack');self.broadcast(saved,success=False)
if __name__=='__main__':unittest.main(verbosity=2)
