#!/usr/bin/env python3
"""Qt watch-only worker fixtures: real helper TLS and strict response boundaries."""
import json,os,subprocess,unittest
import test_litecoin_light_qt as q
from generate_litecoin_watch_descriptor import descriptor
from pathlib import Path
CORE=Path(__file__).resolve().parents[3]
FUNDED=json.loads((CORE/'build/eigenwallet-engine-port/swap-machine/src/litecoin_light/session/wallet/fixture.json').read_text())
VECTORS=json.loads((CORE/'build/eigenwallet-engine-port/swap-machine/src/litecoin_light/account/vectors.json').read_text())
import hashlib
def script_hash(branch):
 script=next(v['script'] for v in VECTORS['vectors'] if v['network']=='regtest' and v['account']==0 and v['branch']==branch and v['index']==0)
 return hashlib.sha256(bytes.fromhex(script)).digest()[::-1].hex()
class WalletQtTests(unittest.TestCase):
 tearDown=q.QtTests.tearDown
 save=q.QtTests.save
 def setUp(self):
  q.QtTests.setUp(self);self.book=self.path/'book';self.book.mkdir(mode=0o700)
  self.f.value['wallet']={'account':descriptor(),'addressBookDirectory':str(self.book)}
  self.f.value['endpoint']={'host':'localhost','port':1};self.save()
 def call(self,operation='wallet-refresh',budget=3):
  env=os.environ.copy();env['XDG_CONFIG_HOME']=str(self.path/'settings');env['SSL_CERT_FILE']=str(q.FIXTURE/'root.pem')
  args=[str(q.PROBE),str(self.wrapper),operation,str(budget)]
  if operation.startswith('wallet-prepare'):args.append(json.dumps(self.payment()))
  if operation=='wallet-estimate':args.append(json.dumps(self.quote()))
  if operation=='wallet-reopen':args.append(json.dumps(self.recovery))
  result=subprocess.run(args,capture_output=True,env=env,timeout=20)
  self.assertEqual(result.returncode,0,result.stderr.decode());return json.loads(result.stdout)
 def payment(self):
  address=next(v['address'] for v in VECTORS['vectors'] if v['network']=='regtest' and v['account']==0 and v['branch']==0 and v['index']==0)
  return {'address':address,'amountAtomic':'40000','feeAtomic':'1000','minimumChangeAtomic':'1000','minimumConfirmations':1}
 def quote(self):
  value=self.payment();del value['feeAtomic'];value.update(targetBlocks=6,maximumRateAtomicPerKvb='2000',maximumFeeAtomic='1000');return value
 def test_real_fee_estimate_does_not_reserve_inputs_or_addresses(self):
  self.call('wallet-init');before=(self.book/'litecoin-address-book.json').read_bytes();block=FUNDED['blocks'][0];rate=[0.00001]
  def wallet(method,params):
   if method=='blockchain.estimatefee':return rate[0]
   if method=='blockchain.scripthash.get_history':return [{'tx_hash':block['txid'],'height':1}] if params[0]==script_hash(0) else []
   if method=='blockchain.transaction.get':return block['transaction']
   if method=='blockchain.transaction.get_merkle':return {'block_height':1,'pos':0,'merkle':[]}
   raise AssertionError(method)
  with q.base.Peer(self.path,[bytes.fromhex(block['header'])],wallet=wallet) as peer:
   self.f.value['endpoint']['port']=peer.port;self.save();self.call('sync')
   result=self.call('wallet-estimate');self.assertEqual(result['state'],'wallet-estimated');self.assertTrue(result['walletEstimateAvailable']);self.assertFalse(result['walletPrepareConfigured'])
   e=result['wallet']['feeEstimate'];self.assertEqual(e['feeAtomic'],str(e['virtualBytes']));self.assertEqual(e['changeAtomic'],str(60000-int(e['feeAtomic'])))
   self.assertFalse(result['wallet']['prepared']);self.assertEqual((self.book/'litecoin-address-book.json').read_bytes(),before)
   rate[0]=-1;self.assertIsNone(self.call('wallet-estimate')['wallet']['feeEstimate'])
   rate[0]=0.00003;self.assertEqual(self.call('wallet-estimate')['state'],'unknown')
 def test_estimate_response_boundaries(self):
  for mode in ['normal','rounded','nochange','unavailable','amount','target','rate','fee','rounding','change','input','size','prepared','extra','missing','overflow']:
   with self.subTest(mode=mode):
    self.fake('estimate-'+mode);result=self.call('wallet-estimate')
    self.assertEqual(result['state'],'wallet-estimated' if mode in ['normal','rounded','nochange','unavailable'] else 'unknown')
    if mode not in ['normal','rounded','nochange','unavailable']:self.assertEqual(result['wallet'],{})
 def test_estimate_request_bounds(self):
  self.fake('estimate-normal')
  for field,value in [('targetBlocks',0),('targetBlocks',1009),('maximumRateAtomicPerKvb','0'),('maximumFeeAtomic','01'),('amountAtomic','18446744073709551616'),('minimumConfirmations',0)]:
   with self.subTest(field=field,value=value):
    quote=self.quote();quote[field]=value
    result=subprocess.run([str(q.PROBE),str(self.wrapper),'wallet-estimate','3',json.dumps(quote)],capture_output=True,timeout=20)
    self.assertEqual(result.returncode,5);self.assertEqual(result.stdout,b'')
 def spend_config(self):
  directory=self.path/'spends'
  if not directory.exists():directory.mkdir(mode=0o700)
  self.f.value['wallet']['spendDirectory']=str(directory);self.save()
 def test_real_preparation_worker_retains_review_across_processes(self):
  self.spend_config();self.call('wallet-init');block=FUNDED['blocks'][0]
  def wallet(method,params):
   if method=='blockchain.scripthash.get_history':return [{'tx_hash':block['txid'],'height':1}] if params[0]==script_hash(0) else []
   if method=='blockchain.transaction.get':return block['transaction']
   if method=='blockchain.transaction.get_merkle':return {'block_height':1,'pos':0,'merkle':[]}
   raise AssertionError(method)
  with q.base.Peer(self.path,[bytes.fromhex(block['header'])],wallet=wallet) as peer:
   self.f.value['endpoint']['port']=peer.port;self.save();self.call('sync')
   first=self.call('wallet-prepare-current');self.assertEqual(first['state'],'wallet-prepared');self.assertTrue(first['walletPrepareAvailable']);self.assertTrue(first['walletPrepareConfigured'])
   self.assertEqual(first['wallet']['changeAtomic'],'59000');self.assertFalse(first['wallet']['signed'])
   self.assertEqual(self.call('wallet-prepare-current')['wallet'],first['wallet'])
   saved_request=json.loads(next((self.path/'spends').glob('ltc-ui-request-*.json')).read_text())
   self.recovery={'requestId':saved_request['requestId'],'payment':self.payment(),'retainedReview':{'signingCommitment':first['wallet']['signingCommitment'],'changeAddress':first['wallet']['changeAddress']}}
   before={str(p):p.read_bytes() for directory in [self.book,self.path/'spends'] for p in directory.iterdir()}
   reopened=self.call('wallet-reopen');self.assertEqual(reopened['state'],'wallet-reopened');self.assertEqual(reopened['wallet'],first['wallet'])
   self.assertTrue(reopened['walletReopenAvailable']);self.assertEqual({str(p):p.read_bytes() for directory in [self.book,self.path/'spends'] for p in directory.iterdir()},before)
   records=list((self.path/'spends').glob('ltc-ui-request-*.json'));self.assertEqual(len(records),1)
   saved=json.loads(records[0].read_text());self.assertEqual(saved['context']['payment'],self.payment())
   self.assertEqual(records[0].stat().st_mode&0o777,0o600)
   loaded=self.call('wallet-requests');self.assertEqual(loaded['state'],'wallet-requests');self.assertEqual(loaded['wallet'],{})
   self.assertEqual(loaded['paymentRequests'],[{'requestId':saved['requestId'],'payment':self.payment(),'scan':{'gap':1,'addressesPerBranch':3,'historyEntriesPerBranch':10},'review':first['wallet']}])
   self.assertEqual(json.loads(records[0].read_text()),saved)
   records[0].write_text('{}')
   env=os.environ.copy();env['SSL_CERT_FILE']=str(q.FIXTURE/'root.pem')
   result=subprocess.run([str(q.PROBE),str(self.wrapper),'wallet-prepare-current','3',json.dumps(self.payment())],capture_output=True,env=env,timeout=20)
   self.assertEqual(result.returncode,5);self.assertEqual(result.stdout,b'');self.assertEqual(records[0].read_text(),'{}')
 def test_payment_request_store_refuses_busy_unsafe_and_linked_records(self):
  import fcntl
  self.spend_config();self.fake('prepare-normal');directory=self.path/'spends'
  def rejected():
   result=subprocess.run([str(q.PROBE),str(self.wrapper),'wallet-prepare-current','3',json.dumps(self.payment())],capture_output=True,timeout=20)
   self.assertEqual(result.returncode,5);self.assertEqual(result.stdout,b'')
  fd=os.open(directory,os.O_RDONLY|os.O_DIRECTORY)
  try:
   fcntl.flock(fd,fcntl.LOCK_EX|fcntl.LOCK_NB);rejected();self.assertEqual(list(directory.iterdir()),[])
  finally:os.close(fd)
  self.assertEqual(self.call('wallet-prepare-current')['state'],'wallet-prepared')
  record=next(directory.glob('ltc-ui-request-*.json'));original=record.read_bytes()
  record.chmod(0o644);rejected();self.assertEqual(record.read_bytes(),original);record.chmod(0o600)
  target=directory/'saved';record.rename(target);record.symlink_to(target);rejected()
  self.assertEqual(target.read_bytes(),original)
 def test_saved_request_loading_rejects_hash_mismatch_and_wrong_wallet(self):
  self.spend_config();self.fake('prepare-normal');self.call('wallet-prepare-current')
  record=next((self.path/'spends').glob('ltc-ui-request-*.json'));original=record.read_bytes()
  value=json.loads(original);value['context']['payment']['amountAtomic']='40001';record.write_text(json.dumps(value,sort_keys=True,separators=(',',':')))
  result=subprocess.run([str(q.PROBE),str(self.wrapper),'wallet-requests','3'],capture_output=True,timeout=20)
  self.assertEqual(result.returncode,5);self.assertEqual(result.stdout,b'')
  record.write_bytes(original)
  self.f.value['wallet']['addressBookDirectory']=str(self.path/'another-book');self.save()
  loaded=self.call('wallet-requests');self.assertEqual(loaded['paymentRequests'],[])
 def test_saved_review_persists_and_rejects_replacement_or_unsafe_files(self):
  self.spend_config();self.fake('prepare-normal');first=self.call('wallet-prepare-current')
  directory=self.path/'spends';record=next(directory.glob('ltc-ui-review-*.json'));before=record.read_bytes()
  self.assertEqual(record.stat().st_mode&0o777,0o600)
  loaded=self.call('wallet-requests');self.assertEqual(loaded['paymentRequests'][0]['review'],first['wallet'])
  self.assertEqual(self.call('wallet-prepare-current')['wallet'],first['wallet']);self.assertEqual(record.read_bytes(),before)
  self.fake('prepare-valid-other-commitment');changed=self.call('wallet-prepare-current')
  self.assertEqual(changed['state'],'unknown');self.assertEqual(changed['wallet'],{});self.assertEqual(record.read_bytes(),before)
  def rejected():
   result=subprocess.run([str(q.PROBE),str(self.wrapper),'wallet-requests','3'],capture_output=True,timeout=20)
   self.assertEqual(result.returncode,5);self.assertEqual(result.stdout,b'')
  record.chmod(0o644);rejected();record.chmod(0o600)
  record.write_text('{}');rejected();record.write_bytes(before)
  value=json.loads(before);value['review']['paymentAtomic']='40001';record.write_text(json.dumps(value,sort_keys=True,separators=(',',':')));rejected();record.write_bytes(before)
  target=directory/'saved-review';record.rename(target);record.symlink_to(target);rejected();record.unlink();target.rename(record)
  record.unlink();legacy=self.call('wallet-requests');self.assertNotIn('review',legacy['paymentRequests'][0]);self.assertFalse(record.exists())
  self.fake('prepare-missing-ui-request');lost=self.call('wallet-prepare-current');self.assertEqual(lost['state'],'unknown');self.assertEqual(lost['wallet'],{});self.assertFalse(record.exists())
 def test_reopened_response_is_bound_to_retained_commitment_and_change(self):
  self.spend_config()
  self.recovery={'requestId':'11111111-2222-4333-8444-555555555555','payment':self.payment(),'retainedReview':{'signingCommitment':'2'*64,'changeAddress':'rltc1q'+'q'*38}}
  for mode in ['normal','commitment','change','amount','fee','signed']:
   with self.subTest(mode=mode):
    self.fake('reopen-'+mode);value=self.call('wallet-reopen')
    self.assertEqual(value['state'],'wallet-reopened' if mode=='normal' else 'unknown')
    if mode!='normal':self.assertEqual(value['wallet'],{})
  for review in [{'signingCommitment':'2'*64},{'signingCommitment':'wrong','changeAddress':None},{'signingCommitment':'2'*64,'changeAddress':'ltc1q'+'q'*38}]:
   recovery=dict(self.recovery,retainedReview=review)
   result=subprocess.run([str(q.PROBE),str(self.wrapper),'wallet-reopen','3',json.dumps(recovery)],capture_output=True,timeout=20)
   self.assertEqual(result.returncode,5);self.assertEqual(result.stdout,b'')
 def test_preparation_response_boundaries(self):
  self.spend_config()
  for mode in ['normal','amount','fee','signed','commitment','change','input','extra']:
   with self.subTest(mode=mode):
    self.fake('prepare-'+mode);value=self.call('wallet-prepare')
    self.assertEqual(value['state'],'wallet-prepared' if mode=='normal' else 'unknown')
    if mode!='normal':self.assertEqual(value['wallet'],{})
 def test_real_wallet_worker_initializes_refreshes_and_clears_failed_snapshot(self):
  self.assertEqual(self.call()['state'],'unknown')
  init=self.call('wallet-init');self.assertEqual(init['state'],'wallet-ready');self.assertTrue(init['wallet']['initialized'])
  blocks=FUNDED['blocks'];credit,spend=blocks;corrupt=[False];by_id={b['txid']:b for b in blocks}
  def wallet(method,params):
   if method=='blockchain.scripthash.get_history':
    a={'tx_hash':credit['txid'],'height':1};b={'tx_hash':spend['txid'],'height':2}
    return [a,b] if params[0]==script_hash(0) else [b] if params[0]==script_hash(1) else []
   if method=='blockchain.transaction.get':
    raw=by_id[params[0]]['transaction'];return raw[:-2]+'01' if corrupt[0] else raw
   if method=='blockchain.transaction.get_merkle':return {'block_height':by_id[params[0]]['height'],'pos':0,'merkle':[]}
   raise AssertionError(method)
  with q.base.Peer(self.path,[bytes.fromhex(b['header']) for b in blocks],wallet=wallet) as peer:
   self.f.value['endpoint']['port']=peer.port;self.save();self.assertEqual(self.call('sync')['state'],'checked')
   value=self.call();self.assertEqual(value['state'],'wallet-checked');self.assertEqual(value['wallet']['observedConfirmedAtomic'],'60000')
   self.assertFalse(value['wallet']['spendable']);self.assertFalse(value['executable'])
   failed=self.call(budget=1);self.assertEqual(failed['state'],'unknown');self.assertEqual(failed['wallet'],{})
   corrupt[0]=True;failed=self.call();self.assertEqual(failed['wallet'],{});self.assertEqual(failed['chain'],{})
 def test_receiving_address_worker_preserves_retry_identity(self):
  self.assertEqual(self.call('wallet-receive')['state'],'unknown')
  self.call('wallet-init')
  first=self.call('wallet-receive');self.assertEqual(first['state'],'wallet-address')
  expected=next(v for v in VECTORS['vectors'] if v['network']=='regtest' and v['account']==0 and v['branch']==0 and v['index']==0)
  self.assertEqual(first['wallet']['address'],expected['address'])
  self.assertEqual(first['wallet']['derivationPath'],expected['path'])
  self.assertEqual(first['wallet']['receivingAllocated'],1)
  self.assertEqual(self.call('wallet-receive')['wallet'],first['wallet'])
  self.assertFalse(first['wallet']['spendable']);self.assertTrue(first['walletReceiveAvailable'])
  (self.book/'litecoin-address-book.json').write_text('{}')
  failed=self.call('wallet-receive');self.assertEqual(failed['state'],'unknown');self.assertEqual(failed['wallet'],{})
 def fake(self,mode):
  self.program=self.path/'wallet-helper.py';self.program.write_text('''#!/usr/bin/python3
import json,sys
from pathlib import Path
request=json.load(sys.stdin);mode=MODE
wallet={'watchOnly':True,'spendable':False,'balanceStatus':'provisional','observedConfirmedAtomic':'60000','immatureAtomic':'0','pendingSpentAtomic':'0','pendingReceivedAtomic':'0','pendingTransactions':0,'confirmedCoinCount':1,'receiveScanned':2,'changeScanned':2,'scanStop':'reported-gap'}
response={'schema':'monzero-ltc-light-wallet-refresh-response-v1','requestId':request['requestId'],'executable':False,'chain':{'network':'regtest','height':2,'tip':'1'*64,'chainWork':'6','freshness':'unassessed'},'wallet':wallet}
if mode.startswith('receive-'):
 response['schema']='monzero-ltc-light-wallet-receive-response-v1'
 wallet={'watchOnly':True,'spendable':False,'address':'rltc1q'+'q'*38,'derivationPath':"84'/1'/0'/0/0",'receivingAllocated':1,'changeAllocated':0}
 response['wallet']=wallet
 if mode=='receive-network':wallet['address']='ltc1q'+'q'*38
 if mode=='receive-path':wallet['derivationPath']="84'/1'/1'/0/0"
 if mode=='receive-branch':wallet['derivationPath']="84'/1'/0'/1/0"
 if mode=='receive-index':wallet['derivationPath']="84'/1'/0'/0/1"
 if mode=='receive-zero':wallet['receivingAllocated']=0
 if mode=='receive-count':wallet['changeAllocated']=10000
 if mode=='receive-extra':wallet['extra']=True
 if mode=='receive-spendable':wallet['spendable']=True
if mode.startswith('estimate-'):
 response['schema']='monzero-ltc-light-wallet-estimate-response-v1'
 estimate={'targetBlocks':6,'rateAtomicPerKvb':'1000','virtualBytes':141,'feeAtomic':'141','inputCount':1,'changeAtomic':'59859'}
 wallet={'watchOnly':True,'spendable':False,'prepared':False,'paymentAddress':request['feeQuote']['address'],'paymentAtomic':'40000','feeEstimate':estimate};response['wallet']=wallet
 if mode=='estimate-rounded':estimate.update(rateAtomicPerKvb='1001',feeAtomic='142',changeAtomic='59858')
 if mode=='estimate-nochange':estimate['changeAtomic']='0'
 if mode=='estimate-unavailable':wallet['feeEstimate']=None
 if mode=='estimate-amount':wallet['paymentAtomic']='40001'
 if mode=='estimate-target':estimate['targetBlocks']=7
 if mode=='estimate-rate':estimate['rateAtomicPerKvb']='2001'
 if mode=='estimate-fee':estimate['feeAtomic']='1001'
 if mode=='estimate-rounding':estimate['feeAtomic']='140'
 if mode=='estimate-change':estimate['changeAtomic']='999'
 if mode=='estimate-input':estimate['inputCount']=0
 if mode=='estimate-size':estimate['virtualBytes']=100001
 if mode=='estimate-prepared':wallet['prepared']=True
 if mode=='estimate-extra':estimate['extra']=True
 if mode=='estimate-missing':del wallet['feeEstimate']
 if mode=='estimate-overflow':estimate.update(rateAtomicPerKvb='18446744073709551615',virtualBytes=100000,feeAtomic='1')
if mode.startswith('prepare-') or mode.startswith('reopen-'):
 response['schema']='monzero-ltc-light-wallet-reopen-response-v1' if mode.startswith('reopen-') else 'monzero-ltc-light-wallet-prepare-response-v1'
 if mode=='reopen-commitment':mode='prepare-valid-other-commitment'
 elif mode=='reopen-change':mode='prepare-valid-other-change'
 else:mode=mode.replace('reopen-','prepare-')
 wallet={'watchOnly':True,'spendable':False,'prepared':True,'signed':False,'paymentAddress':request['payment']['address'],'paymentAtomic':'40000','feeAtomic':'1000','changeAddress':'rltc1q'+'q'*38,'changeAtomic':'59000','inputCount':1,'txid':'1'*64,'signingCommitment':'2'*64}
 response['wallet']=wallet
 if mode=='prepare-missing-ui-request':
  config=json.loads(Path(sys.argv[2]).read_text())
  for path in Path(config['wallet']['spendDirectory']).glob('ltc-ui-request-*.json'):path.unlink()
 if mode=='prepare-valid-other-commitment':wallet['signingCommitment']='3'*64
 if mode=='prepare-valid-other-change':wallet['changeAddress']='rltc1q'+'p'*38
 if mode=='prepare-amount':wallet['paymentAtomic']='40001'
 if mode=='prepare-fee':wallet['feeAtomic']='1001'
 if mode=='prepare-signed':wallet['signed']=True
 if mode=='prepare-commitment':wallet['signingCommitment']='bad'
 if mode=='prepare-change':wallet['changeAtomic']='1'
 if mode=='prepare-input':wallet['inputCount']=0
 if mode=='prepare-extra':wallet['extra']=True
if mode=='second-bad':
 marker=Path(sys.argv[2]+'.seen');mode='spendable' if marker.exists() else 'normal';marker.touch()
if mode=='spendable':wallet['spendable']=True
if mode=='watch':wallet['watchOnly']=False
if mode=='status':wallet['balanceStatus']='verified'
if mode=='amount':wallet['observedConfirmedAtomic']=60000
if mode=='leading-zero':wallet['observedConfirmedAtomic']='060000'
if mode=='overflow':wallet['observedConfirmedAtomic']='18446744073709551616'
if mode=='negative':wallet['pendingSpentAtomic']='-1'
if mode=='scan':wallet['receiveScanned']=4
if mode=='gap':wallet['scanStop']='address-budget'
if mode=='pending':wallet['pendingTransactions']=21
if mode=='count':wallet['confirmedCoinCount']=0
if mode=='extra':wallet['extra']=True
if mode=='identity':response['requestId']='other'
if mode=='schema':response['schema']='monzero-ltc-light-status-response-v1'
sys.stdout.write(json.dumps(response,sort_keys=True,separators=(',',':')))
'''.replace('MODE',repr(mode)));self.program.chmod(0o700);self.save()
 def test_current_receiving_identity_survives_worker_restart(self):
  self.call('wallet-init')
  first=self.call('wallet-current');self.assertEqual(first['state'],'wallet-address')
  record=self.book/'litecoin-address-book.json';before=record.read_bytes()
  self.spend_config() # Adding preparation support must not change the receiving retry ID.
  self.assertEqual(self.call('wallet-current')['wallet'],first['wallet'])
  self.assertEqual(record.read_bytes(),before)
  settings=self.path/'settings/Monzero/LitecoinReceiveRequests.ini'
  self.assertTrue(settings.exists());self.assertNotIn(first['wallet']['address'],settings.read_text())
  text=settings.read_text();settings.write_text(text.replace('=', '=invalid-',1))
  env=os.environ.copy();env['XDG_CONFIG_HOME']=str(self.path/'settings')
  result=subprocess.run([str(q.PROBE),str(self.wrapper),'wallet-current','3'],capture_output=True,env=env,timeout=20)
  self.assertEqual(result.returncode,5);self.assertEqual(record.read_bytes(),before)
 def test_receiving_response_boundaries(self):
  for mode in ['network','path','branch','index','zero','count','extra','spendable']:
   with self.subTest(mode=mode):
    self.fake('receive-'+mode);value=self.call('wallet-receive')
    self.assertEqual(value['state'],'unknown');self.assertEqual(value['wallet'],{})
 def test_wallet_response_boundaries(self):
  for mode in ['spendable','watch','status','amount','leading-zero','overflow','negative','scan','gap','pending','count','extra','identity','schema']:
   with self.subTest(mode=mode):
    self.fake(mode);value=self.call();self.assertEqual(value['state'],'unknown');self.assertEqual(value['wallet'],{});self.assertEqual(value['chain'],{})
 def test_wallet_failed_repeat_removes_previous_amounts(self):
  self.fake('second-bad');value=self.call('wallet-refresh-repeat')
  self.assertEqual(value['before']['wallet']['observedConfirmedAtomic'],'60000')
  self.assertEqual(value['after']['wallet'],{});self.assertEqual(value['after']['state'],'unknown')
if __name__=='__main__':unittest.main(verbosity=2)
