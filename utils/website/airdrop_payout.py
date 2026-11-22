#!/usr/bin/env python3
"""Pay the airdrop from the owner's wallet, safely and resumably.

1. Download the payout CSV from the moderator panel (/airdrop/admin.php -> "Export payout CSV").
   It is already capped to the budget (kind,address,atomic,xmz; 1 XMZ = 1e11 atomic).
2. Run monzero-wallet-rpc for the paying wallet on this machine, e.g.
     monzero-wallet-rpc --wallet-file airdrop --rpc-bind-port 18090 --rpc-login user:pass
3. Dry run (default) shows exactly what would be sent:
     python3 airdrop_payout.py --csv monzero-airdrop-payouts.csv --wallet-rpc http://127.0.0.1:18090 --rpc-login user:pass
4. Pay:  add --send  (asks you to type the total to confirm).

Safety:
- The ledger (default airdrop-payout-ledger.jsonl next to the CSV) records every batch before and
  after it is sent. Re-running pays only what is still owed (CSV amount minus what was already
  sent to that address), so a crash, a re-export after more approvals, or running twice never
  pays anyone twice.
- At most 15 destinations per transaction (16-output limit, one output is change).
- If the wallet rejects a batch, it is retried one address at a time; addresses it still rejects
  are reported and skipped.
- If a send's outcome is unknown (timeout, connection drop), the script stops and refuses to run
  until you check the wallet's outgoing transfers and record the result:
     --resolve BATCH_ID=sent:TXHASH   or   --resolve BATCH_ID=failed
"""
import argparse, csv, json, re, secrets, sys, time, urllib.error, urllib.parse, urllib.request
from pathlib import Path

ATOMIC = 10**11
ADDR_RE = re.compile(r"\A[1-9A-HJ-NP-Za-km-z]{90,110}\Z")
MAX_DESTS = 15


def xmz(atomic):
    return f"{atomic / ATOMIC:,.11f}".rstrip("0").rstrip(".")


class RpcError(Exception):
    """The wallet answered with an error: the transfer definitely did not happen."""


class Wallet:
    """monzero-wallet-rpc client. Digest login is done here: Python's own digest handler fails
    against the wallet RPC (seen with Python 3.14), and a 401 happens before the wallet runs
    anything, so answering the challenge can never send a transfer twice."""

    def __init__(self, url, login=None, timeout=180):
        self.url = url.rstrip("/") + ("" if url.rstrip("/").endswith("/json_rpc") else "/json_rpc")
        self.timeout = timeout
        self.user, _, self.pw = (login or "").partition(":")
        self.nc = 0

    def _digest(self, challenge, body_path="/json_rpc"):
        import hashlib, urllib.request as _u
        f = _u.parse_keqv_list(_u.parse_http_list(challenge.split(" ", 1)[1]))
        md5 = lambda x: hashlib.md5(x.encode()).hexdigest()
        self.nc += 1
        nc, cnonce = f"{self.nc:08x}", secrets.token_hex(8)
        ha1 = md5(f"{self.user}:{f['realm']}:{self.pw}")
        ha2 = md5(f"POST:{body_path}")
        if "auth" in [q.strip() for q in f.get("qop", "").split(",")]:
            resp = md5(f"{ha1}:{f['nonce']}:{nc}:{cnonce}:auth:{ha2}")
            extra = f', qop=auth, nc={nc}, cnonce="{cnonce}"'
        else:
            resp, extra = md5(f"{ha1}:{f['nonce']}:{ha2}"), ""
        op = f', opaque="{f["opaque"]}"' if "opaque" in f else ""
        return (f'Digest username="{self.user}", realm="{f["realm"]}", nonce="{f["nonce"]}", '
                f'uri="{body_path}", algorithm=MD5, response="{resp}"{op}{extra}')

    def call(self, method, params=None):
        # One HTTP connection for the challenge and the answer: the wallet RPC ties its digest
        # challenge to the connection it was sent on.
        import http.client
        u = urllib.parse.urlsplit(self.url)
        body = json.dumps({"jsonrpc": "2.0", "id": "0", "method": method, "params": params or {}}).encode()
        headers = {"Content-Type": "application/json", "Connection": "keep-alive"}
        conn = http.client.HTTPConnection(u.hostname, u.port or 80, timeout=self.timeout)
        try:   # network errors propagate (outcome unknown)
            conn.request("POST", u.path, body, headers)
            r = conn.getresponse()
            if r.status == 401 and self.user:
                challenges = [v for k, v in r.getheaders() if k.lower() == "www-authenticate" and v.lower().startswith("digest")]
                r.read()
                md5 = [c for c in challenges if "md5-sess" not in c.lower()] or challenges
                if not md5:
                    raise RpcError("wallet asked for a login it did not describe")
                conn.request("POST", u.path, body, {**headers, "Authorization": self._digest(md5[0], u.path)})
                r = conn.getresponse()
            data = r.read()
            if r.status == 401:
                raise RpcError("wallet RPC login refused (check --rpc-login)")
            if r.status != 200:
                raise urllib.error.HTTPError(self.url, r.status, r.reason, None, None)
        finally:
            conn.close()
        reply = json.loads(data)
        if "error" in reply:
            raise RpcError(f"{reply['error'].get('code')}: {reply['error'].get('message')}")
        return reply["result"]

