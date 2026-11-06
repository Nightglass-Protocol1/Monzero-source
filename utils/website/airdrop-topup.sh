#!/bin/bash
# Auto-renew the airdrop budget from the mining pool's 1% fee. Run on a schedule (e.g. hourly
# cron) on the web server. It keeps the airdrop pool funded a little ahead of what participants
# have already earned, never exceeding the fee the project has actually collected, and never
# below the base budget. It writes one number to <monzero-private>/airdrop-pool.txt, which the
# airdrop reads as its live budget — nothing else is touched.
#   PRIVATE=~/monzero-private POOL_HOST=monzero-eu BASE=8000 BUFFER=1000 bash airdrop-topup.sh
set -euo pipefail
PRIVATE=${PRIVATE:-$HOME/monzero-private}
POOL_HOST=${POOL_HOST:-monzero-eu}
BASE=${BASE:-8000}            # starting / minimum budget
BUFFER=${BUFFER:-1000}        # headroom kept above what is already committed
U=100000000000

# Fee the project has collected (XMZ), from the pool's own accounting on the pool server.
FEE=$(ssh -o BatchMode=yes "$POOL_HOST" 'sudo python3 -c "import sqlite3;print(sqlite3.connect(\"file:/var/lib/xmzpool/pool.sqlite?mode=ro\",uri=True).execute(\"SELECT COALESCE(SUM(amount),0) FROM pool_fee\").fetchone()[0])"' 2>/dev/null)
FEE_XMZ=$(python3 -c "print(int($FEE)/$U)")

# XMZ already committed to participants (capped sum), from the airdrop DB.
COMMITTED=$(php -r '
  putenv("MONZERO_PRIVATE_DIR='"$PRIVATE"'");
  require "'"$(cd "$(dirname "$0")/../.."/website && pwd)"'/lib/airdrop.php";
  $p=adPayouts(); echo array_sum(array_column($p,"xmz"));
' 2>/dev/null || echo 0)

# Target = committed + buffer, at least BASE, never more than the fee collected.
TARGET=$(python3 -c "
base=$BASE; buf=$BUFFER; committed=float('$COMMITTED' or 0); fee=float('$FEE_XMZ' or 0)
t=max(base, committed+buf)
t=min(t, fee) if fee>0 else t
print(round(t, 2))
")
umask 077
printf '%s\n' "$TARGET" > "$PRIVATE/airdrop-pool.txt"
echo "airdrop budget -> $TARGET XMZ  (fee collected $FEE_XMZ, committed $COMMITTED, base $BASE)"
