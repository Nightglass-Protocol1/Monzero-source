#!/bin/bash
# Send XMZ from the pool wallet's own funds (the pool fee) to the faucet wallet, both on monzero-eu.
# Refuses unless the pool keeps enough unlocked XMZ to pay every miner what it owes (with 20%
# headroom), the amount is within the fee the pool has collected, and the faucet address read from
# the faucet wallet matches the known one. Run from the owner's PC:
#   bash ~/projects/Monzero-App/pool/xmzpool/deploy/fund-faucet.sh check 500
#   bash ~/projects/Monzero-App/pool/xmzpool/deploy/fund-faucet.sh send 500
# Afterwards, once it has 10 confirmations: bash .../utils/faucet/split-outputs.sh
set -euo pipefail
HOST=${HOST:-monzero-eu}
MODE=${1:-}; AMOUNT=${2:-}
case "$MODE" in check|send) ;; *) echo "usage: $0 check|send AMOUNT_XMZ"; exit 1;; esac
case "$AMOUNT" in ''|*[!0-9]*) echo "amount must be a whole number of XMZ"; exit 1;; esac
[ "$AMOUNT" -ge 1 ] && [ "$AMOUNT" -le 2000 ] || { echo "amount must be 1 to 2000 XMZ"; exit 1; }

ssh -o BatchMode=yes "$HOST" sudo MODE="$MODE" AMOUNT="$AMOUNT" bash -s <<'REMOTE'
set -euo pipefail
python3 - <<'PY'
import json, os, sqlite3, subprocess, sys, time
sys.path.insert(0, "/opt/xmzpool")
import xmzpool as xp
EXPECTED = "FPaga4AkLTFA17YbVaXyQvjUtkCmcanXeFM3Wk2BHKxh3kJQFeDgFDxd4hyQrNEcUy4bXGQxnun24KD7gjMToKZj7qZRD2K"
mode, amount = os.environ["MODE"], int(os.environ["AMOUNT"])
X = xp.ATOMIC
cfg = xp.load_config("/etc/xmzpool/pool.json")
auth = tuple(cfg["wallet_rpc_login"].split(":", 1)) if cfg.get("wallet_rpc_login") else None
pool = cfg["wallet_rpc"].rstrip("/") + "/json_rpc"
bal = xp.rpc(pool, "get_balance", {"account_index": 0}, 30, auth)
db = sqlite3.connect("file:" + cfg["database"] + "?mode=ro", uri=True)
owed = db.execute("SELECT COALESCE(SUM(pending),0) FROM balances").fetchone()[0]
fee = db.execute("SELECT COALESCE(SUM(amount),0) FROM pool_fee").fetchone()[0]
# Faucet address straight from the faucet wallet (its own RPC on this host).
login = "faucet:" + open("/etc/xmzfaucet/rpc.pass").read().strip()
out = subprocess.run(["curl", "-s", "-m", "30", "--digest", "-u", login, "http://127.0.0.1:6192/json_rpc", "-d",
    json.dumps({"jsonrpc": "2.0", "id": 0, "method": "get_address", "params": {"account_index": 0}})], capture_output=True, text=True).stdout
faucet = json.loads(out)["result"]["address"]
fb = json.loads(subprocess.run(["curl", "-s", "-m", "30", "--digest", "-u", login, "http://127.0.0.1:6192/json_rpc", "-d",
    json.dumps({"jsonrpc": "2.0", "id": 0, "method": "get_balance", "params": {"account_index": 0}})], capture_output=True, text=True).stdout)["result"]
unlocked = bal["unlocked_balance"]; spare = unlocked - owed; want = amount * X
print("pool wallet:   %.2f XMZ unlocked of %.2f" % (unlocked / X, bal["balance"] / X))
print("owed to miners: %.2f XMZ   spare after that: %.2f XMZ" % (owed / X, spare / X))
print("pool fee collected so far: %.2f XMZ (the airdrop budget is also paid from this)" % (fee / X))
print("faucet wallet: %s…%s  balance %.2f XMZ (%.2f unlocked)" % (faucet[:8], faucet[-6:], fb["balance"] / X, fb["unlocked_balance"] / X))
problems = []
if faucet != EXPECTED: problems.append("the faucet wallet's address is not the known faucet address")
if spare < want * 1.2 + 5 * X: problems.append("not enough spare: miners must stay fully covered (needs %.2f spare)" % ((want * 1.2 + 5 * X) / X))
if want > fee: problems.append("more than the pool fee collected so far")
if problems:
    print("REFUSED:"); [print("  - " + p) for p in problems]; sys.exit(1)
print("OK: %d XMZ can go from the pool wallet to the faucet; miners stay covered." % amount)
if mode != "send":
    print("check only: nothing sent. Run with 'send' to transfer."); sys.exit(0)
res = xp.rpc(pool, "transfer", {"destinations": [{"address": faucet, "amount": want}], "account_index": 0, "priority": 0}, 180, auth)
line = "%s sent %d XMZ pool->faucet tx %s fee %.6f" % (time.strftime("%Y-%m-%d %H:%M:%S UTC", time.gmtime()), amount, res.get("tx_hash"), res.get("fee", 0) / X)
open("/var/lib/xmzpool/faucet-funding.log", "a").write(line + "\n")
print("SENT: " + line)
print("Next: wait ~10 confirmations (~20 min), then run utils/faucet/split-outputs.sh")
PY
REMOTE
