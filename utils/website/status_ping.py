#!/usr/bin/env python3
"""Check in with https://monzero.org/api/status/ from a node, every 10 minutes (systemd timer).

  status_ping.py --node eu --secret-file /etc/xmzstatus/secret

The website records the check-in, re-evaluates every service check and emails the owner about
changes. Running it from two nodes means one silent node (or its timers) is still noticed.
"""
import argparse, hashlib, hmac, json, secrets, sys, time, urllib.error, urllib.request
from pathlib import Path


def ping(site_url, secret, node, timeout=60):
    body = json.dumps({"node": node}, separators=(",", ":")).encode()
    ts, nonce = str(int(time.time())), secrets.token_hex(16)
    signature = hmac.new(secret.encode(), f"{ts}\n{nonce}\n".encode() + body, hashlib.sha256).hexdigest()
    request = urllib.request.Request(site_url.rstrip("/") + "/api/status/", data=body, method="POST", headers={
        "Content-Type": "application/json", "X-Status-Timestamp": ts, "X-Status-Nonce": nonce,
        "X-Status-Signature": signature, "User-Agent": "monzero-status-ping/1"})
    try:
        with urllib.request.urlopen(request, timeout=timeout) as resp:
            return resp.status, json.loads(resp.read(65536) or b"{}")
    except urllib.error.HTTPError as e:
        try:
            return e.code, json.loads(e.read(65536) or b"{}")
        except json.JSONDecodeError:
            return e.code, {}


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--node", required=True, choices=["eu", "us"])
    parser.add_argument("--secret-file", required=True)
    parser.add_argument("--site-url", default="https://monzero.org")
    args = parser.parse_args()
    code, reply = ping(args.site_url, Path(args.secret_file).read_text(encoding="utf-8").strip(), args.node)
    if code != 200:
        sys.exit(f"status check-in refused: HTTP {code} {reply}")
    print(f"checked in; overall status {reply.get('overall')}")


if __name__ == "__main__":
    main()
