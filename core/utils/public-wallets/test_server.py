import unittest,tempfile,pathlib,time,sqlite3,hashlib
from server import Directory,GENESIS,message
OTHER='F'+'1'*94
class Profiles(unittest.TestCase):
 def setUp(self):
  self.temp=tempfile.TemporaryDirectory();self.d=Directory(str(pathlib.Path(self.temp.name)/'profiles.db'),lambda p:p['signature']==hashlib.sha256(p['message'].encode()).hexdigest())
 def tearDown(self):self.temp.cleanup()
 def payload(self,mode='balance',address=OTHER):return dict(address=address,mode=mode,name='' if mode=='private' else 'Example',balance='123456789012345678' if mode=='balance' else '-',height='12345' if mode=='balance' else '-')
 def signed(self,p):
  c=self.d.challenge(p);return dict(nonce=c['nonce'],signature=hashlib.sha256(message(p,c['nonce'],c['expires'],c['revision']).encode()).hexdigest())
 def test_genesis_permanent_historical(self):
  p=self.d.search('genesiswallet1')['profiles'];self.assertEqual(len(p),1);self.assertTrue(p[0]['pinned']);self.assertIsNone(p[0]['updated']);self.assertFalse(p[0]['balanceVerified'])
  with self.assertRaises(ValueError):self.d.challenge(self.payload('private',GENESIS))
 def test_private_default_and_challenge_not_public(self):
  self.assertEqual(self.d.search(OTHER)['profiles'],[]);self.d.challenge(self.payload());self.assertEqual(self.d.search(OTHER)['profiles'],[])
 def test_publish_exact_atoms_and_hide(self):
  self.d.update(self.signed(self.payload()));self.assertEqual(self.d.search(OTHER)['profiles'][0]['balance'],'123456789012345678')
  self.d.update(self.signed(self.payload('private')));self.assertEqual(self.d.search(OTHER)['profiles'],[])
  with self.d.db() as db:
   row=db.execute('SELECT * FROM profiles WHERE address=?',(OTHER,)).fetchone();self.assertIsNone(row['balance']);self.assertEqual(row['name'],'')
 def test_address_only_redacts_balance(self):
  self.d.update(self.signed(self.payload()));self.d.update(self.signed(self.payload('address')));self.assertIsNone(self.d.search(OTHER)['profiles'][0]['balance'])
 def test_unsigned_request_and_tamper_rejected(self):
  p=self.signed(self.payload());p['signature']='wrong'
  with self.assertRaises(ValueError):self.d.update(p)
  self.assertEqual(self.d.search(OTHER)['profiles'],[])
 def test_replay_rejected_after_removal(self):
  p=self.signed(self.payload());self.d.update(p);self.d.update(self.signed(self.payload('private')))
  with self.assertRaises(ValueError):self.d.update(p)
 def test_concurrent_challenges_invalidate_old_consent(self):
  a=self.signed(self.payload());b=self.signed(self.payload('address'));self.d.update(b)
  with self.assertRaises(ValueError):self.d.update(a)
 def test_expiry(self):
  p=self.signed(self.payload())
  with self.d.db() as db:db.execute('UPDATE challenges SET expires=?',(int(time.time())-1,))
  with self.assertRaises(ValueError):self.d.update(p)
 def test_persistence(self):
  self.d.update(self.signed(self.payload()));d=Directory(self.d.path,self.d.verifier);self.assertEqual(len(d.search(OTHER)['profiles']),1)
 def test_no_balance_in_address_request(self):
  p=self.payload();p['mode']='address'
  with self.assertRaises(ValueError):self.d.challenge(p)
 def test_reserved_name_and_injection(self):
  for name in ['GenesisWallet1','<script>alert(1)</script>','x\nMode:balance']:
   p=self.payload();p['name']=name
   with self.assertRaises(ValueError):self.d.challenge(p)
 def test_search_wildcards_literal(self):self.assertEqual(self.d.search('%')['profiles'],[])
 def test_rate_limit(self):
  for _ in range(30):self.d.rate('one')
  with self.assertRaises(ValueError):self.d.rate('one')
  self.d.rate('two')
if __name__=='__main__':unittest.main()
