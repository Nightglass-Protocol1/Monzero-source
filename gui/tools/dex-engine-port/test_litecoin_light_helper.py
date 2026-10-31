#!/usr/bin/env python3
"""Production read-only helper fixtures. Local TLS only; no nodes, mining or wallets."""
import hashlib,json,os,socket,ssl,subprocess,sys,tempfile,threading,unittest,uuid
from pathlib import Path
from cryptography.hazmat.primitives.serialization import load_der_private_key,Encoding,PrivateFormat,NoEncryption
PROGRAM=Path(sys.argv.pop(1)).resolve();FIXTURE=Path(sys.argv.pop(1)).resolve()
GENESIS='530827f38f93b43ed12af0b3ad25a288dc02ed74d6d7857862df51fc56c416f9'
CHILD=bytes.fromhex('00000020f916c456fc51df627885d7d674ed02dc88a225adb3f02ad13eb4938ff3270853111111111111111111111111111111111111111111111111111111111111111170e6494dffff7f202a000000')
ALTERNATIVE=CHILD[:-4]+bytes(4)
DROP_RESPONSE=object()
DESCENDANT=bytes.fromhex('000000209d08f789f2afaf053878d9b21add2f847fff4a9c32fb58b8c5c79ca166f94a9d111111111111111111111111111111111111111111111111111111111111111106e7494dffff7f2000000000')
def block_hash(raw):return hashlib.sha256(hashlib.sha256(raw).digest()).digest()[::-1].hex()
class Peer:
 def __init__(self,path,branch,empty=False,wallet=None):
  self.wallet=wallet
  self.branch=branch;self.empty=empty;self.requests=[];self.errors=[];self.stop=threading.Event()
  cert=path/'server.pem';key=path/'key.pem'
  cert.write_text(ssl.DER_cert_to_PEM_cert((FIXTURE/'server.der').read_bytes()))
  key.write_bytes(load_der_private_key((FIXTURE/'server-key.der').read_bytes(),None).private_bytes(Encoding.PEM,PrivateFormat.PKCS8,NoEncryption()));key.chmod(0o600)
  self.context=ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER);self.context.load_cert_chain(cert,key)
  self.listener=socket.socket();self.listener.bind(('127.0.0.1',0));self.port=self.listener.getsockname()[1]
  self.listener.listen();self.listener.settimeout(.2)
 def __enter__(self):
  self.thread=threading.Thread(target=self.run,daemon=True);self.thread.start();return self
 def __exit__(self,*args):
  self.stop.set();self.listener.close();self.thread.join(7)
  assert not self.thread.is_alive(),'TLS mock failed to stop'
  assert not self.errors,self.errors
 def run(self):
  while not self.stop.is_set():
   try:connection,_=self.listener.accept()
   except socket.timeout:continue
   except OSError:
    if self.stop.is_set():return
    raise
   try:
    connection.settimeout(5)
    with self.context.wrap_socket(connection,server_side=True) as tls:
     with tls.makefile('rwb') as stream:
      while True:
       line=stream.readline(4097)
       if not line:break
       assert len(line)<=4096
       request=json.loads(line);method=request['method'];params=request['params'];self.requests.append(request)
       if method=='server.version':result=['fixture','1.4']
       elif method=='server.features':result={'genesis_hash':GENESIS,'hash_function':'sha256'}
       elif method=='blockchain.headers.subscribe':result={'height':len(self.branch),'hex':self.branch[-1].hex()}
       elif method=='blockchain.block.header':result=self.branch[params[0]-1].hex()
       elif method=='blockchain.block.headers':
        start,count=params;assert 1<=count<=2016
        headers=[] if self.empty else self.branch[start-1:start-1+count]
        result={'count':len(headers),'hex':b''.join(headers).hex(),'max':2016}
       elif self.wallet is not None:result=self.wallet(method,params)
       else:raise AssertionError(method)
       if result is DROP_RESPONSE:break
       stream.write(json.dumps({'jsonrpc':'2.0','id':request['id'],'result':result}).encode()+b'\n');stream.flush()
   except ssl.SSLError:pass # expected untrusted-root fixture
   except Exception as error:self.errors.append(repr(error));return
