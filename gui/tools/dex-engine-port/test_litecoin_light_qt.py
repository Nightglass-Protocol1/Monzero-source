#!/usr/bin/env python3
"""Qt launcher and real helper fixtures. Loopback TLS; no wallet/node/miner."""
import hashlib,importlib.util,json,os,subprocess,sys,unittest
from pathlib import Path
PROBE=Path(sys.argv.pop(1)).resolve();HELPER=Path(sys.argv[1]).resolve();FIXTURE=Path(sys.argv[2]).resolve()
spec=importlib.util.spec_from_file_location('light_helper_fixture',Path(__file__).with_name('test_litecoin_light_helper.py'))
base=importlib.util.module_from_spec(spec);spec.loader.exec_module(base)
def write(path,value):path.write_text(json.dumps(value,sort_keys=True,separators=(',',':')));path.chmod(0o600)
class QtTests(unittest.TestCase):
 def setUp(self):
  self.f=base.HelperTests();self.f.setUp();self.path=self.f.path;self.config=self.f.config
  self.wrapper=self.path/'launcher.json';self.program=HELPER;self.save()
 def tearDown(self):self.f.tearDown()
 def save(self):
  write(self.config,self.f.value)
  write(self.wrapper,{'schema':'monzero-private-ltc-light-launcher-v1','helper':str(self.program),
   'helperDigest':hashlib.sha256(self.program.read_bytes()).hexdigest(),'configFile':str(self.config)})
 def call(self,operation='status',budget=10,configured=True):
  env=os.environ.copy();env['SSL_CERT_FILE']=str(FIXTURE/'root.pem')
  result=subprocess.run([str(PROBE),str(self.wrapper),operation,str(budget)],capture_output=True,env=env,timeout=20)
  if not configured:self.assertNotEqual(result.returncode,0);self.assertEqual(result.stdout,b'');return
  self.assertEqual(result.returncode,0,result.stderr.decode());value=json.loads(result.stdout)
  if operation=='repeat':return value
  self.assertFalse(value['busy']);self.assertFalse(value['executable']);return value
 def test_real_helper_offline_sync_restart_and_reorg(self):
  first=self.call();self.assertEqual(first['state'],'loaded');self.assertEqual(first['chain']['height'],0)
  self.assertFalse(first['syncAvailable'])
  with base.Peer(self.path,[base.CHILD]) as peer:
   self.f.value['endpoint']={'host':'localhost','port':peer.port};self.save()
   result=self.call('sync');self.assertEqual(result['state'],'checked');self.assertEqual(result['chain']['height'],1)
   self.assertEqual(result['chain']['freshness'],'unassessed');self.assertEqual(result['synchronization']['downloadedHeaders'],1)
  self.assertEqual(self.call()['chain']['tip'],base.block_hash(base.CHILD))
  with base.Peer(self.path,[base.ALTERNATIVE,base.DESCENDANT]) as peer:
   self.f.value['endpoint']['port']=peer.port;self.save();result=self.call('sync')
   self.assertEqual(result['state'],'checked');self.assertEqual(result['chain']['height'],2)
   self.assertEqual(result['synchronization']['change']['commonAncestor'],0)
  raw=bytearray((self.f.cache/'headers.bin').read_bytes());raw[48]^=1;(self.f.cache/'headers.bin').write_bytes(raw)
  failed=self.call();self.assertEqual(failed['state'],'unknown');self.assertEqual(failed['chain'],{})
 def fake(self,mode):
  self.program=self.path/'helper.py'
  self.program.write_text('''#!/usr/bin/python3
import json,sys,time
from pathlib import Path
mode=MODE
request=json.load(sys.stdin)
if mode=='second-bad':
 marker=Path(sys.argv[2]+'.calls')
 mode='identity' if marker.exists() else 'normal'
 marker.touch()
if mode=='stall':time.sleep(60)
chain={'network':'regtest','height':1,'tip':'1'*64,'chainWork':'4','freshness':'unassessed'}
response={'schema':'monzero-ltc-light-status-response-v1','requestId':request['requestId'],'executable':False,'chain':chain,'synchronization':None}
if mode.startswith('sync'):
 response['schema']='monzero-ltc-light-sync-response-v1'
 sync={'reportedHeight':1,'reportedTip':'1'*64,'downloadedHeaders':1,'change':None,'reportedTipOnSelectedChain':True}
 response['synchronization']=sync
 if mode=='sync-budget':sync['downloadedHeaders']=11
 if mode=='sync-flag':sync['reportedTipOnSelectedChain']='true'
 if mode=='sync-mismatch':sync['reportedTip']='2'*64
 if mode=='sync-height':sync['reportedHeight']=11
 if mode=='sync-change':sync['change']={'commonAncestor':1,'oldHeight':1,'oldTip':'2'*64,'newHeight':1,'newTip':'1'*64}
 if mode=='sync-extra':sync['unknown']=True
if mode=='identity':response['requestId']='00000000-0000-0000-0000-000000000001'
if mode=='execute':response['executable']=True
if mode=='network':chain['network']='main'
if mode=='freshness':chain['freshness']='current'
if mode=='height':chain['height']=1.5
if mode=='work':chain['chainWork']='04'
if mode=='hash':chain['tip']='bad'
if mode=='extra':response['extra']=True
if mode=='missing':response.pop('synchronization')
if mode=='replace':Path(sys.argv[2]).write_text(Path(sys.argv[2]).read_text()+'\\n')
if mode=='stderr':sys.stderr.write('unexpected diagnostic')
if mode=='oversized':sys.stdout.write('x'*4097);sys.exit(0)
wire=json.dumps(response,sort_keys=True,separators=(',',':'))
if mode=='newline':wire+='\\n'
sys.stdout.write(wire)
'''.replace('MODE',repr(mode)))
  self.program.chmod(0o700)
  if mode.startswith('sync'):self.f.value['endpoint']={'host':'localhost','port':1}
  self.save()
 def test_response_boundaries(self):
  for mode in ['identity','execute','network','freshness','height','work','hash','extra','missing','replace','stderr','oversized','newline']:
   with self.subTest(mode=mode):
    self.fake(mode);result=self.call();self.assertEqual(result['state'],'unknown');self.assertEqual(result['chain'],{})
 def test_cancellation_clears_evidence(self):
  self.fake('stall');result=self.call('cancel');self.assertEqual(result['state'],'unknown');self.assertEqual(result['chain'],{})
 def test_sync_response_details(self):
  self.fake('sync');self.assertEqual(self.call('sync')['state'],'checked')
  for mode in ['sync-budget','sync-flag','sync-mismatch','sync-height','sync-change','sync-extra']:
   with self.subTest(mode=mode):
    self.fake(mode);result=self.call('sync');self.assertEqual(result['state'],'unknown');self.assertEqual(result['chain'],{})
 def test_failed_refresh_clears_previously_loaded_evidence(self):
  self.fake('second-bad');result=self.call('repeat')
  self.assertEqual(result['before']['state'],'loaded');self.assertNotEqual(result['before']['chain'],{})
  self.assertEqual(result['after']['state'],'unknown');self.assertEqual(result['after']['chain'],{})
 def test_configuration_and_digest_boundaries(self):
  self.wrapper.chmod(0o644);self.call(configured=False);self.save()
  wrapper=json.loads(self.wrapper.read_text());wrapper['helperDigest']='0'*64;write(self.wrapper,wrapper);self.call(configured=False)
  self.save();self.f.value['maximumHeight']=1.5;self.save();self.call(configured=False)
  self.f.value['maximumHeight']=10;self.f.value['unknown']=1;self.save();self.call(configured=False)
 def test_sync_budget_refusal_returns_no_chain_evidence(self):
  with base.Peer(self.path,[base.ALTERNATIVE,base.DESCENDANT]) as peer:
   self.f.value['endpoint']={'host':'localhost','port':peer.port};self.save()
   result=self.call('sync',1);self.assertEqual(result['state'],'unknown');self.assertEqual(result['chain'],{})
   self.assertFalse((self.f.cache/'headers.bin').exists())
if __name__=='__main__':unittest.main(verbosity=2)
