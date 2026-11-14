"""Airdrop: closed without a budget/end date, wallet reward only after Telegram/mining, mining days
only from real activity (earnings growth or live hashrate, not a stuck unpaid balance), the signed
monzero-eu tick (signature, replay, mining days, budget renewal capped by the fee), the budget
gate on registration, budget-capped payout export, and the resumable payout script."""
import csv,hashlib,hmac,json,os,secrets,socket,sqlite3,subprocess,sys,tempfile,threading,time
from http.server import BaseHTTPRequestHandler,ThreadingHTTPServer
from pathlib import Path
import requests
root=Path(__file__).resolve().parents[2];site=root/'website';payout=root/'utils/website/airdrop_payout.py'
ADDR=['FWPbFrwvRqpWTMDJAAeT3wAGsCUvERHyEAZJySDBDTKJJxr4q9gNStxSZghEAmtgvd49oySaDP9WGV1KVDeDXFBfM748KwK',
 'Fj4gs1bC1Wf82NWMYCm7gaifFntWiVDz83ZYNEVtwyUmSWxPAAM8Bjy4XwbK2k91Jq5VG3ndJLHrDhs2SoYPryYEBwWxL9J',
 'FQLhxULkbyx4886T7196doS9GPMzexD9gXpsZJDwVjeRVdFCSoHnv7KPbBeGpzJBzHRCAs9UxqeoyFQMYbqSWYTfJJCEnHf',
 'FS1LtxYqTVPRDV5aAhLJzQCjDz2ViLRduE3ijDZu3osWKBjMGkV1XPk4pfDUMqt1Aiezvephdqm6YD19GKFD9ZcXVUEgtLB',
 'FraTu7LjjcZeKvcs4Q4FYGRR14WHcisMnQaApGDwUpiHKq7go5Bpxt7T1qpDkmbb5o85N3anx7eyj6fPvd3VegvJUKiAEGq']
MOD_TOKEN='m'*24;SECRET=secrets.token_hex(32);DAY=86400

def free_port():
 with socket.socket() as s:s.bind(('127.0.0.1',0));return s.getsockname()[1]
def serve(handler):
 srv=ThreadingHTTPServer(('127.0.0.1',0),handler);threading.Thread(target=srv.serve_forever,daemon=True).start();return srv

POOL={}  # address -> miner stats the fake pool API returns
class Pool(BaseHTTPRequestHandler):
 def log_message(self,*a):pass
 def do_GET(self):
  from urllib.parse import urlparse,parse_qs
  a=parse_qs(urlparse(self.path).query).get('address',[''])[0]
  body=json.dumps(POOL.get(a,{'hashrate':0,'pending':0,'paid':0,'workers':[]})).encode()
  self.send_response(200);self.send_header('Content-Type','application/json');self.end_headers();self.wfile.write(body)

