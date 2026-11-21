#!/bin/bash
# Switch on (or change, or off) the DEX liquidity share: a part of the pool's OWN fee sent to the
# liquidity bot's XMZ wallet. Miners are never touched: a transfer goes only when the pool wallet's
# unlocked balance still covers every miner's whole pending balance, and only fee earned from now on
# counts (not the pool's history). Run update-pool.sh first (the program must know `liquidity`).
#   bash enable-liquidity.sh show
#   bash enable-liquidity.sh on  BOT_XMZ_ADDRESS [SHARE_BASIS_POINTS=100]   (100 = 1% of the pool fee)
#   bash enable-liquidity.sh off
set -euo pipefail
HOST=${HOST:-monzero-eu}
MODE=${1:-show}; ADDR=${2:-}; BP=${3:-100}
case "$MODE" in
  show|off) ;;
  on) [[ "$ADDR" =~ ^F[1-9A-HJ-NP-Za-km-z]{94}$ ]] || { echo "the bot's address must be a standard XMZ address (F..., 95 characters)"; exit 1; }
      [[ "$BP" =~ ^[0-9]+$ ]] && [ "$BP" -ge 1 ] && [ "$BP" -le 10000 ] || { echo "share must be 1..10000 basis points (100 = 1%)"; exit 1; } ;;
  *) echo "usage: $0 show | on ADDRESS [BASIS_POINTS] | off"; exit 1 ;;
esac
ssh -o BatchMode=yes "$HOST" sudo MODE="$MODE" ADDR="$ADDR" BP="$BP" bash -s <<'REMOTE'
set -euo pipefail
CFG=/etc/xmzpool/pool.json
grep -q '"liquidity"' /opt/xmzpool/xmzpool.py || { echo "!! the pool program is older than the liquidity share: run update-pool.sh first"; exit 1; }
if [ "$MODE" != show ]; then
  TS=$(date -u +%Y%m%d-%H%M%S); cp -p "$CFG" "$CFG.before-$TS"
  python3 - "$CFG" <<'PY'
import json, os, sys
path = sys.argv[1]; c = json.load(open(path))
lq = c.get("liquidity", {})
if os.environ["MODE"] == "on":
    lq.update(enabled=True, address=os.environ["ADDR"], share_basis_points=int(os.environ["BP"]))
else:
    lq["enabled"] = False
c["liquidity"] = lq
tmp = path + ".tmp"; open(tmp, "w").write(json.dumps(c, indent=1) + "\n"); os.chmod(tmp, os.stat(path).st_mode & 0o777)
st = os.stat(path); os.chown(tmp, st.st_uid, st.st_gid); os.replace(tmp, path)
PY
  python3 -c 'import sys; sys.path.insert(0,"/opt/xmzpool"); import xmzpool; xmzpool.load_config("/etc/xmzpool/pool.json")'
  systemctl restart xmzpool; sleep 5; systemctl is-active xmzpool
  echo "Undo: cp -p $CFG.before-$TS $CFG; systemctl restart xmzpool"
fi
curl -s -m 10 http://127.0.0.1:8117/api/stats | python3 -c 'import sys,json;l=json.load(sys.stdin).get("liquidity");print("liquidity:", json.dumps(l))'
REMOTE
