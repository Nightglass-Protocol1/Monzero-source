"""Test helper: turn on the (mandatory) admin authenticator codes for a freshly set-up admin session."""
import base64,hashlib,hmac,re,struct,time

def totp(secret,offset=0):
 key=base64.b32decode(secret);counter=int(time.time())//30+offset
 h=hmac.new(key,struct.pack('>Q',counter),hashlib.sha1).digest();o=h[19]&15
 return f"{(struct.unpack('>I',h[o:o+4])[0]&0x7fffffff)%1000000:06d}"

def enroll(session,base):
 page=session.get(base+'/admin/?view=account').text
 secret=re.search(r'name="secret" value="([A-Z2-7]{32})"',page).group(1)
 csrf=re.search(r'name="csrf" value="([^"]+)"',page).group(1)
 r=session.post(base+'/admin/?do=totp-enable',data={'csrf':csrf,'secret':secret,'code':totp(secret)},allow_redirects=False)
 assert r.status_code==303,r.text[:300]
 return secret
