#!/usr/bin/env python3
"""Explicit Qt backup operations; PUBLIC keys and private disposable folders."""
import json,subprocess,unittest
import test_litecoin_signing_qt as s

class BackupQtTests(unittest.TestCase):
 setUp=s.SigningQtTests.setUp
 tearDown=s.SigningQtTests.tearDown
 save=s.SigningQtTests.save
 call=s.SigningQtTests.call
 spend_config=s.SigningQtTests.spend_config
 payment=s.SigningQtTests.payment
 def backup(self,directory,mode='wallet-backup-save',password=s.PASSWORD,success=True):
  result=subprocess.run([str(s.q.PROBE),str(self.wrapper),mode,'3',json.dumps({'directory':str(directory)})],
   input=password.encode(),capture_output=True,timeout=20)
  if password:self.assertNotIn(password.encode(),result.stdout+result.stderr)
  self.assertEqual(result.returncode,0 if success else 5,result.stderr.decode())
  if success:return json.loads(result.stdout)
  self.assertEqual(result.stdout,b'')
 def test_real_offline_backup_and_verified_recovery_without_live_vault(self):
  self.f.value.pop('endpoint');self.save();self.call('wallet-init');self.call('wallet-current')
  directory=self.path/'backup';directory.mkdir(mode=0o700)
  result=self.backup(directory);self.assertEqual(result['state'],'wallet-backup-saved')
  self.assertEqual(result['backupDirectory'],str(directory));self.assertEqual(result['chain'],{})
  self.assertEqual(result['wallet']['receivingAllocated'],1)
  self.assertFalse(result['walletSignAvailable']);self.assertFalse(result['walletBroadcastAvailable'])
  saved=(directory/'litecoin-wallet.backup').read_bytes()
  failed=self.backup(directory);self.assertEqual(failed['state'],'unknown');self.assertEqual(failed['wallet'],{});self.assertEqual(failed['backupDirectory'],'')
  (self.path/'vault/litecoin-seed.vault').unlink()
  verified=self.backup(directory,'wallet-backup-verify')
  self.assertEqual(verified['state'],'wallet-backup-verified');self.assertFalse(verified['wallet']['backupSaved'])
  self.assertEqual((directory/'litecoin-wallet.backup').read_bytes(),saved)
  failed=self.backup(directory,'wallet-backup-verify',password='WRONG PUBLIC PASSWORD')
  self.assertEqual(failed['state'],'unknown');self.assertEqual(failed['wallet'],{})
 def fake(self,case):
  self.program=self.path/'backup-helper.py'
  code="""#!/usr/bin/python3
import json,sys,time
request=json.load(sys.stdin);mode=sys.argv[1][2:-9];case=CASE
assert set(request)=={'schema','requestId','backupApproval'}
assert request['backupApproval']['password']==PASSWORD
assert set(request['backupApproval'])=={'directory','password'}
assert PASSWORD not in str(sys.argv)
walletId=json.load(open(sys.argv[2]))['wallet']['vault']['walletId']
w=dict(watchOnly=True,spendable=False,backupVerified=True,backupSaved=mode=='wallet-backup-save',backupDigest='1'*64,walletId=walletId,
 receivingAllocated=1,changeAllocated=2,scope='seed-and-address-allocations',allocationFreshness='unassessed',activeSwapRecovery=False)
if case=='identity':w['walletId']='22222222-2222-4333-8444-555555555555'
if case=='digest':w['backupDigest']='bad'
if case=='scope':w['scope']='full-wallet'
if case=='fresh':w['allocationFreshness']='current'
if case=='swaps':w['activeSwapRecovery']=True
if case=='verified':w['backupVerified']=False
if case=='saved':w['backupSaved']=not w['backupSaved']
if case=='count':w['receivingAllocated']=9999
if case=='fraction':w['changeAllocated']=0.5
if case=='extra':w['seed']='PUBLIC TEST ONLY'
if case=='missing':del w['scope']
if case=='cancel':time.sleep(2)
r=dict(schema='monzero-ltc-light-'+mode+'-response-v1',requestId=request['requestId'],executable=False,wallet=w)
if case=='chain':r['chain']={}
print(json.dumps(r,sort_keys=True,separators=(',',':')),end='')
""".replace('CASE',repr(case)).replace('PASSWORD',repr(s.ESCAPED))
  self.program.write_text(code);self.program.chmod(0o700);self.save()
 def test_receipts_bind_identity_scope_counts_and_operation_and_cancel_clears(self):
  directory=self.path/'backup';directory.mkdir(mode=0o700)
  for case in ['normal','identity','digest','scope','fresh','swaps','verified','saved','count','fraction','extra','missing','chain','cancel']:
   with self.subTest(case=case):
    self.fake(case);result=self.backup(directory,'wallet-backup-cancel' if case=='cancel' else 'wallet-backup-save',s.ESCAPED)
    self.assertEqual(result['state'],'wallet-backup-saved' if case=='normal' else 'unknown')
    self.assertFalse(result['walletSignAvailable']);self.assertFalse(result['walletBroadcastAvailable'])
    if case!='normal':
     self.assertEqual(result['wallet'],{});self.assertEqual(result['chain'],{});self.assertEqual(result['backupDirectory'],'')
  self.fake('normal');self.assertEqual(self.backup(directory,'wallet-backup-verify',s.ESCAPED)['state'],'wallet-backup-verified')
 def test_password_and_destination_preflight(self):
  directory=self.path/'backup';directory.mkdir(mode=0o700);self.fake('normal')
  for password in ['', 'x'*1025]:self.backup(directory,password=password,success=False)
  self.backup('relative',success=False)
  link=self.path/'link';link.symlink_to(directory,target_is_directory=True);self.backup(link,success=False)
  self.f.value['wallet'].pop('vault');self.save();self.backup(directory,success=False)

if __name__=='__main__':unittest.main(verbosity=2)
