#!/usr/bin/env python3
"""Offline backup helper boundaries, using only PUBLIC encrypted seed fixtures."""
import hashlib,json,unittest
from pathlib import Path
import test_litecoin_signing_helper as signing

class BackupHelperTests(unittest.TestCase):
 setUp=signing.SigningHelperTests.setUp
 tearDown=signing.SigningHelperTests.tearDown
 save_config=signing.SigningHelperTests.save_config
 wallet_call=signing.SigningHelperTests.wallet_call
 def destination(self,name='backup'):
  directory=self.path/name;directory.mkdir(mode=0o700);return directory
 def backup(self,directory,save=True,password=None,success=True,**extra):
  request={'backupApproval':{'directory':str(directory),'password':password if password is not None else signing.FIXTURE['vault']['password']}}
  request.update(extra)
  return self.wallet_call('wallet-backup-save' if save else 'wallet-backup-verify',success=success,extra=request)
 def records(self):
  return {str(p.relative_to(self.path)):p.read_bytes() for d in [self.book,self.vault,self.spends] for p in d.iterdir() if p.is_file()}
 def test_save_readback_and_verify_without_live_wallet_or_chain(self):
  self.value.pop('endpoint',None);self.save_config()
  self.wallet_call('wallet-receive');before=self.records();directory=self.destination()
  saved=self.backup(directory);receipt=saved['wallet'];bundle=directory/'litecoin-wallet.backup'
  self.assertEqual(set(saved),{'schema','requestId','executable','wallet'})
  self.assertEqual(receipt,dict(watchOnly=True,spendable=False,backupVerified=True,backupSaved=True,
   backupDigest=hashlib.sha256(bundle.read_bytes()).hexdigest(),walletId=signing.FIXTURE['vault']['wallet'],
   receivingAllocated=1,changeAllocated=0,scope='seed-and-address-allocations',
   allocationFreshness='unassessed',activeSwapRecovery=False))
  self.assertEqual(bundle.stat().st_mode&0o777,0o600);self.assertEqual(self.records(),before)
  for secret in [signing.FIXTURE['vault']['password'],'accountXpub','privateKey']:
   self.assertNotIn(secret,json.dumps(saved))
  original=bundle.read_bytes()
  from nacl.bindings import crypto_aead_xchacha20poly1305_ietf_decrypt
  key=hashlib.scrypt(signing.FIXTURE['vault']['password'].encode(),salt=original[88:104],n=2**17,r=8,p=1,dklen=32,maxmem=256*1024*1024)
  payload=crypto_aead_xchacha20poly1305_ietf_decrypt(original[128:],original[:128],original[104:128],key)
  self.assertEqual(original[:8],b'MZLTCB01')
  self.assertEqual(payload[:176],self.vault_file.read_bytes())
  self.assertEqual(int.from_bytes(payload[176:180],'big'),0)
  self.assertEqual(int.from_bytes(payload[180:184],'big'),len(payload[184:]))
  self.assertEqual(payload[184:],(self.book/'litecoin-address-book.json').read_bytes())
  self.backup(directory,success=False) # A new random backup never overwrites an older generation.
  self.assertEqual(bundle.read_bytes(),original)
  self.wallet_call('wallet-receive') # A valid older backup must not claim fresh allocation counts.
  self.vault_file.unlink()
  for p in self.book.iterdir():p.unlink()
  self.value['cacheDirectory']=str(self.path/'missing-cache');self.save_config()
  verified=self.backup(directory,save=False)['wallet']
  self.assertEqual(verified,dict(receipt,backupSaved=False));self.assertEqual(bundle.read_bytes(),original)
  self.assertFalse((self.path/'missing-cache').exists())
 def test_authentication_context_and_corruption_fail_without_overwrite(self):
  directory=self.destination();self.backup(directory);bundle=directory/'litecoin-wallet.backup';original=bundle.read_bytes()
  self.backup(directory,save=False,password='WRONG PUBLIC PASSWORD',success=False)
  self.value['wallet']['vault']['walletId']='22222222-2222-4333-8444-555555555555';self.save_config()
  self.backup(directory,save=False,success=False)
  self.value['wallet']['vault']['walletId']=signing.FIXTURE['vault']['wallet']
  self.value['wallet']['account']=signing.w.descriptor();self.save_config()
  self.backup(directory,save=False,success=False)
  self.value['wallet']['account']=signing.FIXTURE['account'];self.save_config()
  bundle.write_bytes(original[:-1]+bytes([original[-1]^1]))
  self.backup(directory,save=False,success=False)
  self.assertEqual(bundle.read_bytes(),original[:-1]+bytes([original[-1]^1]))
 def test_explicit_approval_and_private_destination_are_required(self):
  directory=self.destination();before=self.records()
  self.wallet_call('wallet-backup-save',success=False)
  for field,value in [('maximumDownloadHeaders',1),('maximumHistoryEntries',1),
   ('walletScan',{'gap':1,'addressesPerBranch':3,'historyEntriesPerBranch':10}),
   ('signingApproval',{'approvedCommitment':'1'*64,'password':'PUBLIC PASSWORD'}),
   ('payment',{'address':'invalid','amountAtomic':'1','feeAtomic':'1','minimumChangeAtomic':'1','minimumConfirmations':1})]:
   with self.subTest(field=field):self.backup(directory,success=False,**{field:value})
  for password in ['', 'x'*1025]:
   self.backup(directory,password=password,success=False)
  self.wallet_call('wallet-init',success=False,extra={'backupApproval':{'directory':str(directory),'password':'PUBLIC PASSWORD'}})
  self.backup(Path('relative-backup'),success=False)
  link=self.path/'linked-backup';link.symlink_to(directory,target_is_directory=True);self.backup(link,success=False)
  directory.chmod(0o755);self.backup(directory,success=False)
  self.assertFalse((directory/'litecoin-wallet.backup').exists());self.assertEqual(self.records(),before)

if __name__=='__main__':unittest.main(verbosity=2)
