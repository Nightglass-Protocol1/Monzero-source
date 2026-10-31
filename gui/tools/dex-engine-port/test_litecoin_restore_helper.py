#!/usr/bin/env python3
"""Restore PUBLIC encrypted fixtures with exact approval and conflict preservation."""
import json,unittest,uuid
import test_litecoin_backup_helper as backup
s=backup.signing

class RestoreHelperTests(unittest.TestCase):
 setUp=backup.BackupHelperTests.setUp
 tearDown=backup.BackupHelperTests.tearDown
 save_config=backup.BackupHelperTests.save_config
 wallet_call=backup.BackupHelperTests.wallet_call
 destination=backup.BackupHelperTests.destination
 backup=backup.BackupHelperTests.backup
 def prepare_restore(self):
  self.value.pop('endpoint',None);self.save_config()
  self.operation=str(uuid.uuid4())
  self.receiving=self.wallet_call('wallet-receive',extra={'requestId':self.operation})['wallet']
  self.source=self.destination();self.info=self.backup(self.source)['wallet']
  self.original_vault=self.vault_file.read_bytes()
  self.original_book=(self.book/'litecoin-address-book.json').read_bytes()
  self.target_vault=self.destination('restored-vault');self.target_book=self.destination('restored-book')
  self.value['wallet']['vault']['directory']=str(self.target_vault)
  self.value['wallet']['addressBookDirectory']=str(self.target_book);self.save_config()
 def restore(self,success=True,digest=None,password=None,**extra):
  approval={'directory':str(self.source),'password':s.FIXTURE['vault']['password'] if password is None else password,
   'approvedDigest':self.info['backupDigest'] if digest is None else digest}
  return self.wallet_call('wallet-restore',success=success,extra=dict(restoreApproval=approval,**extra))
 def assert_empty(self):
  self.assertEqual(list(self.target_vault.iterdir()),[]);self.assertEqual(list(self.target_book.iterdir()),[])
 def test_restore_exact_records_retry_and_continued_address_allocation(self):
  self.prepare_restore()
  self.value['cacheDirectory']=str(self.path/'missing-cache');self.save_config()
  result=self.restore();wallet=result['wallet']
  self.assertEqual(set(result),{'schema','requestId','executable','wallet'})
  self.assertEqual(wallet,dict(watchOnly=True,spendable=False,restored=True,backupDigest=self.info['backupDigest'],
   walletId=self.info['walletId'],receivingAllocated=1,changeAllocated=0,
   scope='seed-and-address-allocations',allocationFreshness='unassessed',activeSwapRecovery=False))
  self.assertFalse((self.path/'missing-cache').exists())
  self.assertEqual((self.target_vault/'litecoin-seed.vault').read_bytes(),self.original_vault)
  self.assertEqual((self.target_book/'litecoin-address-book.json').read_bytes(),self.original_book)
  self.assertEqual(self.restore()['wallet'],wallet)
  self.value['cacheDirectory']=str(self.path/'cache');self.save_config()
  retry=self.wallet_call('wallet-receive',extra={'requestId':self.operation})['wallet']
  self.assertEqual(retry,self.receiving)
  next_address=self.wallet_call('wallet-receive')['wallet']
  self.assertEqual(next_address['receivingAllocated'],2);self.assertNotEqual(next_address['address'],retry['address'])
  newer=(self.target_book/'litecoin-address-book.json').read_bytes()
  self.restore(success=False)
  self.assertEqual((self.target_book/'litecoin-address-book.json').read_bytes(),newer)
 def test_partial_vault_publication_is_completed_but_conflicting_book_is_not_replaced(self):
  self.prepare_restore();vault=self.target_vault/'litecoin-seed.vault'
  vault.write_bytes(self.original_vault);vault.chmod(0o600)
  self.assertTrue(self.restore()['wallet']['restored'])
  vault.unlink();record=self.target_book/'litecoin-address-book.json';record.write_bytes(b'conflicting fixture')
  self.restore(success=False);self.assertFalse(vault.exists());self.assertEqual(record.read_bytes(),b'conflicting fixture')
 def test_approval_password_account_and_forbidden_fields_precede_publication(self):
  self.prepare_restore()
  for digest in ['0'*64,self.info['backupDigest'].upper(),'invalid']:
   with self.subTest(digest=digest):self.restore(success=False,digest=digest);self.assert_empty()
  self.restore(success=False,password='WRONG PUBLIC PASSWORD');self.assert_empty()
  self.value['wallet']['account']=s.w.descriptor();self.save_config()
  self.restore(success=False);self.assert_empty()
  self.value['wallet']['account']=s.FIXTURE['account'];self.save_config()
  self.wallet_call('wallet-restore',success=False);self.assert_empty()
  for key,value in [('maximumDownloadHeaders',1),('maximumHistoryEntries',1),
   ('backupApproval',{'directory':str(self.source),'password':'PUBLIC PASSWORD'}),
   ('walletScan',{'gap':1,'addressesPerBranch':3,'historyEntriesPerBranch':10}),
   ('broadcastApproval',{'approvedCommitment':'1'*64,'approvedTxid':'2'*64,'host':'localhost','port':1})]:
   with self.subTest(field=key):self.restore(success=False,**{key:value});self.assert_empty()
  self.wallet_call('wallet-init',success=False,extra={'restoreApproval':{'directory':str(self.source),
   'password':'PUBLIC PASSWORD','approvedDigest':self.info['backupDigest']}});self.assert_empty()

if __name__=='__main__':unittest.main(verbosity=2)
