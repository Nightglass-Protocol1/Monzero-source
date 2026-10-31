#!/usr/bin/env python3
"""Disposable generated wallets only: approved setup, backup and configuration publication."""
import hashlib,json,unittest,uuid
from pathlib import Path
import test_litecoin_wallet_helper as w
PASSWORD='PUBLIC DISPOSABLE SETUP PASSWORD'

class CreateHelperTests(unittest.TestCase):
 tearDown=w.WalletHelperTests.tearDown
 save_config=w.WalletHelperTests.save_config
 wallet_call=w.WalletHelperTests.wallet_call
 def setUp(self):
  w.WalletHelperTests.setUp(self)
  self.directories={}
  for key,name in [('setupDirectory','setup'),('vaultDirectory','vault'),('addressBookDirectory','book'),
   ('backupDirectory','backup'),('spendDirectory','spends')]:
   p=self.path/name;p.mkdir(mode=0o700);self.directories[key]=str(p)
  self.value=dict(self.directories,schema='monzero-private-ltc-setup-v1',network='regtest',
   cacheDirectory=str(self.cache),walletId=str(uuid.uuid4()),accountIndex=0,maximumHeight=1000)
  self.save_config()
 def create(self,success=True,digest=None,password=PASSWORD,**extra):
  approved=digest if digest is not None else hashlib.sha256(self.config.read_bytes()).hexdigest()
  return self.wallet_call('wallet-create',success=success,extra=dict(createApproval={'approvedConfigDigest':approved,'password':password},**extra))
 def records(self):
  return {str(p.relative_to(self.path)):p.read_bytes() for directory in self.directories.values() for p in Path(directory).iterdir() if p.is_file()}
 def test_created_configuration_and_backup_work_without_network_and_retry_preserves_keys(self):
  self.value['endpoint']={'host':'localhost','port':1};self.save_config()
  result=self.create();wallet=result['wallet'];self.assertEqual(set(result),{'schema','requestId','executable','wallet'})
  self.assertEqual(len(wallet),13);self.assertTrue(wallet['created']);self.assertTrue(wallet['backupVerified'])
  self.assertEqual(wallet['walletId'],self.value['walletId']);self.assertFalse(wallet['activeSwapRecovery'])
  self.assertEqual(wallet['allocationFreshness'],'unassessed');self.assertEqual(wallet['receivingAllocated'],0)
  configuration=Path(wallet['configurationFile']);raw=configuration.read_bytes()
  self.assertEqual(configuration,self.path/'setup/litecoin-wallet.json');self.assertEqual(configuration.stat().st_mode&0o777,0o600)
  self.assertEqual(hashlib.sha256(raw).hexdigest(),wallet['configurationDigest'])
  settings=json.loads(raw);self.assertEqual(settings['endpoint'],self.value['endpoint'])
  self.assertEqual(settings['wallet']['vault']['walletId'],self.value['walletId'])
  before=self.records();self.assertEqual(self.create()['wallet'],wallet);self.assertEqual(self.records(),before)
  bootstrap=self.config;self.config=configuration
  verified=self.wallet_call('wallet-backup-verify',extra={'backupApproval':{'directory':self.directories['backupDirectory'],'password':PASSWORD}})['wallet']
  self.assertEqual(verified['backupDigest'],wallet['backupDigest'])
  address=self.wallet_call('wallet-receive')['wallet'];self.assertTrue(address['address'].startswith('rltc1'))
  self.assertEqual(address['receivingAllocated'],1)
  self.config=bootstrap;before=self.records();self.create(success=False);self.assertEqual(self.records(),before)
  for record in before.values():self.assertNotIn(PASSWORD.encode(),record)
  self.assertNotIn('accountXpub',json.dumps(result))
 def test_exact_retry_completes_missing_configuration_but_preserves_conflicting_files_and_settings(self):
  receipt=self.create()['wallet'];configuration=Path(receipt['configurationFile']);raw=configuration.read_bytes()
  before=self.records();configuration.unlink()
  self.assertEqual(self.create()['wallet'],receipt);self.assertEqual(self.records(),before)
  configuration.write_bytes(b'conflicting fixture');self.create(success=False)
  self.assertEqual(configuration.read_bytes(),b'conflicting fixture');configuration.write_bytes(raw)
  before=self.records();self.create(success=False,password='WRONG PUBLIC PASSWORD');self.assertEqual(self.records(),before)
  self.value['maximumHeight']=999;self.save_config();self.create(success=False);self.assertEqual(self.records(),before)
 def test_approval_and_invalid_settings_fail_before_key_creation(self):
  self.create(success=False,digest='0'*64);self.assertEqual(self.records(),{})
  self.create(success=False,password='');self.assertEqual(self.records(),{})
  self.wallet_call('wallet-create',success=False);self.assertEqual(self.records(),{})
  for key,value in [('walletScan',{'gap':1,'addressesPerBranch':3,'historyEntriesPerBranch':10}),('maximumDownloadHeaders',1),
   ('backupApproval',{'directory':self.directories['backupDirectory'],'password':PASSWORD})]:
   with self.subTest(field=key):self.create(success=False,**{key:value});self.assertEqual(self.records(),{})
  original=dict(self.value)
  for key,value in [('maximumHeight',10000001),('accountIndex',2147483648),('walletId',str(uuid.UUID(int=0))),
   ('endpoint',{'host':'https://invalid','port':1}),('spendDirectory',self.directories['vaultDirectory'])]:
   with self.subTest(setting=key):
    self.value=dict(original,**{key:value});self.save_config();self.create(success=False);self.assertEqual(self.records(),{})
  self.value=original;self.save_config()
  self.wallet_call('wallet-init',success=False,extra={'createApproval':{'approvedConfigDigest':'0'*64,'password':PASSWORD}})
  self.assertEqual(self.records(),{})

if __name__=='__main__':unittest.main(verbosity=2)
