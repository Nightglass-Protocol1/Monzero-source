#!/usr/bin/env python3
"""Explicit helper signing with PUBLIC encrypted seed fixtures and loopback TLS."""
import hashlib,json,struct,unittest,uuid
from pathlib import Path
from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.asymmetric import ec,utils
import test_litecoin_wallet_helper as w
h=w.h
FIXTURE=json.loads(Path(__file__).with_name('litecoin-signing-fixture.json').read_text())
def sha256d(value):return hashlib.sha256(hashlib.sha256(value).digest()).digest()
def verify_signed(raw,manifest,address):
 # Independent one-input BIP143 verifier for this bounded fixture transaction.
 assert raw[4:6]==b'\x00\x01' and raw[6]==1
 offset=7;outpoint=raw[offset:offset+36];offset+=36
 assert raw[offset]==0;offset+=1
 sequence=raw[offset:offset+4];offset+=4
 output_start=offset;count=raw[offset];offset+=1;assert count in [1,2]
 outputs_start=offset
 for _ in range(count):
  offset+=8;length=raw[offset];offset+=1+length
 outputs=raw[outputs_start:offset];output_end=offset
 assert raw[offset]==2;offset+=1
 length=raw[offset];offset+=1;signature=raw[offset:offset+length];offset+=length
 length=raw[offset];offset+=1;key=raw[offset:offset+length];offset+=length
 assert len(key)==33 and key.hex()==address['publicKey'] and signature[-1]==1
 locktime=raw[offset:offset+4];offset+=4;assert offset==len(raw)
 unsigned=raw[:4]+b'\x01'+outpoint+b'\x00'+sequence+raw[output_start:output_end]+locktime
 assert unsigned==bytes(manifest['transaction'])
 prevout=bytes(manifest['inputs'][0]['prevout']);assert prevout[8:]==b'\x16'+bytes.fromhex(address['script'])
 scriptcode=b'\x19\x76\xa9\x14'+bytes.fromhex(address['script'])[2:]+b'\x88\xac'
 digest=sha256d(raw[:4]+sha256d(outpoint)+sha256d(sequence)+outpoint+scriptcode+prevout[:8]+sequence+sha256d(outputs)+locktime+struct.pack('<I',1))
 ec.EllipticCurvePublicKey.from_encoded_point(ec.SECP256K1(),key).verify(signature[:-1],digest,ec.ECDSA(utils.Prehashed(hashes.SHA256())))
 return sha256d(unsigned)[::-1].hex()
