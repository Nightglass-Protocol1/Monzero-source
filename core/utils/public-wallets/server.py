#!/usr/bin/env python3
"""Opt-in public profiles. No private keys, view keys, wallet files or node RPC."""
import hashlib, hmac, http.server, json, os, re, secrets, sqlite3, subprocess, time
from urllib.parse import urlsplit, parse_qs
GENESIS='FRJ2nMKxwd94nsgTLnjpJ77d3gZ2X5NxdUkTcFXuoNaWSQBNp5syt7dcwVsGPYP6vE1hyuLQcVgTtHKkNBUhpJoENAhsryR'
ADDRESS=re.compile(r'[1-9A-HJ-NP-Za-km-z]{95}\Z')
NAME=re.compile(r'[A-Za-z0-9 _.-]{0,40}\Z')
ORIGIN='https://monzero.org'
def message(p, nonce, expires, revision):
 return '\n'.join(['Monzero public wallet profile v1','Origin:'+ORIGIN,'Address:'+p['address'],'Mode:'+p['mode'],'Balance:'+p['balance'],'Height:'+p['height'],'Name:'+p['name'],'Nonce:'+nonce,'Expires:'+str(expires),'Revision:'+str(revision)])
class Directory:
 def __init__(self, path, verifier):
  self.path=path;self.verifier=verifier
  with self.db() as db:
   db.executescript('CREATE TABLE IF NOT EXISTS profiles(address TEXT PRIMARY KEY,revision INTEGER NOT NULL,mode TEXT NOT NULL,name TEXT NOT NULL,balance TEXT,height TEXT,updated INTEGER,source TEXT); CREATE TABLE IF NOT EXISTS challenges(nonce TEXT PRIMARY KEY,address TEXT,payload TEXT,message TEXT,revision INTEGER,expires INTEGER); CREATE TABLE IF NOT EXISTS rates(bucket TEXT PRIMARY KEY,count INTEGER,expires INTEGER);')
   db.execute('INSERT OR IGNORE INTO profiles VALUES(?,0,?,?,?,?,?,?)',(GENESIS,'balance','GenesisWallet1','376052800000000000','9838',None,'Historical founder disclosure; not a live balance'))
 def db(self):
  db=sqlite3.connect(self.path,timeout=5);db.row_factory=sqlite3.Row;return db
 def search(self,q):
  if len(q)>100:raise ValueError('Search is too long')
  with self.db() as db:
   rows=db.execute("SELECT address,mode,name,balance,height,updated,source FROM profiles WHERE mode!='private' AND (address=? OR name LIKE ? ESCAPE '\\' COLLATE NOCASE) ORDER BY address=? DESC,updated DESC LIMIT 20",(q,'%'+q.replace('\\','\\\\').replace('%','\\%').replace('_','\\_')+'%',GENESIS)).fetchall()
  return {'profiles':[dict(r) | {'pinned':r['address']==GENESIS,'balanceVerified':False} for r in rows]}
 def rate(self,ip):
  now=int(time.time());bucket=hashlib.sha256((ip+':'+str(now//60)).encode()).hexdigest()
  with self.db() as db:
   db.execute('DELETE FROM rates WHERE expires<?',(now,));db.execute('INSERT INTO rates VALUES(?,1,?) ON CONFLICT(bucket) DO UPDATE SET count=count+1',(bucket,now+120))
   if db.execute('SELECT count FROM rates WHERE bucket=?',(bucket,)).fetchone()[0]>30:raise ValueError('Too many requests; try again shortly')
 def challenge(self,p):
  if set(p)!={'address','mode','name','balance','height'} or any(not isinstance(v,str) for v in p.values()):raise ValueError('Invalid profile fields')
  if not ADDRESS.fullmatch(p['address']) or p['mode'] not in ('private','address','balance') or not NAME.fullmatch(p['name']):raise ValueError('Invalid address, mode or name')
  if p['address']==GENESIS:
   if p['mode']!='balance' or p['name']!='GenesisWallet1':raise ValueError('GenesisWallet1 is a permanent public disclosure')
  elif p['name'].lower().replace(' ','') in ('genesis','genesiswallet1'):raise ValueError('Reserved profile name')
  if p['mode']=='balance':
   if not re.fullmatch(r'0|[1-9][0-9]{0,20}',p['balance']) or int(p['balance'])>100000000*10**12:raise ValueError('Invalid atomic balance')
   if not re.fullmatch(r'[1-9][0-9]{0,11}',p['height']):raise ValueError('A synchronized wallet height is required')
  elif p['balance']!='-' or p['height']!='-':raise ValueError('Address-only and private profiles must not contain balances')
  if p['mode']=='private' and p['name']!='':raise ValueError('Private profiles must not contain names')
  now=int(time.time());nonce=secrets.token_hex(32);expires=now+300
  with self.db() as db:
   db.execute('BEGIN IMMEDIATE');db.execute('DELETE FROM challenges WHERE expires<?',(now,))
   if db.execute('SELECT count(*) FROM challenges').fetchone()[0]>=5000:raise ValueError('Directory busy; retry later')
   row=db.execute('SELECT revision FROM profiles WHERE address=?',(p['address'],)).fetchone();revision=row[0] if row else 0
   msg=message(p,nonce,expires,revision)
   db.execute('INSERT INTO challenges VALUES(?,?,?,?,?,?)',(nonce,p['address'],json.dumps(p),msg,revision,expires))
  return {'nonce':nonce,'expires':expires,'revision':revision}
 def update(self,p):
  if set(p)!={'nonce','signature'} or not isinstance(p['nonce'],str) or not re.fullmatch('[0-9a-f]{64}',p['nonce']) or not isinstance(p['signature'],str) or len(p['signature'])>200:raise ValueError('Invalid signature request')
  with self.db() as db:
   row=db.execute('SELECT * FROM challenges WHERE nonce=?',(p['nonce'],)).fetchone()
   if not row or row['expires']<int(time.time()):raise ValueError('Expired or already used request')
  proof={'address':row['address'],'message':row['message'],'signature':p['signature']}
  if not self.verifier(proof):raise ValueError('A valid spend-key wallet signature is required')
  data=json.loads(row['payload'])
  with self.db() as db:
   db.execute('BEGIN IMMEDIATE')
   current=db.execute('SELECT revision FROM profiles WHERE address=?',(row['address'],)).fetchone();revision=current[0] if current else 0
   if revision!=row['revision'] or row['expires']<int(time.time()) or not db.execute('SELECT 1 FROM challenges WHERE nonce=?',(p['nonce'],)).fetchone():raise ValueError('Profile changed; review and sign a new request')
   mode=data['mode'];hidden=mode=='private'
   db.execute('INSERT OR REPLACE INTO profiles VALUES(?,?,?,?,?,?,?,?)',(row['address'],revision+1,mode,'' if hidden else data['name'],data['balance'] if mode=='balance' else None,data['height'] if mode=='balance' else None,int(time.time()),'Wallet-reported; address control verified, balance not independently verified'))
   db.execute('DELETE FROM challenges WHERE address=?',(row['address'],))
  return {'ok':True,'mode':data['mode'],'revision':revision+1}
def native_verify(proof):
 try:
  r=subprocess.run([os.environ['PROFILE_VERIFIER']],input=json.dumps(proof).encode(),stdout=subprocess.PIPE,stderr=subprocess.DEVNULL,timeout=5)
  return r.returncode==0 and r.stdout.strip()==b'true'
 except (OSError,subprocess.TimeoutExpired):return False
class Handler(http.server.BaseHTTPRequestHandler):
 def log_message(self,*args):pass # Do not retain searched addresses or disclosure requests in access logs.
 def reply(self,status,data):
  body=json.dumps(data).encode();self.send_response(status);self.send_header('Content-Type','application/json');self.send_header('Cache-Control','no-store');self.send_header('Content-Length',str(len(body)));self.end_headers();self.wfile.write(body)
 def handle_request(self):
  if not hmac.compare_digest(self.headers.get('X-Profile-Proxy',''),self.server.proxy_token):self.reply(403,{'error':'Forbidden'});return
  try:
   self.connection.settimeout(10);self.server.directory.rate(self.headers.get('X-Profile-Client','unknown'))
   u=urlsplit(self.path)
   if self.command=='GET' and u.path=='/':result=self.server.directory.search(parse_qs(u.query).get('q',[''])[0])
   elif self.command=='POST' and u.path in ('/challenge','/update'):
    n=int(self.headers.get('Content-Length','0'))
    if n<2 or n>4096:raise ValueError('Invalid request size')
    p=json.loads(self.rfile.read(n))
    if not isinstance(p,dict):raise ValueError('Object required')
    result=self.server.directory.challenge(p) if u.path=='/challenge' else self.server.directory.update(p)
   else:self.reply(404,{'error':'Not found'});return
   self.reply(200,result)
  except (ValueError,TypeError,KeyError) as e:
   print('Rejected profile request: '+str(e)[:120],flush=True)
   self.reply(400,{'error':'Request rejected. Check the fields, refresh and try again.'})
  except Exception:self.reply(503,{'error':'Directory temporarily unavailable'})
 do_GET=handle_request
 do_POST=handle_request
if __name__=='__main__':
 os.umask(0o077)
 server=http.server.HTTPServer(('127.0.0.1',18794),Handler)
 server.proxy_token=open(os.environ['PROFILE_PROXY_TOKEN_FILE']).read().strip()
 if len(server.proxy_token)<32:raise RuntimeError('Proxy token missing')
 server.directory=Directory(os.environ['PROFILE_DATABASE'],native_verify);server.serve_forever()
