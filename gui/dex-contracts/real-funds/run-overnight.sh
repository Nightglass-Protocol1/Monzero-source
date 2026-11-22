#!/bin/bash
# The owner's overnight queue (2026-11-22): waits for the running USDT-BSC/XMR sell-XMR trade to
# end, then trades USDT-BSC/XMR the other way (the maker's offer buys XMR) with the coins the
# first trade left: account 0 pays the USDT it was paid, account 1 sells the XMR it bought.
# Ctrl-C or a reboot is safe: run it again and each trade carries on where it was.
set -uo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
FIRST=$HOME/dex-real/xmr-usdt-sell-1
NEXT=$HOME/dex-real/xmr-usdt-1
LOG=$HOME/dex-real/overnight.log
say() { echo "$(date +%T) $*" | tee -a "$LOG"; }

say "== 1. waiting for the first trade (sell XMR for USDT) to end"
while pgrep -f "examples/market_e2e $FIRST/run" >/dev/null; do sleep 30; done
if ! grep -q '^PASS:' "$FIRST/run.log"; then
  say "!! the first trade did not end with PASS: carry it on with its own command (see run.log); nothing more is started"
  exit 1
fi
say "   first trade PASSED"

say "== 2. USDT-BSC/XMR, the maker's offer buys XMR: 22 USDT for 0.0465 XMR"
# The maker (account 0) pays USDT and has ~0.00098 BNB for gas; the taker (account 1) sells the
# XMR its wallet bought in the first trade (a copy of that wallet, so no XMR is sent)
env SIDE=buy-xmz USDT_AMOUNT=22 XMR_AMOUNT=0.0465 MAKER_GAS_WEI=300000000000000 TAKER_GAS_WEI=300000000000000 \
  WORK="$NEXT" XMR_WALLET_FROM="$FIRST/run/taker/main/taker" \
  bash "$HERE/run-xmr-bnb.sh"
code=$?
say "   second trade ended with code $code (0 PASSED, 3 NO SWAP = refunded)"
exit $code
