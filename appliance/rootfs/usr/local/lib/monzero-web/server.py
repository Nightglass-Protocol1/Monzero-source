#!/usr/bin/python3 -I
"""Monzero box setup and status page (HTTPS, port 8443, your own network only).

Runs as the unprivileged user monzero-web. It never changes the system itself: it writes a
request file that the root service monzero-apply checks and carries out. No JavaScript, no
outside resources. First visit: the one-time setup code from the console, a password, the
roles. Afterwards: password login, status, roles, SSH, remote screen, update now.
"""
import hashlib
import hmac
import html
import http.server
import ipaddress
import json
import os
import secrets
import socketserver
import ssl
import subprocess
import time
import urllib.parse
import urllib.request

WEB = "/etc/monzero/web"
DATA = "/var/lib/monzero-web"
REQUESTS = f"{DATA}/requests"
ADMIN = f"{DATA}/admin.json"
ROLES = ("node", "relay", "wallet")
SESSION_SECS = 12 * 3600
sessions = {}           # token -> (expires, csrf)
failures = {}          # address -> [wrong answers, blocked until]


def is_setup():
    return os.path.exists(ADMIN)


def check_code(given):
    try:
        code = open(f"{WEB}/setup-code").read().strip()
    except OSError:
        return False
    given = "".join(c for c in given.upper() if c.isalnum())
    return len(code) == 12 and hmac.compare_digest(given.encode(), code.encode())


def hash_password(pw, salt=None):
    salt = salt or secrets.token_bytes(16)
    h = hashlib.scrypt(pw.encode(), salt=salt, n=2**15, r=8, p=1, maxmem=64 * 1024 * 1024, dklen=32)
    return {"salt": salt.hex(), "hash": h.hex(), "kdf": "scrypt-n15-r8-p1"}


def check_password(pw):
    try:
        a = json.load(open(ADMIN))
    except OSError:
        return False
    return hmac.compare_digest(hash_password(pw, bytes.fromhex(a["salt"]))["hash"], a["hash"])


def throttled(ip):
    """Slow down guessing, per address (so one device cannot lock the owner out): after 5
    wrong answers, wait 2^n seconds, up to an hour."""
    return time.time() < failures.get(ip, [0, 0.0])[1]


def failed(ip):
    if len(failures) > 1000:
        failures.clear()
    f = failures.setdefault(ip, [0, 0.0])
    f[0] += 1
    if f[0] >= 5:
        f[1] = time.time() + min(3600, 2 ** (f[0] - 4))


def request_change(kind, **fields):
    os.makedirs(REQUESTS, exist_ok=True)
    name = f"{int(time.time() * 1000)}-{secrets.token_hex(4)}.json"
    tmp = f"{REQUESTS}/.{name}"
    with open(tmp, "w") as f:
        json.dump({"kind": kind, **fields}, f)
    os.replace(tmp, f"{REQUESTS}/{name}")


def service(name):
    r = subprocess.run(["systemctl", "is-active", name], capture_output=True, text=True)
    return r.stdout.strip() or "unknown"


def node_info():
    try:
        req = urllib.request.Request("http://127.0.0.1:6175/get_info", data=b"{}", headers={"Content-Type": "application/json"})
        i = json.load(urllib.request.urlopen(req, timeout=3))
        return f"height {i['height']} of {i.get('target_height') or i['height']}, {'synced' if i.get('synchronized') else 'syncing'}, peers {i.get('outgoing_connections_count', 0)} out / {i.get('incoming_connections_count', 0)} in"
    except Exception:
        return "not answering"


def read_json(path, default):
    try:
        return json.load(open(path))
    except Exception:
        return default


PAGE = """<!doctype html><html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1"><title>Monzero Box</title><link rel="icon" href="/favicon.png">
<style>
:root{{--bg:#0f1115;--card:#181b22;--fg:#e8e8ea;--muted:#9aa0aa;--accent:#19d6e6;--bad:#ff5c5c;--ok:#4cc38a}}
body{{color-scheme:dark;accent-color:var(--accent);background:var(--bg);color:var(--fg);font:16px/1.5 system-ui,sans-serif;margin:0;padding:16px}}
main{{max-width:640px;margin:0 auto}} .card{{background:var(--card);border-radius:12px;padding:16px 20px;margin:16px 0}}
h1{{font-size:1.4rem;margin:8px 0 0}} .brand{{display:flex;flex-direction:column;align-items:center;gap:4px;margin:8px 0 4px}} .brand img{{width:min(340px,80vw);height:auto}} .brand span{{color:var(--muted);letter-spacing:.2em;font-size:.8rem;text-transform:uppercase}} h2{{font-size:1.1rem;margin-top:0}} label{{display:block;margin:10px 0 4px}}
input[type=text],input[type=password],textarea{{width:100%;box-sizing:border-box;padding:8px;border-radius:8px;border:1px solid var(--muted);background:var(--bg);color:var(--fg)}}
button{{background:var(--accent);color:#111;border:0;border-radius:8px;padding:8px 16px;font-weight:600;margin-top:12px;cursor:pointer}}
.muted{{color:var(--muted)}} .bad{{color:var(--bad)}} .ok{{color:var(--ok)}} code{{word-break:break-all}}
</style></head><body><main><div class="brand"><img src="/brand/wordmark.png" alt="Monzero"><span>Box</span></div>{body}</main></body></html>"""


