#!/bin/bash
# How fast the pool pays: look at the numbers, or change the two settings that decide it.
# Run from the owner's PC:
#   bash ~/projects/Monzero-App/pool/xmzpool/deploy/pool-speed.sh check
#   bash ~/projects/Monzero-App/pool/xmzpool/deploy/pool-speed.sh set interval=600
#   bash ~/projects/Monzero-App/pool/xmzpool/deploy/pool-speed.sh set interval=600 confirmations=30
#
# interval       seconds between payout rounds (no risk: only more, smaller payments)
# confirmations  blocks before a found block is credited to miners. The network locks every mined
#                reward for 60 blocks, so below 60 the pool pays from coins it already holds unlocked.
#                Refused when the pool wallet does not hold enough for that (FORCE=1 overrides).
set -euo pipefail
HOST=${HOST:-monzero-eu}
MODE=${1:-check}; shift || true
INTERVAL=""; DEPTH=""
for kv in "$@"; do case "$kv" in
  interval=*) INTERVAL=${kv#*=};; confirmations=*) DEPTH=${kv#*=};; *) echo "unknown setting: $kv"; exit 1;; esac; done
case "$MODE" in check|set) ;; *) echo "usage: $0 check | set [interval=SECONDS] [confirmations=BLOCKS]"; exit 1;; esac
case "$INTERVAL$DEPTH" in *[!0-9]*) echo "values must be numbers"; exit 1;; esac
if [ "$MODE" = set ]; then
  [ -n "$INTERVAL$DEPTH" ] || { echo "nothing to set"; exit 1; }
  [ -z "$INTERVAL" ] || { [ "$INTERVAL" -ge 300 ] && [ "$INTERVAL" -le 86400 ]; } || { echo "interval must be 300 to 86400 seconds"; exit 1; }
  [ -z "$DEPTH" ] || { [ "$DEPTH" -ge 10 ] && [ "$DEPTH" -le 60 ]; } || { echo "confirmations must be 10 to 60"; exit 1; }
fi
TS=$(date -u +%Y%m%d-%H%M%S)

ssh -o BatchMode=yes "$HOST" sudo TS="$TS" MODE="$MODE" INTERVAL="$INTERVAL" DEPTH="$DEPTH" FORCE="${FORCE:-0}" bash -s <<'REMOTE'
set -euo pipefail
CFG=/etc/xmzpool/pool.json
DB=$(python3 -c 'import json,sys;print(json.load(open(sys.argv[1]))["database"])' "$CFG")
python3 - "$CFG" "$DB" "$MODE" "$INTERVAL" "$DEPTH" "$FORCE" <<'PY'
import json, sqlite3, sys
sys.path.insert(0, "/opt/xmzpool")
import xmzpool as xp
path, dbpath, mode, interval, depth, force = sys.argv[1:7]
cfg = xp.load_config(path)
auth = tuple(cfg["wallet_rpc_login"].split(":", 1)) if cfg.get("wallet_rpc_login") else None
bal = xp.rpc(cfg["wallet_rpc"].rstrip("/") + "/json_rpc", "get_balance", {"account_index": 0}, 30, auth)
db = sqlite3.connect("file:" + dbpath + "?mode=ro", uri=True)
owed = db.execute("SELECT COALESCE(SUM(pending),0) FROM balances").fetchone()[0]
n, waiting = db.execute("SELECT COUNT(*), COALESCE(SUM(reward),0) FROM blocks WHERE status='pending'").fetchone()
reward = waiting // n if n else 0
unlocked, spare = bal["unlocked_balance"], bal["unlocked_balance"] - owed
X = xp.ATOMIC
print("now: paid every %d s, blocks credited after %d" % (cfg["payouts"]["interval_seconds"], cfg["confirmations"]))
print("pool wallet: %.2f XMZ unlocked of %.2f | owed to miners: %.2f | spare: %.2f" % (unlocked / X, bal["balance"] / X, owed / X, spare / X))
print("not yet credited: %d blocks, %.2f XMZ (about %.2f a block)" % (n, waiting / X, reward / X))
import time
due = db.execute("SELECT COUNT(*), COALESCE(SUM(pending),0), COALESCE(MAX(pending),0) FROM balances WHERE pending>=?", (int(cfg["payouts"]["threshold_xmz"] * X),)).fetchone()
print("miners due a payment: %d, %.2f XMZ in all, largest %.2f" % (due[0], due[1] / X, due[2] / X))
print("last payments:")
for pid, ts, status, txid, detail, miners, amount in db.execute(
        "SELECT id, ts, status, COALESCE(txid,''), COALESCE(detail,''), (SELECT COUNT(*) FROM payment_items WHERE payment=payments.id),"
        " (SELECT COALESCE(SUM(amount),0) FROM payment_items WHERE payment=payments.id) FROM payments ORDER BY id DESC LIMIT 6"):
    print("  #%d %s UTC %-9s %2d miners %10.2f XMZ %s %s" % (pid, time.strftime("%H:%M:%S", time.gmtime(ts)), status, miners, amount / X, txid[:16], detail[:420]))
