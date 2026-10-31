#!/usr/bin/env python3
"""Qt creation review, explicit opening and restart with disposable regtest keys."""
import hashlib,json,os,subprocess,unittest,uuid
from pathlib import Path
import test_litecoin_light_qt as q
PASSWORD='PUBLIC DISPOSABLE QT SETUP PASSWORD'
ESCAPED='PUBLIC " \\ \n\x00 Ω SETUP PASSWORD'
class CreateQtTests(unittest.TestCase):
 tearDown=q.QtTests.tearDown
 save=q.QtTests.save
 def setUp(self):
  q.QtTests.setUp(self)
  self.directories={}
  for key,name in [('setupDirectory','setup'),('vaultDirectory','vault'),('addressBookDirectory','book'),
   ('backupDirectory','backup'),('spendDirectory','spends')]:
   p=self.path/name;p.mkdir(mode=0o700);self.directories[key]=str(p)
  self.f.value=dict(self.directories,schema='monzero-private-ltc-setup-v1',network='regtest',
   cacheDirectory=str(self.f.cache),walletId=str(uuid.uuid4()),accountIndex=0,maximumHeight=1000)
  self.save()
 def call(self,mode='wallet-create',password=PASSWORD,code=0,**packet):
  env=os.environ.copy();env['XDG_CONFIG_HOME']=str(self.path/'settings');env['SSL_CERT_FILE']=str(q.FIXTURE/'root.pem')
  result=subprocess.run([str(q.PROBE),str(self.wrapper),mode,'3',json.dumps(packet)],input=password.encode(),
   capture_output=True,env=env,timeout=20)
  if password:self.assertNotIn(password.encode(),result.stdout+result.stderr)
  self.assertEqual(result.returncode,code,result.stderr.decode())
  if code:self.assertEqual(result.stdout,b'');return
  result=json.loads(result.stdout);self.assertFalse(result['executable']);return result
 def records(self):
  return {str(p.relative_to(self.path)):p.read_bytes() for d in self.directories.values() for p in Path(d).iterdir() if p.is_file()}
 def test_creation_does_not_open_automatically_and_restart_reuses_receiving_address(self):
  created=self.call();self.assertEqual(created['state'],'wallet-created');self.assertTrue(created['setupConfigured'])
  self.assertFalse(created['walletConfigured']);self.assertFalse(created['syncAvailable']);self.assertFalse(created['refreshAvailable'])
  self.assertTrue(created['walletCreatedOpenAvailable']);self.assertTrue(created['wallet']['backupVerified'])
  before=self.records()
  self.call('wallet-created-open-direct',code=5,approvedDigest=created['wallet']['configurationDigest'])
  self.call('wallet-created-open',code=5,approvedDigest='0'*64);self.assertEqual(self.records(),before)
  review=self.call('wallet-created-review',password='');self.assertEqual(review['state'],'wallet-created-review')
  self.assertFalse(review['wallet']['backupVerified']);self.assertFalse(review['wallet']['created']);self.assertEqual(self.records(),before)
  opened=self.call('wallet-created-receive',password='');self.assertEqual(opened['state'],'wallet-address')
  self.assertFalse(opened['setupConfigured']);self.assertTrue(opened['walletConfigured']);self.assertFalse(opened['walletSignAvailable'])
  self.assertEqual(opened['wallet']['receivingAllocated'],1);self.assertTrue(opened['wallet']['address'].startswith('rltc1'))
  allocated=self.records();again=self.call('wallet-created-receive',password='')
  self.assertEqual(again['wallet'],opened['wallet']);self.assertEqual(self.records(),allocated)
  failed=self.call();self.assertEqual(failed['state'],'unknown');self.assertEqual(self.records(),allocated)
 def test_preflight_requires_confirmed_password_exact_settings_and_creation_mode(self):
  for password in ['', 'x'*1025]:self.call(password=password,code=5)
  self.call(code=5,confirmationMatches=False);self.call(code=5,approvedConfigDigest='0'*64)
  self.call('status',code=5);self.call('wallet-init',code=5);self.call('wallet-created-review',password='',code=5)
  self.assertEqual(self.records(),{})
  self.f.value['spendDirectory']=self.f.value['vaultDirectory'];self.save();self.call(code=3)
 def test_saved_review_refuses_changed_configuration_and_completion_context(self):
  receipt=self.call()['wallet'];configuration=Path(receipt['configurationFile']);ready=self.path/'setup/litecoin-wallet-ready.json'
  raw=configuration.read_bytes();original=ready.read_bytes()
  value=json.loads(raw);value['cacheDirectory']=str(self.path);q.write(configuration,value)
  self.call('wallet-created-review',password='',code=5);self.assertEqual(configuration.read_bytes(),json.dumps(value,sort_keys=True,separators=(',',':')).encode())
  configuration.write_bytes(raw)
  value=json.loads(original);value['wallet']=str(uuid.uuid4());q.write(ready,value)
  self.call('wallet-created-review',password='',code=5);ready.write_bytes(original)
  self.f.value['maximumHeight']=999;self.save();self.call('wallet-created-review',password='',code=5)
 def fake(self,case,receipt):
  self.program=self.path/'create-helper.py'
  code="""#!/usr/bin/python3
import hashlib,json,sys,time
request=json.load(sys.stdin);case=CASE
assert set(request)=={'schema','requestId','createApproval'}
assert request['createApproval']=={'approvedConfigDigest':hashlib.sha256(open(sys.argv[2],'rb').read()).hexdigest(),'password':PASSWORD}
assert PASSWORD not in str(sys.argv)
w=RECEIPT
if case=='digest':w['configurationDigest']='0'*64
if case=='path':w['configurationFile']+='.other'
if case=='identity':w['walletId']='22222222-2222-4333-8444-555555555555'
if case=='backup':w['backupDigest']='0'*64
if case=='counts':w['receivingAllocated']=1
if case=='scope':w['scope']='full-wallet'
if case=='verified':w['backupVerified']=False
if case=='extra':w['secret']='PUBLIC FIXTURE'
if case=='cancel':time.sleep(2)
r=dict(schema='monzero-ltc-light-wallet-create-response-v1',requestId=request['requestId'],executable=False,wallet=w)
print(json.dumps(r,sort_keys=True,separators=(',',':')),end='')
""".replace('CASE',repr(case)).replace('PASSWORD',repr(ESCAPED)).replace('RECEIPT',repr(receipt))
  self.program.write_text(code);self.program.chmod(0o700);self.save()
 def test_creation_receipt_matches_saved_config_and_completion_and_cancel_clears(self):
  receipt=self.call()['wallet'];before=self.records()
  for case in ['normal','digest','path','identity','backup','counts','scope','verified','extra','cancel']:
   with self.subTest(case=case):
    self.fake(case,receipt);result=self.call('wallet-create-cancel' if case=='cancel' else 'wallet-create',password=ESCAPED)
    self.assertEqual(result['state'],'wallet-created' if case=='normal' else 'unknown')
    self.assertFalse(result['walletConfigured']);self.assertFalse(result['walletSignAvailable'])
    if case!='normal':self.assertEqual(result['wallet'],{});self.assertFalse(result['walletCreatedOpenAvailable'])
    self.assertEqual(self.records(),before)

if __name__=='__main__':unittest.main(verbosity=2)