def load_csv(path):
    owed = {}
    with open(path, newline="", encoding="utf-8") as f:
        rows = list(csv.DictReader(f))
    if not rows or not {"address", "atomic"} <= set(rows[0]):
        sys.exit("!! CSV must have address and atomic columns (export it from the moderator panel)")
    for n, r in enumerate(rows, 2):
        a, amt = r["address"].strip(), r["atomic"].strip()
        if not ADDR_RE.match(a):
            sys.exit(f"!! line {n}: not an XMZ address: {a[:20]}…")
        if not amt.isdigit() or int(amt) <= 0:
            sys.exit(f"!! line {n}: bad amount {amt!r}")
        owed[a] = owed.get(a, 0) + int(amt)
    return owed


def read_ledger(path):
    batches = {}
    if path.exists():
        for line in path.read_text(encoding="utf-8").splitlines():
            if line.strip():
                e = json.loads(line)
                batches.setdefault(e["batch"], {}).update(e)
    return batches


def append(path, entry):
    with open(path, "a", encoding="utf-8") as f:
        f.write(json.dumps({**entry, "at": int(time.time())}, separators=(",", ":")) + "\n")
        f.flush()


def paid_so_far(batches):
    paid = {}
    for b in batches.values():
        if b.get("state") == "sent":
            for a, amt in b["items"]:
                paid[a] = paid.get(a, 0) + amt
    return paid


