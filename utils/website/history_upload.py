#!/usr/bin/env python3
"""Upload network history exports (from network_history.py --output-directory) to the website.

  history_upload.py --directory /var/lib/xmznethistory/public --secret-file /etc/xmznethistory/secret

Sends the six range files as one gzip-compressed JSON body, signed with HMAC-SHA256 over
"timestamp\\nnonce\\nbody". The website keeps a file only if it is newer than the one it serves.
Standard library only.
"""
import argparse, gzip, hashlib, hmac, json, secrets, sys, time, urllib.error, urllib.request
from pathlib import Path

RANGES = ("24h", "7d", "30d", "90d", "1y", "all")


def bundle(directory):
    exports = {}
    for name in RANGES:
        path = Path(directory) / f"{name}.json"
        if path.is_file() and not path.is_symlink():
            exports[name] = json.loads(path.read_text(encoding="utf-8"))
    if not exports:
        raise ValueError(f"no exports in {directory}")
    return gzip.compress(json.dumps(exports, separators=(",", ":"), allow_nan=False).encode(), mtime=0)


def upload(site_url, secret, body, timeout=60):
    ts, nonce = str(int(time.time())), secrets.token_hex(16)
    signature = hmac.new(secret.encode(), f"{ts}\n{nonce}\n".encode() + body, hashlib.sha256).hexdigest()
    request = urllib.request.Request(site_url.rstrip("/") + "/api/network-history/", data=body, method="POST", headers={
        "Content-Type": "application/octet-stream", "X-History-Timestamp": ts, "X-History-Nonce": nonce,
        "X-History-Signature": signature, "User-Agent": "monzero-history-upload/1"})
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
    parser.add_argument("--directory", required=True)
    parser.add_argument("--secret-file", required=True)
    parser.add_argument("--site-url", default="https://monzero.org")
    args = parser.parse_args()
    secret = Path(args.secret_file).read_text(encoding="utf-8").strip()
    body = bundle(args.directory)
    code, reply = upload(args.site_url, secret, body)
    if code != 200:
        sys.exit(f"website refused the upload: HTTP {code} {reply}")
    print(f"uploaded {len(body)} bytes; updated: {', '.join(reply.get('written', [])) or 'nothing newer'}")


if __name__ == "__main__":
    main()
