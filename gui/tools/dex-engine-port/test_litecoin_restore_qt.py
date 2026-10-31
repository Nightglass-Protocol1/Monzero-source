#!/usr/bin/env python3
"""Same-worker restore approval and recovery using PUBLIC fixture keys only."""
import json,subprocess,unittest
import test_litecoin_backup_qt as b
s=b.s

class RestoreQtTests(unittest.TestCase):
 setUp=b.BackupQtTests.setUp
 tearDown=b.BackupQtTests.tearDown
 save=b.BackupQtTests.save
 call=b.BackupQtTests.call
 spend_config=b.BackupQtTests.spend_config
 payment=b.BackupQtTests.payment
 backup=b.BackupQtTests.backup
 def restore(self,directory,mode='wallet-restore',password=s.PASSWORD,success=True,**overrides):
  result=subprocess.run([str(s.q.PROBE),str(self.wrapper),mode,'3',json.dumps(dict(directory=str(directory),**overrides))],
   input=password.encode(),capture_output=True,timeout=20)
  if password:self.assertNotIn(password.encode(),result.stdout+result.stderr)
  self.assertEqual(result.returncode,0 if success else 5,result.stderr.decode())
  if success:return json.loads(result.stdout)
  self.assertEqual(result.stdout,b'')
 def test_real_restore_requires_verification_and_preserves_conflicts(self):
  self.f.value.pop('endpoint');self.save();self.call('wallet-init');self.call('wallet-current')
  directory=self.path/'backup';directory.mkdir(mode=0o700);self.backup(directory)
  original_vault=(self.path/'vault/litecoin-seed.vault').read_bytes()
  original_book=(self.book/'litecoin-address-book.json').read_bytes()
  target_vault=self.path/'new-vault';target_book=self.path/'new-book'
  target_vault.mkdir(mode=0o700);target_book.mkdir(mode=0o700)
  self.f.value['wallet']['vault']['directory']=str(target_vault)
  self.f.value['wallet']['addressBookDirectory']=str(target_book);self.save()
  self.restore(directory,'wallet-restore-direct',success=False)
  result=self.restore(directory);self.assertEqual(result['state'],'wallet-restored')
  self.assertEqual(result['backupDirectory'],str(directory))
  self.assertEqual(result['restoreDirectories'],{'vault':str(target_vault),'addressBook':str(target_book)})
  self.assertFalse(result['walletRestoreAvailable']);self.assertFalse(result['walletSignAvailable']);self.assertFalse(result['walletBroadcastAvailable'])
  self.assertEqual((target_vault/'litecoin-seed.vault').read_bytes(),original_vault)
  self.assertEqual((target_book/'litecoin-address-book.json').read_bytes(),original_book)
  self.assertEqual(self.restore(directory)['wallet'],result['wallet'])
  (target_book/'litecoin-address-book.json').write_bytes(b'conflicting fixture')
  failed=self.restore(directory);self.assertEqual(failed['state'],'unknown')
  self.assertEqual(failed['wallet'],{});self.assertEqual(failed['backupDirectory'],'')
  self.assertEqual((target_book/'litecoin-address-book.json').read_bytes(),b'conflicting fixture')
 def fake(self,case):
  b.BackupQtTests.fake(self,'normal')
  code=self.program.read_text()
  code=code.replace("assert set(request)=={'schema','requestId','backupApproval'}","assert set(request)=={'schema','requestId','restoreApproval' if mode=='wallet-restore' else 'backupApproval'}\napproval=request.get('backupApproval',request.get('restoreApproval'))")
  code=code.replace("request['backupApproval']['password']","approval['password']")
  code=code.replace("assert set(request['backupApproval'])=={'directory','password'}","assert set(approval)==({'directory','password','approvedDigest'} if mode=='wallet-restore' else {'directory','password'})\nif mode=='wallet-restore':assert approval['approvedDigest']=='1'*64")
  anchor="r=dict(schema="
  block="""if mode=='wallet-restore':
 del w['backupVerified'];del w['backupSaved'];w['restored']=True
 if CASE=='digest':w['backupDigest']='2'*64
 if CASE=='identity':w['walletId']='22222222-2222-4333-8444-555555555555'
 if CASE=='counts':w['changeAllocated']=3
 if CASE=='scope':w['scope']='full-wallet'
 if CASE=='swaps':w['activeSwapRecovery']=True
 if CASE=='restored':w['restored']=False
 if CASE=='extra':w['backupSaved']=False
 if CASE=='missing':del w['scope']
 if CASE=='cancel':time.sleep(2)
""".replace('CASE',repr(case))
  assert anchor in code;code=code.replace(anchor,block+anchor);self.program.write_text(code);self.save()
 def test_restore_receipt_matches_entire_verified_backup_and_cancellation_clears(self):
  directory=self.path/'backup';directory.mkdir(mode=0o700)
  for case in ['normal','digest','identity','counts','scope','swaps','restored','extra','missing','cancel']:
   with self.subTest(case=case):
    self.fake(case);result=self.restore(directory,'wallet-restore-cancel' if case=='cancel' else 'wallet-restore',s.ESCAPED)
    self.assertEqual(result['state'],'wallet-restored' if case=='normal' else 'unknown')
    self.assertFalse(result['walletRestoreAvailable']);self.assertFalse(result['walletSignAvailable']);self.assertFalse(result['walletBroadcastAvailable'])
    if case!='normal':self.assertEqual(result['wallet'],{});self.assertEqual(result['chain'],{});self.assertEqual(result['backupDirectory'],'')
 def test_restore_rejects_approval_for_another_backup_or_directory(self):
  directory=self.path/'backup';directory.mkdir(mode=0o700);self.fake('normal')
  self.restore(directory,'wallet-restore-direct',s.ESCAPED,success=False)
  self.restore(directory,password=s.ESCAPED,success=False,approvedDigest='2'*64)
  self.restore(directory,password=s.ESCAPED,success=False,approvedDirectory=str(self.path))
  self.restore(directory,password='',success=False)
  self.restore(directory,password='x'*1025,success=False)

if __name__=='__main__':unittest.main(verbosity=2)
