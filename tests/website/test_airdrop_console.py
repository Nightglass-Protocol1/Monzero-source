"""Airdrop owner console (/admin/?view=airdrop): add moderators (generated or owner-set secrets,
shown once, never logged), moderator sign-in with them, no reviewing your own entry, warning
flags for approving your own referrals, owner overrides, suspend (signs out at once), reset,
remove (typed confirmation), and moderator sign-in throttling."""
import json,os,re,socket,subprocess,sys,tempfile,time,html
from pathlib import Path
import requests
root=Path(__file__).resolve().parents[2];site=root/'website';PASSWORD='correct horse battery staple'
sys.path.insert(0,str(Path(__file__).resolve().parent));from admin_mfa import enroll
ADDR=['FWPbFrwvRqpWTMDJAAeT3wAGsCUvERHyEAZJySDBDTKJJxr4q9gNStxSZghEAmtgvd49oySaDP9WGV1KVDeDXFBfM748KwK',
 'Fj4gs1bC1Wf82NWMYCm7gaifFntWiVDz83ZYNEVtwyUmSWxPAAM8Bjy4XwbK2k91Jq5VG3ndJLHrDhs2SoYPryYEBwWxL9J',
 'FQLhxULkbyx4886T7196doS9GPMzexD9gXpsZJDwVjeRVdFCSoHnv7KPbBeGpzJBzHRCAs9UxqeoyFQMYbqSWYTfJJCEnHf']