class HelperTests(unittest.TestCase):
 def setUp(self):
  self.temp=tempfile.TemporaryDirectory(prefix='monzero-light-helper-');self.path=Path(self.temp.name).resolve()
  self.cache=self.path/'cache';self.cache.mkdir(mode=0o700);self.config=self.path/'config.json'
  self.value={'schema':'monzero-private-ltc-light-v1','network':'regtest','cacheDirectory':str(self.cache),'maximumHeight':10}
  self.save_config()
 def tearDown(self):self.temp.cleanup()
 def save_config(self):self.config.write_text(json.dumps(self.value));self.config.chmod(0o600)
 def call(self,sync=False,budget=10,request=None,success=True,root='root.pem',path=None):
  identity=str(uuid.uuid4());mode='sync' if sync else 'status'
  if request is None:
   request={'schema':f'monzero-ltc-light-{mode}-request-v1','requestId':identity}
   if sync:request['maximumDownloadHeaders']=budget
  raw=request if isinstance(request,bytes) else json.dumps(request).encode()
  env=os.environ.copy();env['SSL_CERT_FILE']=str(FIXTURE/root)
  result=subprocess.run([str(PROGRAM),f'--{mode}-stdio-v1',str(path or self.config)],input=raw,capture_output=True,env=env,timeout=20)
  if not success:
   self.assertNotEqual(result.returncode,0);self.assertEqual(result.stdout,b'');return
  self.assertEqual(result.returncode,0,result.stderr.decode());self.assertEqual(result.stderr,b'')
  reply=json.loads(result.stdout);self.assertEqual(reply['requestId'],identity);self.assertFalse(reply['executable'])
  self.assertEqual(reply['chain']['freshness'],'unassessed');self.assertEqual(reply['chain']['network'],'regtest')
  self.assertEqual(reply['schema'],f'monzero-ltc-light-{mode}-response-v1');return reply
 def endpoint(self,peer):self.value['endpoint']={'host':'localhost','port':peer.port};self.save_config()
 def test_status_is_offline_and_missing_cache_is_genesis(self):
  self.value['endpoint']={'host':'invalid.invalid','port':1};self.save_config()
  result=self.call();self.assertEqual(result['chain']['height'],0);self.assertEqual(result['chain']['tip'],GENESIS)
  self.assertEqual(result['chain']['chainWork'],'2');self.assertIsNone(result['synchronization']);self.assertEqual(list(self.cache.iterdir()),[])
 def test_sync_persist_status_same_tip_and_reorganization(self):
  with Peer(self.path,[CHILD]) as peer:
   self.endpoint(peer);result=self.call(sync=True)
   self.assertEqual(result['chain']['tip'],block_hash(CHILD));self.assertEqual(result['synchronization']['downloadedHeaders'],1)
   self.assertEqual(len(peer.requests),6)
   before=(self.cache/'headers.bin').read_bytes();same=self.call(sync=True)
   self.assertEqual(same['synchronization']['downloadedHeaders'],0);self.assertIsNone(same['synchronization']['change'])
   self.assertEqual((self.cache/'headers.bin').read_bytes(),before)
  self.value.pop('endpoint');self.save_config();self.assertEqual(self.call()['chain']['height'],1)
  with Peer(self.path,[ALTERNATIVE,DESCENDANT]) as peer:
   self.endpoint(peer);result=self.call(sync=True)
   self.assertEqual(result['chain']['height'],2);self.assertEqual(result['chain']['tip'],block_hash(DESCENDANT))
   self.assertEqual(result['synchronization']['change']['commonAncestor'],0)
  self.assertEqual(self.call()['chain']['chainWork'],'6')
 def test_weaker_branch_does_not_replace_selected(self):
  with Peer(self.path,[ALTERNATIVE,DESCENDANT]) as peer:self.endpoint(peer);self.call(sync=True)
  before=(self.cache/'headers.bin').read_bytes()
  with Peer(self.path,[CHILD]) as peer:
   self.endpoint(peer);result=self.call(sync=True)
   self.assertFalse(result['synchronization']['reportedTipOnSelectedChain']);self.assertIsNone(result['synchronization']['change'])
  self.assertEqual((self.cache/'headers.bin').read_bytes(),before)
 def test_budget_and_empty_data_cannot_publish_partial_chain(self):
  with Peer(self.path,[ALTERNATIVE,DESCENDANT]) as peer:
   self.endpoint(peer);self.call(sync=True,budget=1,success=False)
   self.assertNotIn('blockchain.block.headers',[r['method'] for r in peer.requests])
  self.assertFalse((self.cache/'headers.bin').exists())
  with Peer(self.path,[CHILD],empty=True) as peer:self.endpoint(peer);self.call(sync=True,success=False)
  self.assertFalse((self.cache/'headers.bin').exists())
 def test_untrusted_tls_refused_before_rpc(self):
  with Peer(self.path,[CHILD]) as peer:
   self.endpoint(peer);self.call(sync=True,root='other-root.pem',success=False);self.assertEqual(peer.requests,[])
  self.assertFalse((self.cache/'headers.bin').exists())
 def test_malformed_requests_do_not_create_cache(self):
  for request in [b'x'*513,b'{}',b'{"schema":"monzero-ltc-light-status-request-v1","requestId":"00000000-0000-0000-0000-000000000000"}',
   {'schema':'monzero-ltc-light-status-request-v1','requestId':str(uuid.uuid4()),'extra':True},
   {'schema':'monzero-ltc-light-status-request-v1','requestId':str(uuid.uuid4()),'maximumDownloadHeaders':1}]:self.call(request=request,success=False)
  for budget in [0,10_000_001]:self.call(sync=True,budget=budget,success=False)
  self.assertEqual(list(self.cache.iterdir()),[])
 def test_config_boundary_and_corrupt_cache_fail_closed(self):
  self.config.chmod(0o644);self.call(success=False);self.config.chmod(0o600)
  link=self.path/'link.json';link.symlink_to(self.config);self.call(path=link,success=False)
  self.value['unknown']=True;self.save_config();self.call(success=False);self.value.pop('unknown');self.save_config()
  with Peer(self.path,[CHILD]) as peer:self.endpoint(peer);self.call(sync=True)
  self.value['network']='main';self.save_config();self.call(success=False)
  self.value['network']='regtest';self.save_config();record=self.cache/'headers.bin';raw=bytearray(record.read_bytes());raw[48]^=1;record.write_bytes(raw)
  self.call(success=False)
if __name__=='__main__':unittest.main(verbosity=2)
