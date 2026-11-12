#!/bin/bash
# Update only the pool program (xmzpool.py) on monzero-eu. Config, unit and payout setting stay as they are.
# Run from the owner's PC:  bash ~/projects/Monzero-App/pool/xmzpool/deploy/update-pool.sh
# (deploy-eu.sh is the first install: it writes a new config with payouts OFF. Do not use it for updates.)
set -euo pipefail
HOST=${HOST:-monzero-eu}
HERE=$(cd "$(dirname "$0")/.." && pwd)
TS=$(date -u +%Y%m%d-%H%M%S)

echo "== checking the program here"
python3 -m py_compile "$HERE/xmzpool.py"
python3 -W ignore "$HERE/tests/unit_test.py" 2>&1 | tail -3

echo "== copying"
scp -q "$HERE/xmzpool.py" "$HOST:/tmp/xmzpool.py.new"
ssh -o BatchMode=yes "$HOST" sudo TS="$TS" bash -s <<'REMOTE'
set -euo pipefail
CFG=/etc/xmzpool/pool.json
DB=$(python3 -c 'import json,sys;print(json.load(open(sys.argv[1]))["database"])' "$CFG")
[ -e "${DB%.*}.payouts-halted" ] && { echo "!! payouts are halted; reconcile first"; rm -f /tmp/xmzpool.py.new; exit 1; }
# Never restart while a payment is on its way to the wallet
BUSY=$(python3 -c 'import sqlite3,sys;print(sqlite3.connect("file:"+sys.argv[1]+"?mode=ro",uri=True).execute("SELECT COUNT(*) FROM payments WHERE status=?",("sending",)).fetchone()[0])' "$DB")
[ "$BUSY" = 0 ] || { echo "!! a payment is being sent right now; run this again in a minute"; rm -f /tmp/xmzpool.py.new; exit 1; }
python3 -m py_compile /tmp/xmzpool.py.new
cp -p /opt/xmzpool/xmzpool.py "/opt/xmzpool/xmzpool.py.before-$TS"
install -o root -g root -m 644 /tmp/xmzpool.py.new /opt/xmzpool/xmzpool.py; rm -f /tmp/xmzpool.py.new
systemctl restart xmzpool; sleep 5
systemctl is-active xmzpool
curl -s -m 10 http://127.0.0.1:8117/api/stats | python3 -c 'import sys,json;s=json.load(sys.stdin);print("API ok: height",s["network"]["height"],"| payouts enabled:",s["config"]["paymentsEnabled"],"| miners:",s["pool"]["miners"])'
echo "Undo: cp -p /opt/xmzpool/xmzpool.py.before-$TS /opt/xmzpool/xmzpool.py; systemctl restart xmzpool"
REMOTE