class SigningHelperTests(unittest.TestCase):
 tearDown=w.WalletHelperTests.tearDown
 save_config=w.WalletHelperTests.save_config
 endpoint=w.WalletHelperTests.endpoint
 call=w.WalletHelperTests.call
 wallet_call=w.WalletHelperTests.wallet_call
 def setUp(self):
  w.WalletHelperTests.setUp(self)
  self.book=self.path/'book';self.book.mkdir(mode=0o700)
  self.spends=self.path/'spends';self.spends.mkdir(mode=0o700)
  self.vault=self.path/'vault';self.vault.mkdir(mode=0o700)
  self.vault_file=self.vault/'litecoin-seed.vault';self.vault_file.write_bytes(bytes.fromhex(FIXTURE['vault']['vault']));self.vault_file.chmod(0o600)
  self.value['wallet']={'account':FIXTURE['account'],'addressBookDirectory':str(self.book),'spendDirectory':str(self.spends),
   'vault':{'directory':str(self.vault),'walletId':FIXTURE['vault']['wallet']}}
  self.save_config();self.wallet_call('wallet-init');self.pending=False
  self.address=next(a for a in FIXTURE['addresses'] if a['branch']==0)
  self.payment={'address':self.address['address'],'amountAtomic':'40000','feeAtomic':'1000','minimumChangeAtomic':'1000','minimumConfirmations':1}
  self.operation=str(uuid.uuid4())
 def peer(self,broadcast=None):
  credit,spent=FIXTURE['blocks']
  scripts={a['branch']:hashlib.sha256(bytes.fromhex(a['script'])).digest()[::-1].hex() for a in FIXTURE['addresses']}
  def wallet(method,params):
   self.assertNotIn(FIXTURE['vault']['password'],json.dumps(params))
   if method=='blockchain.transaction.broadcast' and broadcast is not None:return broadcast(params)
   if method=='blockchain.scripthash.get_history':
    a={'tx_hash':credit['txid'],'height':1};b={'tx_hash':spent['txid'],'height':0}
    return ([a,b] if self.pending else [a]) if params[0]==scripts[0] else [b] if self.pending and params[0]==scripts[1] else []
   if method=='blockchain.transaction.get':return next(b['transaction'] for b in FIXTURE['blocks'] if b['txid']==params[0])
   if method=='blockchain.transaction.get_merkle':return {'block_height':1,'pos':0,'merkle':[]}
   raise AssertionError('unexpected wallet method '+method)
  return h.Peer(self.path,[bytes.fromhex(credit['header'])],wallet=wallet)
 def prepare(self):
  review=self.wallet_call('wallet-prepare',extra={'requestId':self.operation,'payment':self.payment})['wallet']
  return {'requestId':self.operation,'payment':self.payment,'retainedReview':{'signingCommitment':review['signingCommitment'],'changeAddress':review['changeAddress']},
   'signingApproval':{'approvedCommitment':review['signingCommitment'],'password':FIXTURE['vault']['password']}}
 def records(self):return {p.name:p.read_bytes() for p in self.spends.iterdir()}
 def send_request(self,request,signed):
  value=dict(request);del value['signingApproval']
  value['broadcastApproval']={'approvedCommitment':signed['signingCommitment'],'approvedTxid':signed['txid'],**self.value['endpoint']}
  return value
 def status_request(self,request):
  value=dict(request);value.pop('broadcastApproval',None);value.pop('signingApproval',None);return value
 def test_broadcast_explicit_approval_sends_retained_bytes_over_tls_and_recovers_offline_status(self):
  sent=[]
  def broadcast(params):
   self.assertEqual(len(params),1);raw=bytes.fromhex(params[0]);sent.append(raw)
   self.assertEqual(raw,(self.spends/f'ltc-signed-{signed["signingCommitment"]}.tx').read_bytes())
   self.assertEqual(verify_signed(raw,json.loads((self.spends/f'ltc-spend-{self.operation}.json').read_bytes()),self.address),signed['txid'])
   self.assertEqual(len(list(self.spends.glob('ltc-submission-*'))),1)
   self.assertEqual(len(list(self.spends.glob('ltc-broadcast-*-intent.json'))),1)
   return signed['txid']
  with self.peer(broadcast) as peer:
   self.endpoint(peer);self.call(sync=True);request=self.prepare();signed=self.wallet_call('wallet-sign',extra=request)['wallet']
   send=self.send_request(request,signed);status=self.status_request(send)
   before=len(peer.requests);self.assertEqual(self.wallet_call('wallet-broadcast-status',extra=status)['wallet']['broadcastStatus'],'no-local-attempt');self.assertEqual(len(peer.requests),before)
   self.value['wallet'].pop('vault');self.save_config()
   result=self.wallet_call('wallet-broadcast',extra=send)['wallet']
   self.assertEqual(result['broadcastStatus'],'server-acknowledged');self.assertEqual(result['confirmationStatus'],'unassessed')
   self.assertEqual(result['broadcastEndpoint'],self.value['endpoint']);self.assertEqual(result['txid'],signed['txid'])
   self.assertNotIn('broadcast',result);self.assertEqual(len(sent),1)
   records=self.records();self.assertEqual(self.wallet_call('wallet-broadcast',extra=send)['wallet'],result);self.assertEqual(self.records(),records)
   self.assertEqual(len(sent),2);self.assertEqual(sent[0],sent[1])
  result=self.wallet_call('wallet-broadcast-status',extra=status)['wallet']
  self.assertEqual(result['broadcastStatus'],'server-acknowledged');self.assertTrue(result['recovered']);self.assertEqual(result['confirmationStatus'],'unassessed')
  self.assertEqual(self.records(),records)
  self.value['endpoint']['port']=1;self.save_config()
  self.assertEqual(self.wallet_call('wallet-broadcast-status',extra=status)['wallet']['broadcastStatus'],'no-local-attempt')
 def test_broadcast_rejects_unsigned_mismatched_approval_and_observed_spends_before_intent(self):
  with self.peer() as peer:
   self.endpoint(peer);self.call(sync=True);request=self.prepare();unsigned=self.wallet_call('wallet-reopen',extra=self.status_request(request))['wallet']
   send=self.send_request(request,unsigned);records=self.records();before=len(peer.requests)
   self.wallet_call('wallet-broadcast',success=False,extra=send);self.assertEqual(self.records(),records);self.assertEqual(len(peer.requests),before)
   signed=self.wallet_call('wallet-sign',extra=request)['wallet'];send=self.send_request(request,signed);records=self.records();before=len(peer.requests)
   self.wallet_call('wallet-broadcast',success=False,extra=self.status_request(send))
   for approval in [dict(send['broadcastApproval'],approvedCommitment='0'*64),dict(send['broadcastApproval'],approvedTxid='0'*64),
                    dict(send['broadcastApproval'],host='other.invalid'),dict(send['broadcastApproval'],port=1),dict(send['broadcastApproval'],extra=True)]:
    self.wallet_call('wallet-broadcast',success=False,extra=dict(send,broadcastApproval=approval));self.assertEqual(self.records(),records)
   self.wallet_call('wallet-broadcast',success=False,extra=dict(send,signingApproval=request['signingApproval']))
   self.wallet_call('wallet-broadcast-status',success=False,extra=send);self.wallet_call('wallet-signed',success=False,extra=send)
   self.assertEqual(len(peer.requests),before)
   self.pending=True;self.wallet_call('wallet-broadcast',success=False,extra=send);self.assertEqual(self.records(),records)
   self.assertFalse(list(self.spends.glob('ltc-submission-*')));self.assertFalse(list(self.spends.glob('ltc-broadcast-*')))
 def test_lost_broadcast_ack_remains_unknown_across_helper_restart_and_blocks_resigning(self):
  sent=[]
  def broadcast(params):sent.append(params[0]);return h.DROP_RESPONSE
  with self.peer(broadcast) as peer:
   self.endpoint(peer);self.call(sync=True);request=self.prepare();signed=self.wallet_call('wallet-sign',extra=request)['wallet'];send=self.send_request(request,signed)
   self.wallet_call('wallet-broadcast',success=False,extra=send);self.assertEqual(len(sent),1)
   records=self.records();self.assertEqual(len(list(self.spends.glob('ltc-broadcast-*-ack.json'))),0)
   self.wallet_call('wallet-sign',success=False,extra=request);self.assertEqual(self.records(),records);self.assertEqual(len(sent),1)
  self.value['wallet'].pop('vault');self.save_config()
  result=self.wallet_call('wallet-broadcast-status',extra=self.status_request(send))['wallet']
  self.assertEqual(result['broadcastStatus'],'outcome-unknown');self.assertEqual(result['confirmationStatus'],'unassessed');self.assertEqual(self.records(),records)
  self.assertEqual(bytes.fromhex(sent[0]),(self.spends/f'ltc-signed-{signed["signingCommitment"]}.tx').read_bytes())
 def test_wrong_ack_and_corrupt_submission_evidence_never_report_acceptance(self):
  with self.peer(lambda params:'0'*64) as peer:
   self.endpoint(peer);self.call(sync=True);request=self.prepare();signed=self.wallet_call('wallet-sign',extra=request)['wallet'];send=self.send_request(request,signed)
   self.wallet_call('wallet-broadcast',success=False,extra=send)
   status=self.status_request(send);self.assertEqual(self.wallet_call('wallet-broadcast-status',extra=status)['wallet']['broadcastStatus'],'outcome-unknown')
   path=next(self.spends.glob('ltc-submission-*'));saved=path.read_bytes();path.write_text('{}')
   self.wallet_call('wallet-broadcast-status',success=False,extra=status)
   before=sum(r['method']=='blockchain.transaction.broadcast' for r in peer.requests)
   self.wallet_call('wallet-broadcast',success=False,extra=send)
   self.assertEqual(sum(r['method']=='blockchain.transaction.broadcast' for r in peer.requests),before)
   path.write_bytes(saved);self.assertEqual(self.wallet_call('wallet-broadcast-status',extra=status)['wallet']['broadcastStatus'],'outcome-unknown')
 def test_reconciliation_reports_absence_pending_and_rejects_changed_signed_bytes(self):
  credit=FIXTURE['blocks'][0];script_hash=hashlib.sha256(bytes.fromhex(self.address['script'])).digest()[::-1].hex()
  phase=['credit'];raw=[None];signed=[None]
  def wallet(method,params):
   if method=='blockchain.scripthash.get_history':
    if params[0]!=script_hash:return []
    if phase[0]=='credit':return [{'tx_hash':credit['txid'],'height':1}]
    if phase[0]=='none':return []
    return [{'tx_hash':signed[0]['txid'],'height':2 if phase[0]=='ahead' else 0}]
   if method=='blockchain.transaction.get':
    if params[0]==credit['txid']:return credit['transaction']
    self.assertEqual(params,[signed[0]['txid'],False])
    value=bytearray(raw[0])
    if phase[0]=='changed':value[-5]^=1
    return value.hex()
   if method=='blockchain.transaction.get_merkle':return {'block_height':1,'pos':0,'merkle':[]}
   raise AssertionError(method)
  with h.Peer(self.path,[bytes.fromhex(credit['header'])],wallet=wallet) as peer:
   self.endpoint(peer);self.call(sync=True);request=self.prepare();signed[0]=self.wallet_call('wallet-sign',extra=request)['wallet']
   raw[0]=(self.spends/f'ltc-signed-{signed[0]["signingCommitment"]}.tx').read_bytes()
   self.value['wallet'].pop('vault');self.save_config();records=self.records()
   request=dict(self.status_request(request),maximumDownloadHeaders=10,maximumHistoryEntries=10)
   for value,expected in [('none','not-reported'),('pending','reported-pending')]:
    phase[0]=value;result=self.wallet_call('wallet-reconcile',extra=request)
    self.assertEqual(result['wallet']['confirmationStatus'],expected);self.assertEqual(result['wallet']['broadcastStatus'],'no-local-attempt')
    observation=result['wallet']['paymentObservation'];self.assertEqual(observation['selectedHeight'],result['chain']['height'])
    self.assertEqual(observation['selectedTip'],result['chain']['tip']);self.assertEqual(observation['confirmations'],0)
    self.assertIsNone(observation['height']);self.assertIsNone(observation['blockHash']);self.assertGreater(observation['observedAtUnix'],0)
    self.assertEqual(self.records(),records)
   for phase[0] in ['changed','ahead']:
    self.wallet_call('wallet-reconcile',success=False,extra=request);self.assertEqual(self.records(),records)
 def test_reconciliation_requires_explicit_budgets_and_rejects_approval_and_scan(self):
  with self.peer() as peer:
   self.endpoint(peer);self.call(sync=True);sign=self.prepare();self.wallet_call('wallet-sign',extra=sign)
   request=dict(self.status_request(sign),maximumDownloadHeaders=10,maximumHistoryEntries=10)
   records=self.records();before=len(peer.requests)
   for key in ['maximumDownloadHeaders','maximumHistoryEntries']:
    missing=dict(request);del missing[key];self.wallet_call('wallet-reconcile',success=False,extra=missing)
   for field,value in [('maximumDownloadHeaders',0),('maximumDownloadHeaders',10000001),('maximumHistoryEntries',0),
                       ('maximumHistoryEntries',1001),('signingApproval',sign['signingApproval']),
                       ('walletScan',{'gap':1,'addressesPerBranch':3,'historyEntriesPerBranch':10}),
                       ('broadcastApproval',{'approvedCommitment':'0'*64,'approvedTxid':'0'*64,'host':'localhost','port':peer.port})]:
    self.wallet_call('wallet-reconcile',success=False,extra=dict(request,**{field:value}))
   self.wallet_call('wallet-signed',success=False,extra=request)
   self.wallet_call('wallet-broadcast-status',success=False,extra=request)
   self.assertEqual(len(peer.requests),before);self.assertEqual(self.records(),records)
 def test_signing_retains_independently_verified_bytes_and_retries_without_broadcast(self):
  with self.peer() as peer:
   self.endpoint(peer);self.call(sync=True);request=self.prepare();before_book=(self.book/'litecoin-address-book.json').read_bytes()
   result=self.wallet_call('wallet-sign',extra=request)['wallet'];self.assertTrue(result['signed']);self.assertFalse(result['broadcast'])
   self.assertFalse(result['watchOnly']);self.assertEqual(result['paymentAtomic'],'40000');self.assertEqual(result['changeAtomic'],'59000')
   self.assertNotIn(FIXTURE['vault']['password'],json.dumps(result));self.assertNotIn(FIXTURE['seed'],json.dumps(result))
   commitment=result['signingCommitment'];raw=(self.spends/f'ltc-signed-{commitment}.tx').read_bytes()
   manifest=json.loads((self.spends/f'ltc-spend-{self.operation}.json').read_bytes())
   self.assertEqual(verify_signed(raw,manifest,self.address),result['txid'])
   records=self.records();self.assertEqual(self.wallet_call('wallet-sign',extra=request)['wallet'],result);self.assertEqual(self.records(),records)
   self.assertEqual((self.book/'litecoin-address-book.json').read_bytes(),before_book)
   self.pending=True;self.wallet_call('wallet-sign',success=False,extra=request);self.assertEqual(self.records(),records)
 def test_signed_recovery_is_offline_and_checks_retained_signatures_without_unlocking(self):
  with self.peer() as peer:
   self.endpoint(peer);self.call(sync=True);request=self.prepare()
   recovery=dict(request);del recovery['signingApproval']
   self.wallet_call('wallet-signed',success=False,extra=recovery)
   signed=self.wallet_call('wallet-sign',extra=request)['wallet']
  self.value.pop('endpoint');self.value['wallet'].pop('vault');self.save_config()
  records=self.records();book=(self.book/'litecoin-address-book.json').read_bytes()
  result=self.wallet_call('wallet-signed',extra=recovery)['wallet']
  expected=dict(signed,watchOnly=True,recovered=True,broadcastStatus='unassessed');del expected['broadcast']
  self.assertEqual(result,expected);self.assertEqual(self.records(),records);self.assertEqual((self.book/'litecoin-address-book.json').read_bytes(),book)
  self.wallet_call('wallet-signed',success=False,extra=request)
  self.wallet_call('wallet-signed',success=False,extra=dict(recovery,walletScan={'gap':1,'addressesPerBranch':3,'historyEntriesPerBranch':10}))
  self.wallet_call('wallet-signed',success=False,extra=dict(recovery,retainedReview=dict(recovery['retainedReview'],signingCommitment='0'*64)))
  path=self.spends/('ltc-signed-'+signed['signingCommitment']+'.tx');original=path.read_bytes();path.write_bytes(original[:-1]+bytes([original[-1]^1]))
  self.wallet_call('wallet-signed',success=False,extra=recovery);path.write_bytes(original)
  intent=self.spends/('ltc-signing-'+signed['signingCommitment']+'.json');original=intent.read_bytes();intent.write_text('{}')
  self.wallet_call('wallet-signed',success=False,extra=recovery);intent.write_bytes(original)
  claim=next(self.spends.glob('ltc-input-*.claim'));claim.unlink();self.wallet_call('wallet-signed',success=False,extra=recovery);self.assertFalse(claim.exists())
 def test_signing_requires_explicit_matching_approval_password_account_and_claims(self):
  with self.peer() as peer:
   self.endpoint(peer);self.call(sync=True);request=self.prepare();records=self.records()
   missing=dict(request);del missing['signingApproval'];self.wallet_call('wallet-sign',success=False,extra=missing)
   for approval in [dict(request['signingApproval'],approvedCommitment='0'*64),dict(request['signingApproval'],password='WRONG PUBLIC PASSWORD'),
                    dict(request['signingApproval'],password=''),dict(request['signingApproval'],password='x'*1025),dict(request['signingApproval'],extra=True)]:
    self.wallet_call('wallet-sign',success=False,extra=dict(request,signingApproval=approval));self.assertEqual(self.records(),records)
   self.wallet_call('wallet-reopen',success=False,extra=request);self.wallet_call('wallet-prepare',success=False,extra=request)
   self.vault_file.write_bytes(bytes.fromhex(FIXTURE['wrongVault']));self.wallet_call('wallet-sign',success=False,extra=request);self.assertEqual(self.records(),records)
   self.vault_file.write_bytes(bytes.fromhex(FIXTURE['vault']['vault']))
   metadata=self.value['wallet'].pop('vault');self.save_config();self.wallet_call('wallet-sign',success=False,extra=request)
   self.value['wallet']['vault']=metadata;self.save_config();self.assertEqual(self.records(),records)
   self.pending=True;self.wallet_call('wallet-sign',success=False,extra=request);self.pending=False;self.assertEqual(self.records(),records)
   claim=next(self.spends.glob('ltc-input-*.claim'));claim.unlink();self.wallet_call('wallet-sign',success=False,extra=request)
   self.assertFalse(claim.exists());self.assertFalse(list(self.spends.glob('ltc-signing-*')));self.assertFalse(list(self.spends.glob('*.tx')))
if __name__=='__main__':unittest.main(verbosity=2)