with tempfile.TemporaryDirectory() as tmp:
 tmp=Path(tmp);private=tmp/'private';private.mkdir()
 (private/'airdrop-tick-secret').write_text(SECRET+'\n')
 pool=serve(Pool)
 def config(**kw):
  c={'one_per_person':False,'enabled':True,'pool_xmz':20,'ends_at':int(time.time())+30*DAY,'auto_renew':True,'topup_buffer':5,'max_per_ip':0,
     'mining_days_required':2,'admin_reward_xmz':0.25,'pool_stats':f'http://127.0.0.1:{pool.server_port}/api/miner',
     'admins':[{'name':'mod1','token':MOD_TOKEN,'address':ADDR[4]}]};c.update(kw)
  def php(v):
   if isinstance(v,bool):return 'true' if v else 'false'
   if isinstance(v,(int,float)):return repr(v)
   if isinstance(v,str):return "'"+v.replace("\\","\\\\").replace("'","\\'")+"'"
   if isinstance(v,list):return '['+','.join(php(x) for x in v)+']'
   return '['+','.join(f'{php(k)}=>{php(x)}' for k,x in v.items())+']'
  (private/'airdrop-config.php').write_text('<?php return '+php(c)+';\n')
 env={**os.environ,'MONZERO_PRIVATE_DIR':str(private)}
 def lib(code):  # run PHP against the library directly (for multi-day mining)
  r=subprocess.run(['php','-d','opcache.enable=0','-r','require "'+str(site/'lib/airdrop.php')+'"; '+code],env=env,capture_output=True,text=True)
  assert r.returncode==0,r.stderr+r.stdout;return r.stdout
 port=free_port();base=f'http://127.0.0.1:{port}';API=base+'/airdrop/api.php'
 server=subprocess.Popen(['php','-d','opcache.enable=0','-S',f'127.0.0.1:{port}','-t',str(site)],env=env,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
 try:
  for _ in range(100):
   try:
    with socket.create_connection(('127.0.0.1',port),timeout=.2):break
   except OSError:time.sleep(.1)
  reg=lambda a:requests.post(API+'?action=register',json={'address':a})
  status=lambda t:requests.get(API,params={'action':'status','token':t}).json()['status']
  db=lambda:sqlite3.connect(private/'airdrop.sqlite')

  # Closed without an end date, or without a budget.
  config(ends_at=0);c=requests.get(API+'?action=config').json();assert c['live'] is False
  assert reg(ADDR[0]).status_code==503
  config(pool_xmz=0,auto_renew=False);assert requests.get(API+'?action=config').json()['live'] is False
  config();c=requests.get(API+'?action=config').json();assert c['live'] is True and c['full'] is False and c['participants']==0

  # Registering pays nothing by itself: the wallet reward waits for Telegram or mining.
  # (ADDR[0] already has a small unpaid pool balance from a few minutes of mining, never paid out.)
  POOL[ADDR[0]]={'hashrate':0,'pending':50_000_000_000,'paid':0,'workers':[]}
  r=reg(ADDR[0]);assert r.status_code==200,r.text;ta=r.json()['token'];s=r.json()['status']
  assert s['tasks']['wallet']=='todo' and s['earned']==0
  assert requests.get(API+'?action=config').json()['participants']==1

  # A stuck unpaid balance is not mining: no day, however often it is seen.
  for _ in range(3):s=status(ta)
  assert s['mining_days']==0,s
  # Live hashrate counts today (once).
  POOL[ADDR[0]]={'hashrate':1200,'pending':50_000_000_000,'paid':0,'workers':[{'name':'w','difficulty':1}]}
  s=status(ta);s=status(ta);assert s['mining_days']==1 and s['tasks']['mining']=='todo'

  # Library: earnings growth counts a day; flat earnings don't; the first sighting is only a baseline.
  POOL[ADDR[1]]={'hashrate':0,'pending':0,'paid':5000,'workers':[]}  # earnings from before joining
  r=reg(ADDR[1]);tb=r.json()['token'];pid=db().execute('SELECT id FROM participants WHERE address=?',(ADDR[1],)).fetchone()[0]
  now=int(time.time())
  out=lib(f'''$n={now}; $p={pid};
   echo json_encode([adObserveMining($p, 5000, false, $n), adMiningDays($p),
     adObserveMining($p, 5000, false, $n+{DAY}), adMiningDays($p),
     adObserveMining($p, 9000, false, $n+{DAY}), adMiningDays($p),
     adObserveMining($p, 9500, false, $n+{2*DAY}), adMiningDays($p)]);''')
  assert json.loads(out)==[False,0,False,0,False,1,True,2],out
  s=status(tb);assert s['tasks']['mining']=='done' and s['tasks']['wallet']=='done' and s['earned']==7.0,s  # 6 + 1 unlocked

  # Telegram verification also unlocks the wallet reward.
  pc=json.loads(lib(f'''$r=adRegister("{ADDR[2]}", null); adAward((int)$r["participant"]["id"], "telegram"); echo json_encode(adStatus($r["participant"]));'''))
  assert pc['tasks']['wallet']=='done' and pc['tasks']['telegram']=='done' and pc['earned']==2.0,pc

  # Signed tick from monzero-eu.
  def tick(payload,ts=None,nonce=None,secret=SECRET):
   body=json.dumps(payload).encode();ts=ts or str(int(time.time()));nonce=nonce or secrets.token_hex(16)
   sig=hmac.new(secret.encode(),f'{ts}\n{nonce}\n'.encode()+body,hashlib.sha256).hexdigest()
   return requests.post(base+'/airdrop/tick.php',data=body,headers={'X-Airdrop-Timestamp':ts,'X-Airdrop-Nonce':nonce,'X-Airdrop-Signature':sig})
  assert tick({'balances':{}},secret='0'*64).status_code==401
  assert tick({'balances':{}},ts=str(int(time.time())-3600)).status_code==401
  n=secrets.token_hex(16);assert tick({'balances':{}},nonce=n).status_code==200;assert tick({'balances':{}},nonce=n).status_code==409
  assert requests.get(base+'/airdrop/tick.php').status_code==405
  # ADDR[3]: first tick is a baseline, growth on the next tick records today.
  POOL[ADDR[3]]={'hashrate':0,'pending':0,'paid':700,'workers':[]}
  r=reg(ADDR[3]);td=r.json()['token']
  r=tick({'balances':{ADDR[3]:700}}).json();assert r['ok'] and r['checked']>=1
  assert status(td)['mining_days']==0
  tick({'balances':{ADDR[3]:700}});assert status(td)['mining_days']==0
  POOL[ADDR[3]]['paid']=650  # a lower reading (e.g. a stale API answer) must not lower the baseline
  assert status(td)['mining_days']==0
  POOL[ADDR[3]]['paid']=900
  tick({'balances':{ADDR[3]:900}});assert status(td)['mining_days']==1

  # Budget renewal from the tick: max(base, committed + buffer), never above the fee collected.
  committed=json.loads(lib('echo json_encode(adBudget());'))['committed']
  r=tick({'balances':{},'fee_atomic':15*10**11}).json();assert r['pool_xmz']==15,r
  assert (private/'airdrop-pool.txt').read_text().strip()=='15'
  r=tick({'balances':{},'fee_atomic':10**6*10**11}).json();assert r['pool_xmz']==max(20,round(committed+5,2)),(r,committed)
  config(auto_renew=False);assert requests.get(API+'?action=config').json()['pool_xmz']==20  # override ignored without auto_renew
  config()

  # Moderator approval + budget gate: push earnings past the budget.
  assert requests.post(API+'?action=submit',json={'token':ta,'task':'share_x','detail':'https://x.com/a/status/1'}).status_code==200
  pend=requests.get(API+'?action=admin.pending',headers={'X-Airdrop-Admin':MOD_TOKEN}).json()['pending']
  assert requests.post(API+'?action=admin.review',headers={'X-Airdrop-Admin':MOD_TOKEN},json={'id':pend[0]['id'],'decision':'approve'}).json()['ok']
  (private/'airdrop-pool.txt').unlink()
  con=db()
  for pid,in con.execute('SELECT id FROM participants').fetchall():
   for t in ['forum_active','run_node','forum_post','mining']:con.execute('INSERT OR REPLACE INTO progress VALUES (?,?,?,?)',(pid,t,{'forum_active':500,'run_node':400,'forum_post':250,'mining':600}[t],0))
  con.commit()
  c=requests.get(API+'?action=config').json();assert c['full'] is True
  r=reg(ADDR[4]);assert r.status_code==400 and 'fully committed' in r.json()['error']
  exp=requests.get(API+'?action=admin.export',headers={'X-Airdrop-Admin':MOD_TOKEN}).json()
  assert exp['scale']<1 and sum(p['atomic'] for p in exp['payouts'])<=20*10**11,exp
  mod=[p for p in exp['payouts'] if p['kind']=='moderator'];assert mod==[{'kind':'moderator','address':ADDR[4],'atomic':25_000_000_000,'xmz':0.25}],mod
  assert requests.get(API+'?action=admin.export').status_code==401
  csv_text=requests.get(base+'/airdrop/admin.php',params={'key':MOD_TOKEN,'export':'csv'}).text
  rows=list(csv.DictReader(csv_text.splitlines()));assert len(rows)==len(exp['payouts']) and sum(int(r['atomic']) for r in rows)<=20*10**11
  print('airdrop site: ok')
 finally:
  server.terminate();server.wait()

 # ---- Payout script against a fake wallet ----
 W={'balance':10**6*10**11,'sent':[],'reject':set(),'hang':False}
 class Wallet(BaseHTTPRequestHandler):
  def log_message(self,*a):pass
  def do_POST(self):
   req=json.loads(self.rfile.read(int(self.headers['Content-Length'])));m,p=req['method'],req['params']
   if m=='get_balance':res={'result':{'balance':W['balance'],'unlocked_balance':W['balance']}}
   elif m=='transfer':
    if W['hang']:time.sleep(3);return
    bad=[d['address'] for d in p['destinations'] if d['address'] in W['reject']]
    assert len(p['destinations'])<=15
    if bad:res={'error':{'code':-4,'message':'refused '+bad[0][:6]}}
    else:
     W['sent'].append(p['destinations']);W['balance']-=sum(d['amount'] for d in p['destinations'])
     res={'result':{'tx_hash':secrets.token_hex(32),'fee':10**7}}
   body=json.dumps({'jsonrpc':'2.0','id':'0',**res}).encode()
   self.send_response(200);self.send_header('Content-Type','application/json');self.end_headers();self.wfile.write(body)
 wal=serve(Wallet);url=f'http://127.0.0.1:{wal.server_port}'
 alphabet='123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz'
 addrs=['F'+''.join(secrets.choice(alphabet) for _ in range(94)) for _ in range(33)]
 csvp=tmp/'pay.csv';ledger=tmp/'ledger.jsonl'
 csvp.write_text('kind,address,atomic,xmz\n'+''.join(f'participant,{a},{(i+1)*10**10},0\n' for i,a in enumerate(addrs)))
 total=sum((i+1)*10**10 for i in range(33))
 run=lambda *a,timeout=60:subprocess.run([sys.executable,'-I',str(payout),'--csv',str(csvp),'--wallet-rpc',url,'--ledger',str(ledger),*a],capture_output=True,text=True,timeout=timeout)
 r=run();assert r.returncode==0 and 'Dry run' in r.stdout and not W['sent'],r.stdout+r.stderr
 W['reject']={addrs[3]}
 r=run('--send','--yes');assert r.returncode==1 and 'rejected' in r.stdout,r.stdout+r.stderr
 paid={d['address']:d['amount'] for b in W['sent'] for d in b}
 assert len(paid)==32 and addrs[3] not in paid and all(len(b)<=15 for b in W['sent'])
 W['reject']=set();r=run('--send','--yes');assert r.returncode==0,r.stdout+r.stderr
 paid={};[paid.__setitem__(d['address'],paid.get(d['address'],0)+d['amount']) for b in W['sent'] for d in b]
 assert sum(paid.values())==total and len(paid)==33  # everyone exactly once
 n=len(W['sent']);r=run('--send','--yes');assert r.returncode==0 and len(W['sent'])==n and 'Still due: 0' in r.stdout
 # A re-export with a higher amount pays only the difference.
 csvp.write_text(csvp.read_text().replace(f'{addrs[0]},{10**10}',f'{addrs[0]},{3*10**10}'))
 r=run('--send','--yes');assert r.returncode==0 and W['sent'][-1]==[{'address':addrs[0],'amount':2*10**10}],W['sent'][-1]
 # Unknown outcome: stop, refuse to continue until resolved.
 csvp.write_text(csvp.read_text()+f'participant,{"F"+"a"*94},{10**11},1\n');W['hang']=True
 r=subprocess.run([sys.executable,'-I','-c',f'import sys;sys.argv=["x","--csv",{str(csvp)!r},"--wallet-rpc",{url!r},"--ledger",{str(ledger)!r},"--send","--yes"];sys.path.insert(0,{str(payout.parent)!r});import airdrop_payout as p;p.Wallet.__init__.__defaults__=(None,1);p.main()'],capture_output=True,text=True,timeout=60)
 assert r.returncode!=0 and 'UNKNOWN' in r.stderr,r.stdout+r.stderr
 W['hang']=False;r=run('--send','--yes');assert r.returncode!=0 and 'unresolved' in r.stderr
 batch=[json.loads(l)['batch'] for l in ledger.read_text().splitlines() if json.loads(l)['state']=='unknown'][0]
 r=run('--resolve',f'{batch}=failed','--send','--yes');assert r.returncode==0 and W['sent'][-1]==[{'address':'F'+'a'*94,'amount':10**11}],r.stdout+r.stderr
 print('airdrop payout: ok')