for d in (60, 45, 30, 20, 10):
    need = (60 - d) * reward
    print("  credited after %2d blocks: the pool pays %9.2f XMZ ahead of the network -> %s" % (d, need / X, "covered" if spare >= need * 1.2 else "NOT covered by the spare coins"))
if mode == "set" and depth:
    need = (60 - int(depth)) * reward
    if spare < need * 1.2 and force != "1":
        sys.exit("!! refused: %s confirmations needs %.2f XMZ spare (with a fifth in hand); the wallet has %.2f" % (depth, need * 1.2 / X, spare / X))
PY
if [ "$MODE" = check ]; then
  echo "payout halt file: $([ -e "${DB%.*}.payouts-halted" ] && echo PRESENT || echo none) | pool running since: $(systemctl show -p ActiveEnterTimestamp --value xmzpool)"
  echo "pool log since it started (payouts, warnings, errors):"
  journalctl -u xmzpool --since "$(systemctl show -p ActiveEnterTimestamp --value xmzpool)" --no-pager 2>/dev/null | grep -iE 'pay|warn|error|traceback|exception' | tail -30 | cut -c1-420
  exit 0
fi
[ -e "${DB%.*}.payouts-halted" ] && { echo "!! payouts are halted; reconcile first"; exit 1; }
BUSY=$(python3 -c 'import sqlite3,sys;print(sqlite3.connect("file:"+sys.argv[1]+"?mode=ro",uri=True).execute("SELECT COUNT(*) FROM payments WHERE status=?",("sending",)).fetchone()[0])' "$DB")
[ "$BUSY" = 0 ] || { echo "!! a payment is being sent right now; run this again in a minute"; exit 1; }
cp -p "$CFG" "$CFG.before-speed-$TS"
python3 - "$CFG" "$INTERVAL" "$DEPTH" <<'PY'
import json, sys
cfg = json.load(open(sys.argv[1]))
if sys.argv[2]: cfg.setdefault("payouts", {})["interval_seconds"] = int(sys.argv[2])
if sys.argv[3]: cfg["confirmations"] = int(sys.argv[3])
open(sys.argv[1], "w").write(json.dumps(cfg, indent=2) + "\n")
PY
systemctl restart xmzpool; sleep 5
systemctl is-active xmzpool
python3 -c 'import json,sys;c=json.load(open(sys.argv[1]));print("set: paid every",c.get("payouts",{}).get("interval_seconds","(default 3600)"),"s | credited after",c.get("confirmations","(default 60)"),"blocks")' "$CFG"
curl -s -m 10 http://127.0.0.1:8117/api/stats | python3 -c 'import sys,json;s=json.load(sys.stdin);print("API ok: height",s["network"]["height"],"| payouts enabled:",s["config"]["paymentsEnabled"])'
echo "Undo: cp -p $CFG.before-speed-$TS $CFG; systemctl restart xmzpool"
REMOTE
