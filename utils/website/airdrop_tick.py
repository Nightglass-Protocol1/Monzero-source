#!/usr/bin/env python3
"""Push the airdrop tick to https://monzero.org/airdrop/tick.php from monzero-eu (systemd timer,
every 15 minutes). The webspace has no cron, so this replaces the old airdrop-mining-check.php and
airdrop-topup.sh jobs.

  airdrop_tick.py --secret-file /etc/xmzairdrop/secret [--db /var/lib/xmzpool/pool.sqlite]

Reads the pool database read-only and sends, HMAC-signed:
  fee_atomic  total pool fee collected (the airdrop budget is never renewed above this)
  balances    {address: pending + paid} for every pool address, so the website can count a mining
              day only when an address's earnings actually grow
Everything sent is already public through https://pool.monzero.org/api/miner.
"""
import argparse, hashlib, hmac, json, secrets, sqlite3, sys, time, urllib.error, urllib.request
from pathlib import Path


def read_pool(db_path):
    con = sqlite3.connect(f"file:{db_path}?mode=ro", uri=True, timeout=30)
    try:
        fee = con.execute("SELECT COALESCE(SUM(amount), 0) FROM pool_fee").fetchone()[0]
        balances = {a: int(p) + int(d) for a, p, d in con.execute("SELECT address, pending, paid FROM balances")}
    finally:
        con.close()
    return {"fee_atomic": int(fee), "balances": balances}


def sign(secret, body, ts=None, nonce=None):
    ts = ts or str(int(time.time()))
    nonce = nonce or secrets.token_hex(16)
    sig = hmac.new(secret.encode(), f"{ts}\n{nonce}\n".encode() + body, hashlib.sha256).hexdigest()
    return {"X-Airdrop-Timestamp": ts, "X-Airdrop-Nonce": nonce, "X-Airdrop-Signature": sig}


def push(site_url, secret, payload, timeout=60):
    body = json.dumps(payload, separators=(",", ":")).encode()
    req = urllib.request.Request(site_url.rstrip("/") + "/airdrop/tick.php", data=body, method="POST",
                                 headers={"Content-Type": "application/json", "User-Agent": "monzero-airdrop-tick/1", **sign(secret, body)})
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            return resp.status, json.loads(resp.read(65536) or b"{}")
    except urllib.error.HTTPError as e:
        try:
            return e.code, json.loads(e.read(65536) or b"{}")
        except json.JSONDecodeError:
            return e.code, {}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--secret-file", required=True)
    ap.add_argument("--db", default="/var/lib/xmzpool/pool.sqlite")
    ap.add_argument("--site-url", default="https://monzero.org")
    args = ap.parse_args()
    secret = Path(args.secret_file).read_text(encoding="utf-8").strip()
    payload = read_pool(args.db)
    code, reply = push(args.site_url, secret, payload)
    if code != 200:
        sys.exit(f"airdrop tick refused: HTTP {code} {reply}")
    print(f"airdrop tick ok: {len(payload['balances'])} pool addresses, checked {reply.get('checked')}, "
          f"newly credited {reply.get('newly_credited')}, budget {reply.get('pool_xmz')} XMZ")


if __name__ == "__main__":
    main()