with tempfile.TemporaryDirectory() as tmp:
 tmp=Path(tmp);private=tmp/'private';private.mkdir();mail=tmp/'mail.jsonl'
 (private/'airdrop-config.php').write_text(f"<?php return ['enabled'=>true,'pool_xmz'=>8000,'ends_at'=>{int(time.time())+30*86400},'max_per_ip'=>0,'admin_reward_xmz'=>0.25,'pool_stats'=>'http://127.0.0.1:1/x'];\n")
 with socket.socket() as s:s.bind(('127.0.0.1',0));port=s.getsockname()[1]
 base=f'http://127.0.0.1:{port}'
 env={**os.environ,'MONZERO_PRIVATE_DIR':str(private),'MONZERO_ADMIN_DB':str(private/'admin.sqlite'),'MONZERO_ADMIN_MAIL_LOG':str(mail),'MONZERO_ADMIN_BASE_URL':base,'MONZERO_BOARD_DB':str(private/'board.sqlite'),'MONZERO_FAUCET_DB':str(private/'faucet.sqlite')}
 assert subprocess.run(['php',str(site/'admin/bootstrap.php'),'security@monzero.org'],env=env,capture_output=True).returncode==0
 server=subprocess.Popen(['php','-d','opcache.enable=0','-S',f'127.0.0.1:{port}','-t',str(site)],env=env,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
 try:
  for _ in range(100):
   try:
    with socket.create_connection(('127.0.0.1',port),timeout=.2):break
   except OSError:time.sleep(.1)
  token=re.search(r'\?setup=([A-Za-z0-9_-]{43})',json.loads(mail.read_text().splitlines()[-1])['body']).group(1)
  owner=requests.Session();assert owner.post(base+'/admin/?do=setup',data={'token':token,'password':PASSWORD,'confirm':PASSWORD},allow_redirects=False).status_code==303;enroll(owner,base)
  ov=owner.get(base+'/admin/').text;assert 'Manage moderators' in ov and '/admin/?view=airdrop' in ov
  csrf=lambda:re.search(r'name="csrf" value="([^"]+)"',owner.get(base+'/admin/?view=airdrop&tab=moderators').text).group(1)
  A=base+'/admin/?do='
  page=owner.get(base+'/admin/?view=airdrop').text;assert 'Moderator scoreboard' in page and 'No moderators yet' in page

  # Add: validation, then success with generated secrets shown once.
  r=owner.post(A+'airdrop-mod-add',data={'csrf':csrf(),'name':'Moon Crypto','address':ADDR[0][:-1]+'x','telegram':'@kevin_crypto'});assert r.status_code==400 and 'XMZ address' in r.text
  r=owner.post(A+'airdrop-mod-add',data={'csrf':csrf(),'name':'Moon Crypto','address':ADDR[0],'telegram':'@kevin_crypto','password':'short','pin':'1'});assert r.status_code==400 and 'Password' in r.text
  r=owner.post(A+'airdrop-mod-add',data={'csrf':csrf(),'name':'Moon Crypto','address':ADDR[0],'telegram':'@kevin_crypto','note':'applied via Telegram'});assert r.status_code==200,r.text[:500]
  pre=html.unescape(re.search(r'<pre id="adr-cred-text">(.*?)</pre>',r.text,re.S).group(1))
  pw=re.search(r'Password: (\S+)',pre).group(1);pin=re.search(r'PIN: (\d+)',pre).group(1)
  assert re.fullmatch(r'[a-z2-9]{4}(-[a-z2-9]{4}){3}',pw) and len(pin)==6 and ADDR[0] in pre and '@kevin_crypto' in pre
  assert pw not in owner.get(base+'/admin/?view=airdrop&tab=moderators').text  # shown once only
  r=owner.post(A+'airdrop-mod-add',data={'csrf':csrf(),'name':'moon crypto','address':ADDR[1],'telegram':'@other_mod'});assert r.status_code==400 and 'already exists' in r.text
  r=owner.post(A+'airdrop-mod-add',data={'csrf':csrf(),'name':'Second','address':ADDR[2],'telegram':'@second_mod','password':'my-own-pass-123','pin':'080015'});assert r.status_code==200
  audit=owner.get(base+'/admin/?view=audit').text;assert 'airdrop.moderator_added' in audit and pw not in audit and 'my-own-pass-123' not in audit

  # Moderator signs in with those details.
  def mod_login(pw_,pin_,addr=ADDR[0],tg='kevin_crypto'):
   s=requests.Session();s.post(base+'/airdrop/admin.php',data={'action':'login','address':addr,'telegram':tg,'password':pw_,'pin':pin_},allow_redirects=False);return s
  moon=mod_login(pw,pin);p=moon.get(base+'/airdrop/admin.php').text;assert 'Signed in as <strong>Moon Crypto</strong>' in p
  assert 'Signed in as <strong>Second</strong>' in mod_login('my-own-pass-123','080015',ADDR[2],'@second_mod').get(base+'/airdrop/admin.php').text

  # Participants: the moderator's own entry, and someone they referred.
  API=base+'/airdrop/api.php'
  own=requests.post(API+'?action=register',json={'address':ADDR[0]}).json();ref=own['ref_code']
  fr=requests.post(API+'?action=register',json={'address':ADDR[1],'ref':ref}).json()
  for t in (own['token'],fr['token']):assert requests.post(API+'?action=submit',json={'token':t,'task':'share_x','detail':'https://x.com/a/status/1'}).status_code==200
  p=moon.get(base+'/airdrop/admin.php').text;mcsrf=re.search(r'name="csrf" value="([^"]+)"',p).group(1)
  ids=[int(x) for x in re.findall(r'name="id" value="(\d+)"><input type="hidden" name="decision" value="approve"',p)];assert len(ids)==2
  r=moon.post(base+'/airdrop/admin.php',data={'csrf':mcsrf,'id':ids[0],'decision':'approve'},allow_redirects=False);assert 'own=1' in r.headers['Location']
  assert requests.post(API+'?action=admin.review',json={'id':ids[0],'decision':'approve'}).status_code==401
  moon.post(base+'/airdrop/admin.php',data={'csrf':mcsrf,'id':ids[1],'decision':'approve'})
  act=owner.get(base+'/admin/?view=airdrop&tab=activity').text
  assert 'their referral' in act and 'Moon Crypto' in act and act.count('<tr class="flag">')==1
  mods=owner.get(base+'/admin/?view=airdrop&tab=moderators').text;assert 'approved own referrals ×1' in mods and 'not joined' in mods  # Second has not joined

  # Owner overrides the flagged approval.
  r=owner.post(A+'airdrop-override',data={'csrf':csrf(),'submission':ids[1],'to':'rejected','mod':'Moon Crypto'},allow_redirects=False);assert r.status_code==303
  act=owner.get(base+'/admin/?view=airdrop&tab=activity&status=rejected').text;assert 'console' in act and '>rejected<' in act
  over=owner.get(base+'/admin/?view=airdrop').text;assert 'changed #%d'%ids[1] in over
  assert owner.post(A+'airdrop-override',data={'csrf':csrf(),'submission':ids[1],'to':'rejected'}).status_code==409

  # Suspend: signed out immediately; reactivate works.
  mid=re.search(r'name="id" value="(\d+)"><button type="submit" class="secondary">Suspend',owner.get(base+'/admin/?view=airdrop&tab=moderators').text).group(1)
  assert owner.post(A+'airdrop-mod-suspend',data={'csrf':csrf(),'id':mid},allow_redirects=False).status_code==303
  assert 'Moderator sign in' in moon.get(base+'/airdrop/admin.php').text
  assert 'Moderator sign in' in mod_login(pw,pin).get(base+'/airdrop/admin.php').text
  owner.post(A+'airdrop-mod-activate',data={'csrf':csrf(),'id':mid})
  assert 'Signed in as' in mod_login(pw,pin).get(base+'/airdrop/admin.php').text

  # Reset: old details stop working, new ones work.
  r=owner.post(A+'airdrop-mod-reset',data={'csrf':csrf(),'id':mid});pre=html.unescape(re.search(r'<pre id="adr-cred-text">(.*?)</pre>',r.text,re.S).group(1))
  pw2=re.search(r'Password: (\S+)',pre).group(1);pin2=re.search(r'PIN: (\d+)',pre).group(1)
  assert 'Moderator sign in' in mod_login(pw,pin).get(base+'/airdrop/admin.php').text
  assert 'Signed in as' in mod_login(pw2,pin2).get(base+'/airdrop/admin.php').text

  # Remove needs the typed name.
  assert owner.post(A+'airdrop-mod-remove',data={'csrf':csrf(),'id':mid,'confirm':'moon'}).status_code==400
  assert owner.post(A+'airdrop-mod-remove',data={'csrf':csrf(),'id':mid,'confirm':'Moon Crypto'},allow_redirects=False).status_code==303
  assert 'Moon Crypto' not in owner.get(base+'/admin/?view=airdrop&tab=moderators').text.split('Add a moderator')[0].split('<h2>Moderators</h2>')[1]

  # Sign-in throttle: 10 failures in 15 minutes locks out even the right details.
  for _ in range(10):mod_login('wrong-password','000000',ADDR[2],'second_mod')
  s=requests.Session();r=s.post(base+'/airdrop/admin.php',data={'action':'login','address':ADDR[2],'telegram':'second_mod','password':'my-own-pass-123','pin':'080015'})
  assert 'Too many failed sign-ins' in r.text

  # Participants: list (default = joined without a referral link), detail, adjust, revoke, disqualify.
  import sqlite3
  db=sqlite3.connect(private/'airdrop.sqlite')
  pid=lambda a:db.execute('SELECT id FROM participants WHERE address=?',(a,)).fetchone()[0]
  short=lambda a:a[:8]+'…'+a[-6:]
  lst=owner.get(base+'/admin/?view=airdrop&tab=participants').text
  assert 'Joined without a referral link' in lst and short(ADDR[0]) in lst and short(ADDR[1]) not in lst
  assert short(ADDR[1]) in owner.get(base+'/admin/?view=airdrop&tab=participants&f=referred').text
  assert 'on same network' in owner.get(base+'/admin/?view=airdrop&tab=participants&f=flagged').text  # both registered from 127.0.0.1
  assert short(ADDR[0]) in owner.get(base+'/admin/?view=airdrop&tab=participants&f=all&q='+ADDR[0][5:15]).text
  d=owner.get(base+f'/admin/?view=airdrop&tab=participant&id={pid(ADDR[0])}').text
  assert 'joined <strong>without a referral link</strong>' in d and ADDR[0] in d and 'People they referred (1)' in d
  status=lambda t:requests.get(API,params={'action':'status','token':t}).json()['status']
  before=status(own['token'])['earned']
  assert owner.post(A+'airdrop-p-adjust',data={'csrf':csrf(),'pid':pid(ADDR[0]),'xmz':'abc','reason':'x'}).status_code==400
  assert owner.post(A+'airdrop-p-adjust',data={'csrf':csrf(),'pid':pid(ADDR[0]),'xmz':'2','reason':''}).status_code==400
  assert owner.post(A+'airdrop-p-adjust',data={'csrf':csrf(),'pid':pid(ADDR[0]),'xmz':'+2','reason':'great forum guide'},allow_redirects=False).status_code==303
  st=status(own['token']);assert st['earned']==before+2 and st['adjustment']==2,st
  assert owner.post(A+'airdrop-p-adjust',data={'csrf':csrf(),'pid':pid(ADDR[0]),'xmz':'-100','reason':'x'}).status_code==400  # beyond the cap
  assert owner.post(A+'airdrop-p-adjust',data={'csrf':csrf(),'pid':pid(ADDR[0]),'xmz':'-25','reason':'take it all'},allow_redirects=False).status_code==303
  assert status(own['token'])['earned']==0  # never below zero
  # Revoke an automatic task.
  db.execute("INSERT INTO progress VALUES (?,?,?,?)",(pid(ADDR[1]),'telegram',100,0));db.execute('UPDATE participants SET telegram_id=42 WHERE id=?',(pid(ADDR[1]),));db.commit()
  assert owner.post(A+'airdrop-p-revoke',data={'csrf':csrf(),'pid':pid(ADDR[1]),'task':'mining','reason':'x'}).status_code==400
  assert owner.post(A+'airdrop-p-revoke',data={'csrf':csrf(),'pid':pid(ADDR[1]),'task':'telegram','reason':'left at once'},allow_redirects=False).status_code==303
  assert db.execute("SELECT COUNT(*) FROM progress WHERE participant_id=? AND task='telegram'",(pid(ADDR[1]),)).fetchone()[0]==0
  assert db.execute('SELECT telegram_id FROM participants WHERE id=?',(pid(ADDR[1]),)).fetchone()[0] is None
  # Disqualify: earns nothing, cannot submit, leaves the leaderboard; reinstate restores.
  db.execute("INSERT INTO progress VALUES (?,?,?,?)",(pid(ADDR[1]),'mining',600,0));db.commit()
  fr_earned=status(fr['token'])['earned'];assert fr_earned>0
  assert owner.post(A+'airdrop-p-disqualify',data={'csrf':csrf(),'pid':pid(ADDR[1]),'reason':''}).status_code==400
  assert owner.post(A+'airdrop-p-disqualify',data={'csrf':csrf(),'pid':pid(ADDR[1]),'reason':'duplicate entries'},allow_redirects=False).status_code==303
  st=status(fr['token']);assert st['disqualified'] and st['earned']==0
  assert 'disqualified' in requests.post(API+'?action=submit',json={'token':fr['token'],'task':'forum_post','detail':'someone'}).json()['error']
  assert short(ADDR[1]) not in json.dumps(requests.get(API+'?action=leaderboard').json())
  assert short(ADDR[1]) in owner.get(base+'/admin/?view=airdrop&tab=participants&f=disqualified').text
  owner.post(A+'airdrop-p-reinstate',data={'csrf':csrf(),'pid':pid(ADDR[1])})
  assert status(fr['token'])['earned']==fr_earned and not status(fr['token'])['disqualified']
  d=owner.get(base+f'/admin/?view=airdrop&tab=participant&id={pid(ADDR[1])}').text
  assert 'reinstated' in d and 'disqualified' in d and 'revoked telegram' in d and 'left at once' in d
  audit=owner.get(base+'/admin/?view=audit').text;assert 'airdrop.participant_adjust' in audit and 'airdrop.participant_disqualify' in audit

  # CSRF is required on console actions.
  assert owner.post(A+'airdrop-mod-add',data={'csrf':'x','name':'Third','address':ADDR[1],'telegram':'@third_mod'}).status_code==403
  print('airdrop console: ok')
 finally:
  server.terminate();server.wait()
