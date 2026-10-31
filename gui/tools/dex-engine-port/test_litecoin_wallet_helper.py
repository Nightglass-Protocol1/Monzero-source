#!/usr/bin/env python3
"""Private watch-only helper over loopback TLS; no real wallets or chain daemons."""
import hashlib,json,os,subprocess,unittest,uuid
from pathlib import Path
import test_litecoin_light_helper as h
from generate_litecoin_watch_descriptor import descriptor
CORE=Path(__file__).resolve().parents[3]
RUST=CORE/'build/eigenwallet-engine-port'
FUNDED=json.loads((RUST/'swap-machine/src/litecoin_light/session/wallet/fixture.json').read_text())
VECTORS=json.loads((RUST/'swap-machine/src/litecoin_light/account/vectors.json').read_text())
def script_hash(branch):
 script=next(v['script'] for v in VECTORS['vectors'] if v['network']=='regtest' and v['account']==0 and v['branch']==branch and v['index']==0)
 return hashlib.sha256(bytes.fromhex(script)).digest()[::-1].hex()
class WalletHelperTests(unittest.TestCase):
 setUp=h.HelperTests.setUp
 tearDown=h.HelperTests.tearDown
 save_config=h.HelperTests.save_config
 call=h.HelperTests.call
 endpoint=h.HelperTests.endpoint
 def configure_wallet(self,suffix=''):
  self.book=self.path/('book'+suffix);self.book.mkdir(mode=0o700)
  self.value['wallet']={'account':descriptor(),'addressBookDirectory':str(self.book)};self.save_config()
 def wallet_call(self,mode='wallet-refresh',success=True,scan=None,extra=None):
  identity=(extra or {}).get('requestId',str(uuid.uuid4()));request={'schema':f'monzero-ltc-light-{mode}-request-v1','requestId':identity}
  if mode in ['wallet-refresh','wallet-prepare','wallet-reopen','wallet-estimate','wallet-sign','wallet-broadcast']:request['walletScan']=scan or {'gap':1,'addressesPerBranch':3,'historyEntriesPerBranch':10}
  request.update(extra or {})
  env=os.environ.copy();env['SSL_CERT_FILE']=str(h.FIXTURE/'root.pem')
  result=subprocess.run([str(h.PROGRAM),f'--{mode}-stdio-v1',str(self.config)],input=json.dumps(request).encode(),capture_output=True,env=env,timeout=20)
  if not success:self.assertNotEqual(result.returncode,0);self.assertEqual(result.stdout,b'');return
  self.assertEqual(result.returncode,0,result.stderr.decode());self.assertEqual(result.stderr,b'')
  value=json.loads(result.stdout);self.assertEqual(value['requestId'],identity)
  self.assertEqual(value['schema'],f'monzero-ltc-light-{mode}-response-v1');self.assertFalse(value['executable'])
  self.assertFalse(value['wallet']['spendable']);self.assertEqual(value['wallet']['watchOnly'],mode not in ['wallet-sign','wallet-broadcast'])
  self.assertNotIn('accountXpub',result.stdout.decode());self.assertNotIn('synchronization',value)
  return value
 def test_explicit_initialization_and_descriptor_binding(self):
  self.configure_wallet();self.wallet_call(success=False)
  initial=self.wallet_call('wallet-init');self.assertTrue(initial['wallet']['initialized'])
  self.assertEqual(initial['wallet']['receivingAllocated'],0)
  record=self.book/'litecoin-address-book.json';before=record.read_bytes();self.wallet_call('wallet-init');self.assertEqual(record.read_bytes(),before)
  self.value['wallet']['account']['network']='test';self.save_config();self.wallet_call('wallet-init',success=False)
  self.assertEqual(record.read_bytes(),before)
  self.value['wallet']['account']=descriptor();self.save_config()
  self.wallet_call('wallet-init',success=False,extra={'walletScan':{'gap':1,'addressesPerBranch':3,'historyEntriesPerBranch':10}})
  self.wallet_call(success=False,extra={'maximumDownloadHeaders':1})
  self.wallet_call(success=False,scan={'gap':0,'addressesPerBranch':3,'historyEntriesPerBranch':10})
 def test_receiving_address_is_durable_and_retryable(self):
  self.configure_wallet();self.wallet_call('wallet-receive',success=False)
  self.wallet_call('wallet-init');identity=str(uuid.uuid4())
  first=self.wallet_call('wallet-receive',extra={'requestId':identity})['wallet']
  expected=next(v for v in VECTORS['vectors'] if v['network']=='regtest' and v['account']==0 and v['branch']==0 and v['index']==0)
  self.assertEqual(first['address'],expected['address'])
  self.assertEqual(first['receivingAllocated'],1);self.assertEqual(first['changeAllocated'],0)
  record=self.book/'litecoin-address-book.json';before=record.read_bytes()
  self.assertEqual(self.wallet_call('wallet-receive',extra={'requestId':identity})['wallet'],first)
  self.assertEqual(record.read_bytes(),before)
  second=self.wallet_call('wallet-receive')['wallet']
  self.assertNotEqual(second['address'],first['address']);self.assertEqual(second['receivingAllocated'],2)
  retry=self.wallet_call('wallet-receive',extra={'requestId':identity})['wallet']
  self.assertEqual(retry['address'],first['address']);self.assertEqual(retry['receivingAllocated'],2)
  before=record.read_bytes()
  self.wallet_call('wallet-receive',success=False,extra={'requestId':str(uuid.UUID(int=0))})
  self.wallet_call('wallet-receive',success=False,extra={'maximumDownloadHeaders':1})
  self.wallet_call('wallet-receive',success=False,extra={'walletScan':{'gap':1,'addressesPerBranch':3,'historyEntriesPerBranch':10}})
  self.value['wallet']['account']['network']='test';self.save_config()
  self.wallet_call('wallet-receive',success=False);self.assertEqual(record.read_bytes(),before)
 def test_funded_and_pending_refresh_across_processes(self):
  for pending in [False,True]:
   with self.subTest(pending=pending):
    self.cache=self.path/f'cache-{pending}';self.cache.mkdir(mode=0o700);self.value['cacheDirectory']=str(self.cache)
    self.configure_wallet(str(pending));self.wallet_call('wallet-init')
    blocks=FUNDED['blocks'];credit,spend=blocks;corrupt=[False]
    by_id={b['txid']:b for b in blocks}
    def wallet(method,params):
     if method=='blockchain.scripthash.get_history':
      a={'tx_hash':credit['txid'],'height':1};b={'tx_hash':spend['txid'],'height':0 if pending else 2}
      return [a,b] if params[0]==script_hash(0) else [b] if params[0]==script_hash(1) else []
     if method=='blockchain.transaction.get':
      raw=by_id[params[0]]['transaction'];return raw[:-2]+'01' if corrupt[0] else raw
     if method=='blockchain.transaction.get_merkle':return {'block_height':by_id[params[0]]['height'],'pos':0,'merkle':[]}
     raise AssertionError(method)
    branch=[bytes.fromhex(b['header']) for b in blocks[:1 if pending else 2]]
    with h.Peer(self.path,branch,wallet=wallet) as peer:
     self.endpoint(peer);self.call(sync=True)
     value=self.wallet_call();amounts=value['wallet'];self.assertEqual(amounts['balanceStatus'],'provisional')
     self.assertEqual(amounts['observedConfirmedAtomic'],'0' if pending else '60000')
     self.assertEqual(amounts['pendingSpentAtomic'],'100000' if pending else '0')
     self.assertEqual(amounts['pendingReceivedAtomic'],'60000' if pending else '0')
     self.assertEqual(amounts['pendingTransactions'],int(pending));self.assertEqual(amounts['receiveScanned'],2)
     self.assertEqual(amounts['changeScanned'],2);self.assertEqual(value['chain']['height'],len(branch))
     self.assertEqual(self.wallet_call()['wallet'],amounts)
     self.wallet_call(success=False,scan={'gap':1,'addressesPerBranch':1,'historyEntriesPerBranch':10})
     corrupt[0]=True;self.wallet_call(success=False)
 def test_preparation_retains_review_and_retry_identity(self):
  self.configure_wallet();self.wallet_call('wallet-init')
  spend_dir=self.path/'spends';spend_dir.mkdir(mode=0o700)
  self.value['wallet']['spendDirectory']=str(spend_dir);self.save_config()
  block=FUNDED['blocks'][0]
  def wallet(method,params):
   if method=='blockchain.scripthash.get_history':return [{'tx_hash':block['txid'],'height':1}] if params[0]==script_hash(0) else []
   if method=='blockchain.transaction.get':return block['transaction']
   if method=='blockchain.transaction.get_merkle':return {'block_height':1,'pos':0,'merkle':[]}
   raise AssertionError(method)
  address=next(v['address'] for v in VECTORS['vectors'] if v['network']=='regtest' and v['account']==0 and v['branch']==0 and v['index']==0)
  payment={'address':address,'amountAtomic':'40000','feeAtomic':'1000','minimumChangeAtomic':'1000','minimumConfirmations':1}
  identity=str(uuid.uuid4());extra={'requestId':identity,'payment':payment}
  with h.Peer(self.path,[bytes.fromhex(block['header'])],wallet=wallet) as peer:
   self.endpoint(peer);self.call(sync=True)
   first=self.wallet_call('wallet-prepare',extra=extra)['wallet']
   self.assertTrue(first['prepared']);self.assertFalse(first['signed'])
   self.assertEqual(first['paymentAtomic'],'40000');self.assertEqual(first['feeAtomic'],'1000');self.assertEqual(first['changeAtomic'],'59000')
   self.assertEqual(first['inputCount'],1);self.assertEqual(len(first['txid']),64);self.assertEqual(len(first['signingCommitment']),64)
   records={p.name:p.read_bytes() for p in spend_dir.iterdir()}
   self.assertEqual(self.wallet_call('wallet-prepare',extra=extra)['wallet'],first)
   self.assertEqual({p.name:p.read_bytes() for p in spend_dir.iterdir()},records)
   self.wallet_call('wallet-prepare',success=False,extra={'requestId':identity,'payment':dict(payment,amountAtomic='40001')})
   self.assertEqual({p.name:p.read_bytes() for p in spend_dir.iterdir()},records)
   self.assertFalse(any(p.suffix=='.tx' for p in spend_dir.iterdir()))
   self.wallet_call('wallet-prepare',success=False,extra={'payment':dict(payment,feeAtomic='01')})
   self.wallet_call('wallet-refresh',success=False,extra={'payment':payment})
   self.wallet_call('wallet-prepare',success=False)
 def test_reopen_requires_saved_review_and_current_inputs_without_writes(self):
  for amount in ['40000','99000']:
   with self.subTest(amount=amount):
    self.cache=self.path/('cache-reopen-'+amount);self.cache.mkdir(mode=0o700);self.value['cacheDirectory']=str(self.cache)
    self.configure_wallet(amount);self.wallet_call('wallet-init')
    spends=self.path/('spends-'+amount);spends.mkdir(mode=0o700);self.value['wallet']['spendDirectory']=str(spends);self.save_config()
    credit,spent=FUNDED['blocks'];pending=[False]
    def wallet(method,params):
     if method=='blockchain.scripthash.get_history':
      a={'tx_hash':credit['txid'],'height':1};b={'tx_hash':spent['txid'],'height':0}
      return ([a,b] if pending[0] else [a]) if params[0]==script_hash(0) else [b] if pending[0] and params[0]==script_hash(1) else []
     if method=='blockchain.transaction.get':return next(b['transaction'] for b in FUNDED['blocks'] if b['txid']==params[0])
     if method=='blockchain.transaction.get_merkle':return {'block_height':1,'pos':0,'merkle':[]}
     raise AssertionError(method)
    address=next(v['address'] for v in VECTORS['vectors'] if v['network']=='regtest' and v['account']==0 and v['branch']==0 and v['index']==0)
    payment={'address':address,'amountAtomic':amount,'feeAtomic':'1000','minimumChangeAtomic':'1000','minimumConfirmations':1}
    identity=str(uuid.uuid4());extra={'requestId':identity,'payment':payment}
    with h.Peer(self.path,[bytes.fromhex(credit['header'])],wallet=wallet) as peer:
     self.endpoint(peer);self.call(sync=True)
     first=self.wallet_call('wallet-prepare',extra=extra)['wallet']
     review={'signingCommitment':first['signingCommitment'],'changeAddress':first['changeAddress']}
     request=dict(extra,retainedReview=review)
     before={str(p):p.read_bytes() for directory in [self.book,spends] for p in directory.iterdir()}
     self.assertEqual(self.wallet_call('wallet-reopen',extra=request)['wallet'],first)
     for bad in [dict(review,signingCommitment='0'*64),dict(review,changeAddress=address),dict(review,extra=True)]:
      self.wallet_call('wallet-reopen',success=False,extra=dict(extra,retainedReview=bad))
     self.wallet_call('wallet-reopen',success=False,extra=dict(request,requestId=str(uuid.uuid4())))
     for policy in [dict(payment,amountAtomic='40001'),dict(payment,feeAtomic='1001'),dict(payment,minimumConfirmations=2)]:
      self.wallet_call('wallet-reopen',success=False,extra=dict(request,payment=policy))
     self.wallet_call('wallet-reopen',success=False,extra=extra)
     self.wallet_call('wallet-prepare',success=False,extra=request)
     pending[0]=True;self.wallet_call('wallet-reopen',success=False,extra=request);pending[0]=False
     self.assertEqual({str(p):p.read_bytes() for directory in [self.book,spends] for p in directory.iterdir()},before)
     claim=next(spends.glob('ltc-input-*.claim'));claim.unlink()
     self.wallet_call('wallet-reopen',success=False,extra=request);self.assertFalse(claim.exists())
     self.assertFalse(any(p.suffix=='.tx' for p in spends.iterdir()))
 def test_fee_estimate_is_bounded_and_does_not_reserve_inputs_or_change(self):
  self.configure_wallet();self.wallet_call('wallet-init');block=FUNDED['blocks'][0];reported=[0.00001]
  before={p.name:p.read_bytes() for p in self.book.iterdir()}
  def wallet(method,params):
   if method=='blockchain.estimatefee':self.assertEqual(params,[6]);return reported[0]
   if method=='blockchain.scripthash.get_history':return [{'tx_hash':block['txid'],'height':1}] if params[0]==script_hash(0) else []
   if method=='blockchain.transaction.get':return block['transaction']
   if method=='blockchain.transaction.get_merkle':return {'block_height':1,'pos':0,'merkle':[]}
   raise AssertionError(method)
  address=next(v['address'] for v in VECTORS['vectors'] if v['network']=='regtest' and v['account']==0 and v['branch']==0 and v['index']==0)
  quote={'address':address,'amountAtomic':'40000','minimumChangeAtomic':'1000','minimumConfirmations':1,'targetBlocks':6,'maximumRateAtomicPerKvb':'2000','maximumFeeAtomic':'1000'}
  with h.Peer(self.path,[bytes.fromhex(block['header'])],wallet=wallet) as peer:
   self.endpoint(peer);self.call(sync=True)
   value=self.wallet_call('wallet-estimate',extra={'feeQuote':quote})['wallet'];self.assertFalse(value['prepared'])
   estimate=value['feeEstimate'];self.assertEqual(estimate['rateAtomicPerKvb'],'1000');self.assertEqual(estimate['targetBlocks'],6)
   self.assertEqual(int(estimate['feeAtomic']),estimate['virtualBytes']);self.assertEqual(int(estimate['changeAtomic']),60000-int(estimate['feeAtomic']))
   self.assertEqual(estimate['inputCount'],1);self.assertEqual(value['paymentAddress'],address)
   self.assertEqual({p.name:p.read_bytes() for p in self.book.iterdir()},before)
   self.wallet_call('wallet-estimate',success=False,extra={'feeQuote':dict(quote,maximumFeeAtomic='1')})
   reported[0]=-1;self.assertIsNone(self.wallet_call('wallet-estimate',extra={'feeQuote':quote})['wallet']['feeEstimate'])
   for rate in [1,-2,'0.00001']:
    reported[0]=rate;self.wallet_call('wallet-estimate',success=False,extra={'feeQuote':quote})
   self.assertEqual({p.name:p.read_bytes() for p in self.book.iterdir()},before)
   self.wallet_call('wallet-refresh',success=False,extra={'feeQuote':quote})
 def test_private_journal_and_config_boundaries(self):
  self.configure_wallet();self.wallet_call('wallet-init')
  self.config.chmod(0o644);self.wallet_call('wallet-init',success=False);self.config.chmod(0o600)
  self.book.chmod(0o755);self.wallet_call(success=False);self.book.chmod(0o700)
  record=self.book/'litecoin-address-book.json';record.write_text('{}');self.wallet_call(success=False)
  self.wallet_call('wallet-init',success=False)
if __name__=='__main__':unittest.main(verbosity=2)
