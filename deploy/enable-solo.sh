#!/bin/bash
# Switch on solo mining on pool.monzero.org (monzero-eu): installs the program and page that know solo
# mining (update-pool.sh), adds the solo port to the live config, opens it in the firewall and
# restarts the pool. Safe to run again: each step checks whether it is already done.
# Run from the owner's PC:  bash ~/projects/Monzero-App/pool/xmzpool/deploy/enable-solo.sh
# Solo: port 4444 (difficulty 5000, like 3333), or the login solo:ADDRESS on any port; the pool keeps
# 1% of a solo block (config "solo": {"fee_basis_points": 100}, the default).
set -euo pipefail
HOST=${HOST:-monzero-eu}
PORT=${SOLO_PORT:-4444}
HERE=$(cd "$(dirname "$0")" && pwd)

echo "== 1. program and page (the same checks and safety stops as update-pool.sh)"
bash "$HERE/update-pool.sh"

echo; echo "== 2. solo port $PORT in the config, the firewall and the running pool"
ssh -o BatchMode=yes "$HOST" sudo PORT="$PORT" bash -s <<'REMOTE'
set -euo pipefail
CFG=/etc/xmzpool/pool.json
DB=$(python3 -c 'import json,sys;print(json.load(open(sys.argv[1]))["database"])' "$CFG")
# Add the solo port once (a backup of the config first)
if python3 -c 'import json,sys;c=json.load(open(sys.argv[1]));sys.exit(0 if any(p.get("solo") for p in c["stratum_ports"]) else 1)' "$CFG"; then
  echo "config already has a solo port"
else
  cp -p "$CFG" "$CFG.before-solo-$(date -u +%Y%m%d-%H%M%S)"
  python3 - "$CFG" "$PORT" <<'PY'
import json, sys
path, port = sys.argv[1], int(sys.argv[2])
c = json.load(open(path))
if any(p["port"] == port for p in c["stratum_ports"]):
    sys.exit(f"!! port {port} is already used by a shared port; choose another with SOLO_PORT=")
c["stratum_ports"].append({"port": port, "difficulty": 5000, "desc": "Solo mining", "solo": True})
json.dump(c, open(path + ".new", "w"), indent=2)
PY
  cat "$CFG.new" > "$CFG"; rm -f "$CFG.new"   # in place: the config keeps its owner and permissions
  echo "config: solo port $PORT added"
fi
# Firewall (ufw, as for 3333 and 5555)
if command -v ufw >/dev/null && ufw status | grep -q "Status: active"; then
  ufw status | grep -qE "^$PORT/tcp +ALLOW" && echo "firewall already allows $PORT" || { ufw allow "$PORT/tcp" comment "xmzpool solo" >/dev/null; echo "firewall: $PORT/tcp opened"; }
fi
# Never restart while a payment is on its way to the wallet
BUSY=$(python3 -c 'import sqlite3,sys;print(sqlite3.connect("file:"+sys.argv[1]+"?mode=ro",uri=True).execute("SELECT COUNT(*) FROM payments WHERE status=?",("sending",)).fetchone()[0])' "$DB")
[ "$BUSY" = 0 ] || { echo "!! a payment is being sent right now; run this again in a minute"; exit 1; }
systemctl restart xmzpool; sleep 5
systemctl is-active xmzpool
ss -ltn | grep -q ":$PORT " && echo "listening on $PORT" || { echo "!! nothing listens on $PORT"; exit 1; }
curl -s -m 10 http://127.0.0.1:8117/api/stats | python3 -c 'import sys,json;s=json.load(sys.stdin)["solo"];print("API solo:",s)'
echo "Undo the port: restore $CFG.before-solo-* and systemctl restart xmzpool"
REMOTE

echo; echo "== 3. from outside"
curl -s -m 15 https://pool.monzero.org/api/stats | python3 -c 'import sys,json;s=json.load(sys.stdin);print("ports:",[(p["port"],p.get("solo")) for p in s["config"]["ports"]]);print("solo:",s["solo"])'
timeout 5 bash -c "exec 3<>/dev/tcp/pool.monzero.org/$PORT" && echo "port $PORT reachable from here" || echo "!! port $PORT not reachable from here (cloud firewall?)"
