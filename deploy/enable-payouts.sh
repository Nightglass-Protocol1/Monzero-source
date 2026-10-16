#!/bin/bash
# Turn on xmzpool payouts on monzero-eu, after the first credited blocks were checked.
# Run from the owner's PC:  bash ~/projects/Monzero-App/pool/xmzpool/deploy/enable-payouts.sh
set -euo pipefail
HOST=${HOST:-monzero-eu}
ssh -o BatchMode=yes "$HOST" sudo bash -s <<'REMOTE'
set -euo pipefail
CFG=/etc/xmzpool/pool.json UNIT=/etc/systemd/system/xmzpool.service
[ -e /var/lib/xmzpool/pool.payouts-halted ] && { echo "!! payouts are halted; reconcile first"; exit 1; }
confirmed=$(python3 -c 'import sqlite3;print(sqlite3.connect("file:/var/lib/xmzpool/pool.sqlite?mode=ro",uri=True).execute("SELECT COUNT(*) FROM blocks WHERE status=?",("confirmed",)).fetchone()[0])')
[ "$confirmed" -gt 0 ] || { echo "!! no confirmed blocks yet; nothing checked"; exit 1; }
cp -p "$CFG" "$CFG.before-payouts"; cp -p "$UNIT" "$UNIT.before-payouts"
python3 - "$CFG" <<'PY'
import json, sys
cfg = json.load(open(sys.argv[1])); cfg["payouts"]["enabled"] = True
json.dump(cfg, open(sys.argv[1], "w"), indent=2)
PY
grep -q -- '^ExecStart=.*--enable-payouts' "$UNIT" || sed -i -E 's#^(ExecStart=/usr/bin/python3 /opt/xmzpool/xmzpool.py /etc/xmzpool/pool.json)$#\1 --enable-payouts#' "$UNIT"
grep -q -- '^ExecStart=.*--enable-payouts' "$UNIT" || { echo "!! unit not updated; restoring"; cp -p "$CFG.before-payouts" "$CFG"; cp -p "$UNIT.before-payouts" "$UNIT"; exit 1; }
systemctl daemon-reload; systemctl restart xmzpool; sleep 5
systemctl is-active xmzpool
curl -s -m 10 http://127.0.0.1:8117/api/stats | python3 -c 'import sys,json;c=json.load(sys.stdin)["config"];print("payouts enabled:",c["paymentsEnabled"],"| threshold XMZ:",c["minPaymentThreshold"]/1e11)'
echo "Undo: cp $CFG.before-payouts $CFG; cp $UNIT.before-payouts $UNIT; systemctl daemon-reload; systemctl restart xmzpool"
REMOTE