def send_batch(wallet, ledger, items):
    """Send one transaction. Returns (sent_items, rejected_items). Exits on unknown outcome."""
    batch = secrets.token_hex(6)
    append(ledger, {"batch": batch, "state": "sending", "items": items})
    try:
        res = wallet.call("transfer", {"destinations": [{"address": a, "amount": amt} for a, amt in items],
                                       "account_index": 0, "priority": 0})
    except RpcError as e:
        append(ledger, {"batch": batch, "state": "failed", "error": str(e)})
        return [], [(a, amt, str(e)) for a, amt in items]
    except Exception as e:  # timeout / connection lost: it may or may not have gone out
        append(ledger, {"batch": batch, "state": "unknown", "error": repr(e)})
        sys.exit(f"!! outcome of batch {batch} is UNKNOWN ({e!r}). Check the wallet's outgoing transfers, then re-run with\n"
                 f"   --resolve {batch}=sent:TXHASH   or   --resolve {batch}=failed")
    append(ledger, {"batch": batch, "state": "sent", "tx_hash": res.get("tx_hash"), "fee": res.get("fee")})
    print(f"   sent {len(items)} payment(s), {xmz(sum(a for _, a in items))} XMZ, tx {res.get('tx_hash')} fee {xmz(int(res.get('fee') or 0))}")
    return items, []


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--csv", required=True)
    ap.add_argument("--wallet-rpc", default="http://127.0.0.1:18090")
    ap.add_argument("--rpc-login", help="user:pass for monzero-wallet-rpc --rpc-login")
    ap.add_argument("--ledger", help="default: airdrop-payout-ledger.jsonl next to the CSV")
    ap.add_argument("--send", action="store_true", help="actually pay (default is a dry run)")
    ap.add_argument("--yes", action="store_true", help="skip the typed confirmation (for tests)")
    ap.add_argument("--resolve", action="append", default=[], metavar="BATCH=sent:TXHASH|failed")
    args = ap.parse_args()

    ledger = Path(args.ledger) if args.ledger else Path(args.csv).resolve().with_name("airdrop-payout-ledger.jsonl")
    for r in args.resolve:
        m = re.fullmatch(r"([0-9a-f]{12})=(sent:([0-9a-f]{64})|failed)", r)
        if not m:
            sys.exit(f"!! bad --resolve {r!r}")
        if m.group(1) not in read_ledger(ledger):
            sys.exit(f"!! no batch {m.group(1)} in {ledger}")
        append(ledger, {"batch": m.group(1), "state": "sent" if m.group(3) else "failed", "tx_hash": m.group(3), "resolved": True})
        print(f"recorded batch {m.group(1)} as {'sent' if m.group(3) else 'failed'}")

    batches = read_ledger(ledger)
    open_batches = [k for k, b in batches.items() if b.get("state") in ("sending", "unknown")]
    if open_batches:
        sys.exit("!! unresolved batch(es) in the ledger: " + ", ".join(open_batches) +
                 "\n   Check the wallet's outgoing transfers and record each with --resolve BATCH=sent:TXHASH or BATCH=failed")

    owed = load_csv(args.csv)
    paid = paid_so_far(batches)
    due = [(a, owed[a] - paid.get(a, 0)) for a in owed if owed[a] - paid.get(a, 0) > 0]
    over = [a for a in paid if paid[a] > owed.get(a, 0)]
    total = sum(amt for _, amt in due)
    print(f"CSV: {len(owed)} address(es), {xmz(sum(owed.values()))} XMZ. Already paid: {xmz(sum(paid.values()))} XMZ.")
    if over:
        print(f"note: {len(over)} address(es) were already paid more than this CSV lists (the CSV may be older); they get nothing more.")
    print(f"Still due: {len(due)} address(es), {xmz(total)} XMZ in {-(-len(due) // MAX_DESTS)} transaction(s).")
    if not due:
        return

    wallet = Wallet(args.wallet_rpc, args.rpc_login)
    bal = wallet.call("get_balance", {"account_index": 0})
    unlocked = int(bal.get("unlocked_balance", 0))
    print(f"Wallet unlocked balance: {xmz(unlocked)} XMZ (fees are extra, about 0.0001-0.001 XMZ per transaction)")
    if not args.send:
        for a, amt in due[:20]:
            print(f"   would pay {xmz(amt):>16} XMZ -> {a[:12]}…{a[-6:]}")
        if len(due) > 20:
            print(f"   … and {len(due) - 20} more")
        print("Dry run only. Add --send to pay.")
        return
    if unlocked <= total:
        sys.exit("!! not enough unlocked balance to pay everything plus fees. Fund the wallet or wait for it to unlock.")
    if not args.yes:
        typed = input(f"Type the total to pay ({xmz(total)}) to confirm: ").strip().replace(",", "")
        if typed != xmz(total).replace(",", ""):
            sys.exit("not confirmed; nothing sent")

    rejected = []
    for i in range(0, len(due), MAX_DESTS):
        items = [list(x) for x in due[i:i + MAX_DESTS]]
        sent, bad = send_batch(wallet, ledger, items)
        if bad and len(items) > 1:
            print(f"   batch rejected ({bad[0][2]}); retrying one at a time")
            for it in items:
                _, b1 = send_batch(wallet, ledger, [it])
                rejected += b1
        else:
            rejected += bad
    if rejected:
        print(f"!! {len(rejected)} payment(s) were rejected by the wallet and NOT paid (re-run later to retry):")
        for a, amt, err in rejected:
            print(f"   {xmz(amt)} XMZ -> {a[:12]}…{a[-6:]}: {err}")
        sys.exit(1)
    print("All due payments sent.")


if __name__ == "__main__":
    main()