def page(body):
    return PAGE.format(body=body).encode()


def role_boxes(selected):
    names = {"node": "XMZ node: helps the network; your wallet uses it",
             "relay": "DEX relay: carries signed offers for the DEX; holds no funds",
             "wallet": "Wallet + DEX app: on this box's screen (or the remote screen)"}
    # The wallet role only where the installed release has the wallet app (64-bit ARM: later)
    roles = [r for r in ROLES if r != "wallet" or os.path.exists("/opt/monzero/current/monzero-wallet-gui")]
    return "".join(f'<label><input type="checkbox" name="role" value="{r}"{" checked" if r in selected else ""}> {html.escape(names[r])}</label>' for r in roles)


class Handler(http.server.BaseHTTPRequestHandler):
    server_version = "monzero-box"
    sys_version = ""

    def log_message(self, fmt, *args):
        pass

    def send(self, status, body=b"", headers=()):
        self.send_response(status)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Security-Policy", "default-src 'none'; style-src 'unsafe-inline'; img-src 'self'; form-action 'self'; frame-ancestors 'none'; base-uri 'none'")
        self.send_header("X-Frame-Options", "DENY")
        self.send_header("Referrer-Policy", "same-origin")
        self.send_header("Cache-Control", "no-store")
        self.send_header("Strict-Transport-Security", "max-age=31536000")
        for k, v in headers:
            self.send_header(k, v)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def session(self):
        for part in self.headers.get("Cookie", "").split(";"):
            k, _, v = part.strip().partition("=")
            if k == "mzs" and v in sessions and sessions[v][0] > time.time():
                return v
        return None

    def form(self):
        n = int(self.headers.get("Content-Length", "0"))
        if n > 16384:
            raise ValueError("too large")
        return urllib.parse.parse_qs(self.rfile.read(n).decode(), keep_blank_values=True)

    def same_origin(self):
        # A browser form post carries Origin (this page sends Referrer-Policy: same-origin, so
        # browsers give the real origin here; with no-referrer they send "null"). A "null" origin
        # counts only if the browser itself marks the request same-origin (Sec-Fetch-Site).
        origin = self.headers.get("Origin")
        host = self.headers.get("Host", "")
        if origin is None:
            return True
        if origin == "null":
            return self.headers.get("Sec-Fetch-Site") == "same-origin"
        return urllib.parse.urlparse(origin).netloc == host

    BRAND = {"/brand/wordmark.png": "wordmark-600.png", "/favicon.png": "favicon-32.png", "/favicon.ico": "favicon-32.png"}

    def do_GET(self):
        if self.path in self.BRAND:   # the Monzero artwork, from the image itself
            try:
                data = open("/usr/share/monzero/branding/" + self.BRAND[self.path], "rb").read()
            except OSError:
                return self.send(404, page("<p>Not found.</p>"))
            self.send_response(200)
            self.send_header("Content-Type", "image/png")
            self.send_header("Content-Length", str(len(data)))
            self.send_header("Cache-Control", "max-age=86400")
            self.send_header("X-Content-Type-Options", "nosniff")
            self.end_headers()
            self.wfile.write(data)
            return
        if self.path not in ("/", "/index.html"):
            return self.send(404, page("<p>Not found.</p>"))
        if not is_setup():
            return self.send(200, page(self.setup_form()))
        s = self.session()
        if not s:
            return self.send(200, page(self.login_form()))
        return self.send(200, page(self.status(s)))

    def setup_form(self, error=""):
        return f"""<div class="card"><h2>Set up this box</h2>
<p class="muted">The setup code is shown on the box's screen or console (it is used once).</p>
{f'<p class="bad">{html.escape(error)}</p>' if error else ''}
<form method="post" action="/setup">
<label>Setup code</label><input type="text" name="code" autocomplete="off" required>
<label>New password for this page (12 characters or more)</label><input type="password" name="pw" minlength="12" required autocomplete="new-password">
<label>Password again</label><input type="password" name="pw2" minlength="12" required autocomplete="new-password">
<h2 style="margin-top:20px">What should it run?</h2>{role_boxes(("node", "relay"))}
<label><input type="checkbox" name="share" value="1" checked> Share my relay with the Monzero network through Tor (recommended: no router setup, your home IP stays hidden; it carries signed offers only, never funds)</label>
<label>SSH public key (optional; SSH stays off without one)</label><textarea name="sshkey" rows="3" placeholder="ssh-ed25519 AAAA... you@pc"></textarea>
<button type="submit">Set up</button></form></div>"""

    def login_form(self, error=""):
        return f"""<div class="card"><h2>Sign in</h2>{f'<p class="bad">{html.escape(error)}</p>' if error else ''}
<form method="post" action="/login"><label>Password</label><input type="password" name="pw" required autocomplete="current-password">
<button type="submit">Sign in</button></form></div>"""

    def status(self, s):
        csrf = sessions[s][1]
        cfg = read_json("/etc/monzero/appliance.json", {})
        upd = read_json("/var/lib/monzero-update/status.json", {})
        result = read_json(f"{DATA}/last-result.json", {})
        roles = cfg.get("roles", [])
        rows = "".join(f"<tr><td>{n}</td><td>{html.escape(service(u))}</td></tr>" for n, u in (("Node", "monzerod"), ("DEX relay", "monzero-dex-relay"), ("Wallet screen", "monzero-wallet-session"), ("Remote screen", "monzero-wallet-vnc"), ("SSH", "ssh")))
        inst = upd.get("installed", {})
        tok = f'<input type="hidden" name="csrf" value="{csrf}">'
        return f"""<div class="card"><h2>Status</h2><table>{rows}</table>
<p>Node: {html.escape(node_info())}</p>
<p class="muted">Installed: core {html.escape(str(inst.get('core')))}, relay {html.escape(str(inst.get('relay')))}.
Last update check: {html.escape(upd.get('result', 'not yet'))}</p>
{f'<p class="muted">Last change: {html.escape(result.get("result", ""))}</p>' if result else ''}</div>
<div class="card"><h2>Roles</h2><form method="post" action="/roles">{tok}{role_boxes(roles)}<button>Save</button></form></div>
<div class="card"><h2>Share the relay with the Monzero network</h2>
<p class="muted">Other relays and apps can then read the offers your relay carries (signed offers only: it never holds funds or keys).</p>
{self.public_relay_info()}
<form method="post" action="/public-relay">{tok}
<label><input type="radio" name="mode" value="onion"{' checked' if cfg.get('publicRelay', 'off') == 'onion' else ''}> Through Tor (recommended: no router setup, home IP hidden)</label>
<label><input type="radio" name="mode" value="port"{' checked' if cfg.get('publicRelay') == 'port' else ''}> Through a router port (UPnP, or forward TCP 8790 yourself)</label>
<label>Your public IPv4 address (only if your router has no UPnP)</label><input type="text" name="ip" value="{html.escape(cfg.get('publicRelayIp', ''))}" placeholder="leave empty to ask the router">
<label><input type="radio" name="mode" value="off"{' checked' if cfg.get('publicRelay', 'off') == 'off' else ''}> Not shared (only this box and your home network)</label>
<button>Save</button></form></div>
<div class="card"><h2>Remote screen for the wallet</h2>
<p class="muted">Shows the wallet's screen in your browser (HTTPS, your network only). Off unless you need it.</p>
{self.vnc_info(cfg)}
<form method="post" action="/vnc">{tok}<label><input type="checkbox" name="on" value="1"{' checked' if cfg.get('vnc') else ''}> On</label><button>Save</button></form></div>
<div class="card"><h2>SSH</h2><form method="post" action="/ssh">{tok}
<label>Public key (keys only, no passwords). Empty = SSH off.</label><textarea name="sshkey" rows="3">{html.escape(cfg.get('sshkey', ''))}</textarea><button>Save</button></form></div>
<div class="card"><h2>Updates</h2><p class="muted">Signed releases install by themselves once a day.</p>
<form method="post" action="/update">{tok}<button>Check now</button></form></div>
<div class="card"><h2>Password</h2><form method="post" action="/password">{tok}
<label>New password</label><input type="password" name="pw" minlength="12" required autocomplete="new-password">
<label>Again</label><input type="password" name="pw2" minlength="12" required autocomplete="new-password"><button>Change</button></form>
<form method="post" action="/logout">{tok}<button>Sign out</button></form></div>"""

    def public_relay_info(self):
        st = read_json(f"{DATA}/public-relay.json", {})
        if not st:
            return ""
        url = f'<br>Address: <code>{html.escape(st["url"])}</code>' if st.get("url") else ""
        return f'<p>{html.escape(st.get("message", ""))}{url}</p>'

    def vnc_info(self, cfg):
        if not cfg.get("vnc"):
            return ""
        try:
            pw = open(f"{DATA}/vnc-password").read().strip()
        except OSError:
            return '<p class="muted">Starting...</p>'
        host = html.escape(self.headers.get("Host", "").rsplit(":", 1)[0])
        return f'<p>Open <code>https://{host}:6080/vnc.html</code> and enter <code>{html.escape(pw)}</code> (changes each time it is turned on).</p>'

    def do_POST(self):
        try:
            f = self.form()
        except Exception:
            return self.send(400, page("<p>Bad request.</p>"))
        if not self.same_origin():
            return self.send(403, page("<p>Refused (other site).</p>"))
        get = lambda k: (f.get(k) or [""])[0]
        ip = self.client_address[0]
        if self.path == "/setup" and not is_setup():
            if throttled(ip):
                return self.send(429, page(self.setup_form("Too many wrong codes: wait and try again.")))
            if not check_code(get("code")):
                failed(ip)
                return self.send(200, page(self.setup_form("That setup code is not right.")))
            if len(get("pw")) < 12 or get("pw") != get("pw2"):
                return self.send(200, page(self.setup_form("The passwords differ or are shorter than 12 characters.")))
            roles = [r for r in f.get("role", []) if r in ROLES]
            json.dump(hash_password(get("pw")), open(ADMIN + ".tmp", "w"))
            os.chmod(ADMIN + ".tmp", 0o600)
            os.replace(ADMIN + ".tmp", ADMIN)
            failures.pop(ip, None)
            request_change("setup", roles=roles, sshkey=get("sshkey").strip(), share=get("share") == "1")
            return self.redirect_new_session()
        if self.path == "/login" and is_setup():
            if throttled(ip):
                return self.send(429, page(self.login_form("Too many wrong passwords: wait and try again.")))
            if not check_password(get("pw")):
                failed(ip)
                return self.send(200, page(self.login_form("Wrong password.")))
            failures.pop(ip, None)
            return self.redirect_new_session()
        s = self.session()
        if not s or not hmac.compare_digest(get("csrf"), sessions[s][1]):
            return self.send(403, page('<p>Please <a href="/">sign in</a> again.</p>'))
        if self.path == "/roles":
            request_change("roles", roles=[r for r in f.get("role", []) if r in ROLES])
        elif self.path == "/public-relay":
            request_change("public_relay", mode=get("mode"), ip=get("ip").strip())
        elif self.path == "/vnc":
            request_change("vnc", on=get("on") == "1")
        elif self.path == "/ssh":
            request_change("ssh", sshkey=get("sshkey").strip())
        elif self.path == "/update":
            request_change("update")
        elif self.path == "/password":
            if len(get("pw")) < 12 or get("pw") != get("pw2"):
                return self.send(200, page('<p class="bad">The passwords differ or are too short.</p><p><a href="/">Back</a></p>'))
            json.dump(hash_password(get("pw")), open(ADMIN + ".tmp", "w"))
            os.chmod(ADMIN + ".tmp", 0o600)
            os.replace(ADMIN + ".tmp", ADMIN)
            request_change("password")   # the remote screen password follows the page's
            sessions.clear()
        elif self.path == "/logout":
            sessions.pop(s, None)
        else:
            return self.send(404, page("<p>Not found.</p>"))
        self.send(303, b"", [("Location", "/")])

    def redirect_new_session(self):
        token, csrf = secrets.token_urlsafe(32), secrets.token_urlsafe(24)
        sessions[token] = (time.time() + SESSION_SECS, csrf)
        self.send(303, b"", [("Location", "/"), ("Set-Cookie", f"mzs={token}; Path=/; Secure; HttpOnly; SameSite=Strict; Max-Age={SESSION_SECS}")])


class Server(socketserver.ThreadingMixIn, http.server.HTTPServer):
    daemon_threads = True
    address_family = __import__("socket").AF_INET6

    def verify_request(self, request, client_address):
        # Your own network only: loopback, private and link-local addresses
        ip = ipaddress.ip_address(client_address[0])
        ip = ip.ipv4_mapped or ip
        return ip.is_private or ip.is_loopback or ip.is_link_local


def main():
    srv = Server(("::", 8443), Handler)
    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    ctx.minimum_version = ssl.TLSVersion.TLSv1_2
    ctx.load_cert_chain(f"{WEB}/tls.crt", f"{WEB}/tls.key")
    # Handshake in the request's own thread, so one slow client cannot hold up the others
    srv.socket = ctx.wrap_socket(srv.socket, server_side=True, do_handshake_on_connect=False)
    srv.serve_forever()


if __name__ == "__main__":
    main()
