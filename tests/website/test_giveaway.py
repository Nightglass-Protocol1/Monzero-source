"""Telegram giveaway: link -> address claims the first N places, each linked to the Telegram ID,
@handle and payout address; membership and a public @handle required; one claim per account,
handle and address; the last place posts once to the group; the export CSV feeds airdrop_payout.py.
Run: python3 tests/website/test_giveaway.py   (needs php)"""
import csv, io, json, os, subprocess, sys, tempfile, threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

root = Path(__file__).resolve().parents[2]; site = root / 'website'
sys.path.insert(0, str(root / 'utils/website'))
import airdrop_payout  # noqa: E402

ADDR = ['FWPbFrwvRqpWTMDJAAeT3wAGsCUvERHyEAZJySDBDTKJJxr4q9gNStxSZghEAmtgvd49oySaDP9WGV1KVDeDXFBfM748KwK',
        'Fj4gs1bC1Wf82NWMYCm7gaifFntWiVDz83ZYNEVtwyUmSWxPAAM8Bjy4XwbK2k91Jq5VG3ndJLHrDhs2SoYPryYEBwWxL9J',
        'FQLhxULkbyx4886T7196doS9GPMzexD9gXpsZJDwVjeRVdFCSoHnv7KPbBeGpzJBzHRCAs9UxqeoyFQMYbqSWYTfJJCEnHf']
NONMEMBERS = {666}
SENT = []   # messages the bot sent through the fake Telegram API (sendMessage)


class FakeTelegram(BaseHTTPRequestHandler):
    def do_POST(self):
        body = json.loads(self.rfile.read(int(self.headers['Content-Length'])))
        method = self.path.rsplit('/', 1)[-1]
        if method == 'getChatMember':
            out = {'ok': True, 'result': {'status': 'left' if body['user_id'] in NONMEMBERS else 'member'}}
        else:
            SENT.append((method, body)); out = {'ok': True, 'result': {'message_id': 1}}
        data = json.dumps(out).encode()
        self.send_response(200); self.send_header('Content-Length', str(len(data))); self.end_headers(); self.wfile.write(data)
    def log_message(self, *a): pass


srv = ThreadingHTTPServer(('127.0.0.1', 0), FakeTelegram); threading.Thread(target=srv.serve_forever, daemon=True).start()
private = Path(tempfile.mkdtemp())
(private / 'telegram-config.php').write_text(
    "<?php return ['bot_token' => '12345:" + 'a' * 35 + "', 'webhook_secret' => '" + 's' * 40 + "', 'bot_username' => 'MonzeroTestBot',"
    f" 'api' => 'http://127.0.0.1:{srv.server_port}', 'feed_chat' => '@MonzeroOfficial'];\n")
ENV = {**os.environ, 'MONZERO_PRIVATE_DIR': str(private)}


def config(**kw):
    c = {'enabled': True, 'code': 'abc123def456', 'slots': 2, 'amount_xmz': 500, 'title': 'Test giveaway', 'opens_at': 0, 'closes_at': 0}
    c.update(kw)
    (private / 'giveaway-config.php').write_text('<?php return ' + json.dumps(c).replace('{', '[').replace('}', ']').replace('":', '" =>') + ';\n')


def msg(user, text, username='user%d'):
    frm = {'id': user, 'first_name': 'U'}
    if username: frm['username'] = username % user if '%' in username else username
    upd = {'message': {'chat': {'id': user, 'type': 'private'}, 'from': frm, 'text': text}}
    r = subprocess.run(['php', '-d', 'opcache.enable=0', '-r', f'require "{site}/lib/telegram.php"; echo json_encode(tgHandle(json_decode(stream_get_contents(STDIN), true)));'],
                       input=json.dumps(upd), env=ENV, capture_output=True, text=True)
    assert r.returncode == 0 and not r.stderr.strip(), r.stderr
    return json.loads(r.stdout) or ''


def php(code):
    r = subprocess.run(['php', '-d', 'opcache.enable=0', '-r', f'require "{site}/lib/giveaway.php"; {code}'], env=ENV, capture_output=True, text=True)
    assert r.returncode == 0, r.stderr
    return r.stdout


LINK = '/start gw-abc123def456'

# Not running: no config, then disabled
assert 'not valid' in msg(1, LINK) or 'not running' in msg(1, LINK)
config(enabled=False)
assert 'not running' in msg(1, LINK)
config(opens_at=4102444800)
assert 'opens at' in msg(1, LINK)
config()
assert 'not valid' in msg(1, '/start gw-wrongcode99')

# Requirements: public @handle and group membership
assert '@handle' in msg(1, LINK, username=None)
assert 'Join https://t.me/MonzeroOfficial' in msg(666, LINK)
# A plain address without opening the link first is not a claim
assert 'Send /help' in msg(5, ADDR[0])

# User 1 claims place 1, linked to @user1 and ADDR[0]
r = msg(1, LINK); assert '2 places left' in r and '@user1' in r, r
assert 'checksum' in msg(1, ADDR[0][:-1] + ('a' if ADDR[0][-1] != 'a' else 'b'))
r = msg(1, ADDR[0]); assert 'Place 1 of 2' in r and '@user1' in r, r
assert 'Place 1 of 2' in msg(1, LINK), 'opening the link again shows the claim'
# Same address from another account; same handle (changed case) from another account
msg(2, LINK); assert 'already been used' in msg(2, ADDR[0])
msg(3, LINK, username='USER1'); assert 'already claimed' in msg(3, ADDR[1], username='USER1')
assert not [s for s in SENT if s[0] == 'sendMessage'], 'no group post yet'
# User 2 takes the last place with /claim; the group hears once
r = msg(2, '/claim ' + ADDR[1]); assert 'Place 2 of 2' in r, r
posts = [s for s in SENT if s[0] == 'sendMessage']
assert len(posts) == 1 and 'all 2 places are claimed' in posts[0][1]['text'] and posts[0][1]['chat_id'] == '@MonzeroOfficial', posts
# Full: new people are told, a stale session cannot squeeze in
assert 'All 2 places have been claimed' in msg(4, LINK)
assert msg(3, ADDR[2], username='other3').startswith(('Sorry', 'All 2'))
assert php('echo gwClaimed("abc123def456");') == '2'

# Export: handles and IDs linked, no @ at the start of a cell, and airdrop_payout.py accepts it
out = php('echo gwExportCsv("abc123def456");')
rows = list(csv.DictReader(io.StringIO(out)))
assert [(r['slot'], r['telegram_username'], r['telegram_id'], r['address'], r['atomic']) for r in rows] == [
    ('1', 'user1', '1', ADDR[0], str(500 * 10**11)), ('2', 'user2', '2', ADDR[1], str(500 * 10**11))], rows
assert not any(cell.startswith(('@', '=', '+', '-')) for line in out.splitlines()[1:] for cell in line.split(','))
p = private / 'gw.csv'; p.write_text(out)
assert airdrop_payout.load_csv(p) == {ADDR[0]: 500 * 10**11, ADDR[1]: 500 * 10**11}

# Address window: a session older than 30 minutes is refused
config(code='second000001', slots=5)
msg(7, '/start gw-second000001')
php('gwDb()->exec("UPDATE sessions SET started_at = started_at - 1801");')
assert 'Send /help' in msg(7, ADDR[2]) or 'ran out' in msg(7, '/claim ' + ADDR[2])
# Existing bot commands still work
assert 'Monzero pool alerts' in msg(8, '/help')
print('giveaway: all checks passed')
srv.shutdown()
