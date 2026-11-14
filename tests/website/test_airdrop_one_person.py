"""One airdrop entry per person: a second address from the same network or browser is refused;
entries that later turn out to share a network or browser are disqualified automatically (the
oldest stays), on use and by the 15-minute tick for older entries; the owner can reinstate, which
exempts the entry from the automatic check."""
import hashlib,hmac,json,os,secrets,socket,subprocess,tempfile,time
from pathlib import Path
import requests
root=Path(__file__).resolve().parents[2];site=root/'website'
ADDR=['FWPbFrwvRqpWTMDJAAeT3wAGsCUvERHyEAZJySDBDTKJJxr4q9gNStxSZghEAmtgvd49oySaDP9WGV1KVDeDXFBfM748KwK',
 'Fj4gs1bC1Wf82NWMYCm7gaifFntWiVDz83ZYNEVtwyUmSWxPAAM8Bjy4XwbK2k91Jq5VG3ndJLHrDhs2SoYPryYEBwWxL9J',
 'FQLhxULkbyx4886T7196doS9GPMzexD9gXpsZJDwVjeRVdFCSoHnv7KPbBeGpzJBzHRCAs9UxqeoyFQMYbqSWYTfJJCEnHf',
 'FS1LtxYqTVPRDV5aAhLJzQCjDz2ViLRduE3ijDZu3osWKBjMGkV1XPk4pfDUMqt1Aiezvephdqm6YD19GKFD9ZcXVUEgtLB']
SECRET=secrets.token_hex(32)
dev=lambda:secrets.token_hex(16)
with tempfile.TemporaryDirectory() as tmp:
 private=Path(tmp)/'private';private.mkdir();(private/'airdrop-tick-secret').write_text(SECRET)
 def config(**kw):
  extra=''.join(f",'{k}'=>{v}" for k,v in kw.items())
  (private/'airdrop-config.php').write_text(f"<?php return ['enabled'=>true,'pool_xmz'=>8000,'ends_at'=>{int(time.time())+30*86400},'pool_stats'=>'http://127.0.0.1:1/x','trusted_proxies'=>['127.0.0.1/32']{extra}];\n")
 config()
 with socket.socket() as s:s.bind(('127.0.0.1',0));port=s.getsockname()[1]
 base=f'http://127.0.0.1:{port}';API=base+'/airdrop/api.php'
 env={**os.environ,'MONZERO_PRIVATE_DIR':str(private)}
 srv=subprocess.Popen(['php','-d','opcache.enable=0','-S',f'127.0.0.1:{port}','-t',str(site)],env=env,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
 try:
  for _ in range(100):
   try:
    with socket.create_connection(('127.0.0.1',port),timeout=.2):break
   except OSError:time.sleep(.1)
  def reg(a,ip,d):return requests.post(API+'?action=register',json={'address':a,'device':d},headers={'X-Real-IP':ip})
  def status(t,ip,d):return requests.get(API,params={'action':'status','token':t,'device':d},headers={'X-Real-IP':ip}).json()['status']
  def lib(code):
   r=subprocess.run(['php','-d','opcache.enable=0','-r','require "'+str(site/'lib/airdrop.php')+'"; '+code],env=env,capture_output=True,text=True);assert r.returncode==0,r.stderr;return r.stdout
  def tick():
   body=json.dumps({'balances':{}}).encode();ts=str(int(time.time()));n=secrets.token_hex(16)
   sig=hmac.new(SECRET.encode(),f'{ts}\n{n}\n'.encode()+body,hashlib.sha256).hexdigest()
   return requests.post(base+'/airdrop/tick.php',data=body,headers={'X-Airdrop-Timestamp':ts,'X-Airdrop-Nonce':n,'X-Airdrop-Signature':sig}).json()
  d1,d2,d3=dev(),dev(),dev()

  # Sign-up: one entry per network and per browser.
  ra=reg(ADDR[0],'198.51.100.1',d1);assert ra.status_code==200,ra.text;ta=ra.json()['token']
  r=reg(ADDR[1],'198.51.100.1',d2);assert r.status_code==400 and 'Only one airdrop entry' in r.json()['error'],r.text   # same network
  r=reg(ADDR[1],'203.0.113.7',d1);assert r.status_code==400 and 'Only one airdrop entry' in r.json()['error']           # same browser
  rb=reg(ADDR[1],'203.0.113.7',d2);assert rb.status_code==200;tb=rb.json()['token']                                     # a different person
  # Re-entering your own address elsewhere is fine.
  assert reg(ADDR[0],'192.0.2.50',d3).status_code==200 and not status(ta,'192.0.2.50',d3)['disqualified']

  # Later link: B opens its dashboard from A's network -> B (newer) is disqualified, A stays.
  st=status(tb,'198.51.100.1',d2);assert st['disqualified'] and st['earned']==0,st
  assert not status(ta,'198.51.100.1',d1)['disqualified']
  assert 'disqualified' in requests.post(API+'?action=submit',json={'token':tb,'task':'forum_post','detail':'someone','device':d2},headers={'X-Real-IP':'203.0.113.7'}).json()['error']
  reason=lib(f'echo adDb()->query("SELECT status_reason FROM participants WHERE address=\\"{ADDR[1]}\\"")->fetchColumn();')
  assert reason.startswith('Automatic: same device or connection') and ADDR[0][:8] in reason,reason

  # Entries made while the rule was off are caught by the tick.
  config(one_per_person='false',max_per_ip=0)
  assert reg(ADDR[2],'192.0.2.99',dev()).status_code==200 and reg(ADDR[3],'192.0.2.99',dev()).status_code==200
  config()
  r=tick();assert r['ok'] and r['duplicates_disqualified']==1,r
  rows=json.loads(lib('echo json_encode(adDb()->query("SELECT address,status FROM participants ORDER BY id")->fetchAll(PDO::FETCH_KEY_PAIR));'))
  assert rows[ADDR[2]]=='active' and rows[ADDR[3]]=='disqualified' and rows[ADDR[0]]=='active',rows

  # Owner reinstates a genuine shared connection: exempt, stays active on the next tick.
  pid=lib(f'echo adDb()->query("SELECT id FROM participants WHERE address=\\"{ADDR[3]}\\"")->fetchColumn();')
  assert lib(f'var_export(adReinstate({pid},"console:owner"));')=='NULL'
  assert tick()['duplicates_disqualified']==0
  assert json.loads(lib('echo json_encode(adDb()->query("SELECT address,exempt FROM participants WHERE status=\'active\'")->fetchAll(PDO::FETCH_KEY_PAIR));'))[ADDR[3]]==1
  # Only keyed hashes are stored, never the raw IP or device id.
  dump=(private/'airdrop.sqlite').read_bytes()
  assert b'198.51.100.1' not in dump and d1.encode() not in dump
  print('airdrop one-per-person: ok')
 finally:
  srv.terminate();srv.wait()
