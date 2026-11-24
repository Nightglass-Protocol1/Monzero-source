#!/bin/bash
# The owner's real-funds XMR <-> BNB test (see XMR-BNB-CHECKLIST.md): one public-offer trade on
# BNB Smart Chain mainnet and Monero mainnet, both sides run here, through the same helper
# commands the apps use (post, take, approve, tick, report). Real coins, small amounts.
#
#   BNB_AMOUNT=0.03 XMR_AMOUNT=0.06 bash real-funds/run-xmr-bnb.sh
#
# First run: makes the test accounts (a fresh phrase, kept in a private file, never printed),
# prints the three addresses to fund and waits until the coins are there. Ctrl-C is safe at
# any point: run the same command again and the trade carries on where it was.
#   Maker (account 0): pays BNB_AMOUNT, receives XMR into its own Monero wallet
#   Taker (account 1): sells XMR_AMOUNT from its Monero wallet, receives BNB
#
# Other runs with the same accounts (each in its own WORK folder):
#   SIDE=sell-xmz   the maker's offer sells XMR (the maker holds the XMR and claims; the taker pays)
#   USDT_AMOUNT=23  pair USDT-BSC/XMR: the payer locks USDT (BEP-20, at most 60) instead of BNB
#   USDT_SOL_AMOUNT=10  pair USDT/XMR on Solana mainnet: the payer locks USDT (Tether's SPL mint, at most
#                   60) and needs a little SOL for fees; the seller needs ~0.003 SOL (its USDT account's rent)
#   SOL_AMOUNT=0.2  pair SOL/XMR on Solana mainnet: the payer locks SOL (at most 0.25); the trade
#                   accounts are the Solana keys the wallet derives from accounts 0 and 1
#   XMZ_AMOUNT=1000 BNB_AMOUNT=0.016  pair BNB/XMZ (the first swap of the XMR/XMZ route): the seller sells
#                   XMZ from a v1.3.8 monzero-wallet-rpc wallet on node.monzero.org; no Monero programs
#   XMR_WALLET_FROM=<wallet file>  start the seller's Monero wallet as a copy of an earlier run's
#                   (e.g. ~/dex-real/xmr-bnb-1/run/maker/main/maker), so its XMR need not be sent again
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd); ENGINE=$HERE/../engine
ROOT=${MONZERO_ROOT:-$HOME/projects/Monzero-App}
MONERO_DIR=${MONERO_DIR:-$ROOT/core/build/monero-official}
SIDE=${SIDE:-buy-xmz}
case $SIDE in buy-xmz|sell-xmz) ;; *) echo "!! SIDE is buy-xmz or sell-xmz"; exit 1;; esac
CHAIN=56; RELAY_CHAIN='"rpc": "https://bsc-rpc.publicnode.com", "contract": "0x9073290952DCDF3225D8f65d787cF9148a490feb"'
if [ -n "${USDT_SOL_AMOUNT:-}" ]; then PAIR=USDT/XMR; COINWORD=usdt-sol; CHAIN=501000101
  RELAY_CHAIN='"rpc": "https://solana-rpc.publicnode.com", "solProgram": "GtbWmiCkc4c9nmEbN8tERLovA5CG3FbJQTKisaunEkLK", "maxTokenValue": "60000000"'
elif [ -n "${SOL_AMOUNT:-}" ]; then PAIR=SOL/XMR; COINWORD=sol; CHAIN=501000101
  RELAY_CHAIN='"rpc": "https://solana-rpc.publicnode.com", "solProgram": "GtbWmiCkc4c9nmEbN8tERLovA5CG3FbJQTKisaunEkLK"'
elif [ -n "${USDT_AMOUNT:-}" ]; then PAIR=USDT-BSC/XMR; COINWORD=usdt; else PAIR=BNB/XMR; COINWORD=bnb; fi
PRIV=xmr
if [ -n "${XMZ_AMOUNT:-}" ]; then
  [ "$PAIR" = BNB/XMR ] || { echo "!! XMZ_AMOUNT runs BNB/XMZ only"; exit 1; }
  PRIV=xmz; PAIR=BNB/XMZ; XMR_AMOUNT=0
fi
WORK=${WORK:-$HOME/dex-real/$PRIV-$COINWORD-$([ "$SIDE" = buy-xmz ] && echo 1 || echo sell-1)}
ACCOUNTS_FILE=${ACCOUNTS_FILE:-$HOME/.config/monzero-dex/xmr-bnb-mainnet-accounts.json}
RELAY_PORT=18797
: "${XMR_AMOUNT:?set XMR_AMOUNT (XMR the seller sells), e.g. 0.06}"
# A ceiling for this test (each about 50-60 GBP at November 2026 prices): a typo cannot send more
if [ "$PAIR" = USDT/XMR ]; then
  # Tether's SPL mint has 6 decimals; gas in lamports (the seller's: its USDT account's rent)
  VALUE_WEI=$(python3 -c "from decimal import Decimal as D; v=D('$USDT_SOL_AMOUNT'); assert 0 < v <= D('60'), 'USDT_SOL_AMOUNT must be above 0 and at most 60'; print(int(v*10**6))")
  MAX_VALUE=250000000
  export MAKER_GAS_WEI=${MAKER_GAS_WEI:-10000000} TAKER_GAS_WEI=${TAKER_GAS_WEI:-3000000}
elif [ "$PAIR" = SOL/XMR ]; then
  # Lamports (10^9 per SOL); gas in lamports too (fees and the swap account's rent)
  VALUE_WEI=$(python3 -c "from decimal import Decimal as D; v=D('$SOL_AMOUNT'); assert 0 < v <= D('0.25'), 'SOL_AMOUNT must be above 0 and at most 0.25'; print(int(v*10**9))")
  MAX_VALUE=250000000
  export MAKER_GAS_WEI=${MAKER_GAS_WEI:-10000000} TAKER_GAS_WEI=${TAKER_GAS_WEI:-0}
elif [ "$PAIR" = BNB/XMR ] || [ "$PAIR" = BNB/XMZ ]; then
  : "${BNB_AMOUNT:?set BNB_AMOUNT (BNB the payer pays), e.g. 0.03, or USDT_AMOUNT}"
  VALUE_WEI=$(python3 -c "from decimal import Decimal as D; v=D('$BNB_AMOUNT'); assert 0 < v <= D('0.1'), 'BNB_AMOUNT must be above 0 and at most 0.1'; print(int(v*10**18))")
  MAX_VALUE=100000000000000000
else
  # BSC-USD has 18 decimals, as BNB
  VALUE_WEI=$(python3 -c "from decimal import Decimal as D; v=D('$USDT_AMOUNT'); assert 0 < v <= D('60'), 'USDT_AMOUNT must be above 0 and at most 60'; print(int(v*10**18))")
  MAX_VALUE=60000000000000000000
fi
if [ $PRIV = xmz ]; then
  ATOMS=$(python3 -c "from decimal import Decimal as D; v=D('$XMZ_AMOUNT'); assert 0 < v <= D('20000'), 'XMZ_AMOUNT must be above 0 and at most 20000'; print(int(v*10**11))")
else
  ATOMS=$(python3 -c "from decimal import Decimal as D; v=D('$XMR_AMOUNT'); assert 0 < v <= D('0.3'), 'XMR_AMOUNT must be above 0 and at most 0.3'; print(int(v*10**12))")
fi

T=$ROOT/core/build/dex-rust-toolchain-1.95.0/sysroot/usr
command -v cargo >/dev/null || export PATH=$T/bin:$PATH LD_LIBRARY_PATH=$T/lib/x86_64-linux-gnu
export CARGO_HOME=${CARGO_HOME:-$ROOT/core/build/dex-cargo-home}

if [ $PRIV = xmr ]; then
echo "== Monero programs (official release, checked against its signed hash list)"
( cd "$MONERO_DIR/dl" && grep 'monero-linux-x64-v' hashes.txt | sha256sum -c - )
fi

echo "== test accounts"
if [ ! -f "$ACCOUNTS_FILE" ]; then
  ( cd "$ROOT/monzero-app-eth/gui/dex-contracts" && node scripts/new-test-phrase.js "$ACCOUNTS_FILE" >/dev/null )
  echo "   made $ACCOUNTS_FILE (private). Back it up while it holds coins."
else
  echo "   $ACCOUNTS_FILE"
fi
mkdir -p "$WORK"; chmod 700 "$WORK"
SELLER=$([ "$SIDE" = sell-xmz ] && echo maker || echo taker)
if [ -n "${XMR_WALLET_FROM:-}" ] && [ ! -f "$WORK/run/$SELLER/main/$SELLER.keys" ]; then
  [ -f "$XMR_WALLET_FROM.keys" ] || { echo "!! no wallet at $XMR_WALLET_FROM"; exit 1; }
  pgrep -f "wallet-dir $(dirname "$XMR_WALLET_FROM")" >/dev/null && { echo "!! that wallet is open in another run"; exit 1; }
  mkdir -p "$WORK/run/$SELLER/main"
  cp "$XMR_WALLET_FROM" "$WORK/run/$SELLER/main/$SELLER"; cp "$XMR_WALLET_FROM.keys" "$WORK/run/$SELLER/main/$SELLER.keys"
  echo "   the $SELLER's XMR wallet: a copy of $XMR_WALLET_FROM (use only this copy from now on)"
fi

if [ $PRIV = xmz ]; then
  WALLET_RPC=${XMZ_WALLET_RPC:-$(ls "$HOME"/dex-real/xmz-bin/*/monzero-wallet-rpc | head -1)}
  WALLET_NODE=${XMZ_NODE:-node.monzero.org:6175}; TRUSTED=0; WITNESSES=
  curl -s -m 10 "http://$WALLET_NODE/get_info" | python3 -c "import json,sys; d=json.load(sys.stdin); assert d['nettype']=='mainnet'; print('== XMZ node', '$WALLET_NODE', 'height', d['height'])" || { echo "!! XMZ node $WALLET_NODE does not answer"; exit 1; }
  echo "   wallet program: $WALLET_RPC ($("$WALLET_RPC" --version))"
else
WALLET_RPC=$MONERO_DIR/linux-x64/monero-wallet-rpc
echo "== Monero nodes"
info() { curl -s -m 10 "$1/get_info" | python3 -c "import json,sys; d=json.load(sys.stdin); assert d['nettype']=='mainnet' and d['synchronized']; print(d['height'])" 2>/dev/null; }
# The wallets sync from the project's own node when it answers, else a public one (not trusted then)
WALLET_NODE=${XMR_NODE:-}; TRUSTED=0
if [ -z "$WALLET_NODE" ] && h=$(info http://31.70.193.31:18089); then WALLET_NODE=31.70.193.31:18089; TRUSTED=1; fi
if [ -z "$WALLET_NODE" ]; then
  for n in xmr-node.cakewallet.com:18081 xmr.cryptostorm.is:18081; do
    if info "https://$n" >/dev/null; then WALLET_NODE=$n; break; fi
  done
fi
[ -n "$WALLET_NODE" ] || { echo "!! no Monero mainnet node answers; try later or set XMR_NODE=host:port"; exit 1; }
echo "   wallets use: $WALLET_NODE $([ $TRUSTED = 1 ] && echo '(own node)' || echo '(public, untrusted)')"
# Witnesses: at least two other nodes that confirm the XMR lock, over https
WITNESSES=
for n in https://node.monerodevs.org:18089 https://monero.stackwallet.com:18081 https://xmr.unshakled.net:443 https://xmr.cryptostorm.is:18081 https://xmr-node.cakewallet.com:18081; do
  case "$n" in *"$WALLET_NODE"*) continue;; esac
  if h=$(info "$n"); then WITNESSES=${WITNESSES:+$WITNESSES,}$n; echo "   witness: $n (height $h)"; fi
  [ "$(echo "$WITNESSES" | tr ',' '\n' | grep -c .)" -ge 3 ] && break
done
[ "$(echo "$WITNESSES" | tr ',' '\n' | grep -c .)" -ge 2 ] || { echo "!! fewer than 2 witness nodes answer; try later"; exit 1; }
fi

echo "== local relay (chain $CHAIN, $PAIR, XMR offers on mainnet accepted: this test only)"
mkdir -p "$WORK/relay-data"
cat > "$WORK/relay.json" <<J
{"listen": "127.0.0.1:$RELAY_PORT", "dataDir": "$WORK/relay-data", "xmrMainnet": true,
 "chains": [{"chainId": $CHAIN, $RELAY_CHAIN, "maxValueWei": "$MAX_VALUE"}]}
J
( cd "$HERE/../relay" && cargo build --offline --release 2>&1 | tail -1 )
( cd "$ENGINE" && cargo build --offline --release --example market_e2e 2>&1 | tail -1 )
systemctl --user stop dex-xmr-bnb-relay 2>/dev/null || true
trap 'systemctl --user stop dex-xmr-bnb-relay 2>/dev/null || true' EXIT
systemd-run --user --quiet --unit=dex-xmr-bnb-relay --collect "$HERE/../relay/target/release/monzero-dex-relay" "$WORK/relay.json"
for _ in $(seq 1 30); do curl -s -m 2 "http://127.0.0.1:$RELAY_PORT/v1/info" | grep -qF "$PAIR" && break; sleep 1; done
curl -s -m 5 "http://127.0.0.1:$RELAY_PORT/v1/info" | grep -qF "$PAIR" || { echo "!! the relay did not come up with the $PAIR pair"; exit 1; }

echo "== the trade ($SIDE, $PAIR): ${USDT_SOL_AMOUNT:-${SOL_AMOUNT:-${BNB_AMOUNT:-${USDT_AMOUNT:-}}}} ${PAIR%%/*} for $([ $PRIV = xmz ] && echo "$XMZ_AMOUNT XMZ" || echo "$XMR_AMOUNT XMR") (allow 1-2 hours once funded)"
mkdir -p "$WORK/run"
code=0
NET=mainnet COIN=$PRIV CHAIN_ID=$CHAIN SIDE=$SIDE PAIR=$PAIR FUNDER_RPC=none OWNER_INDEX=0 CLAIMER_INDEX=1 \
  ACCOUNTS_FILE=$ACCOUNTS_FILE RELAY=http://127.0.0.1:$RELAY_PORT \
  XMZ_NODE=$WALLET_NODE TRUSTED_DAEMON=$TRUSTED WITNESSES=$WITNESSES \
  WALLET_RPC_BIN=$WALLET_RPC VALUE_WEI=$VALUE_WEI XMZ_ATOMS=$ATOMS \
  "$ENGINE/target/release/examples/market_e2e" "$WORK/run" 2>&1 | tee -a "$WORK/run.log" || code=${PIPESTATUS[0]}
case $code in
  0) echo "== PASSED. The XMR bought is in $WORK/run/$([ "$SIDE" = sell-xmz ] && echo taker || echo maker)/main; the ${PAIR%%/*} went to account $([ "$SIDE" = sell-xmz ] && echo 0 || echo 1)." ;;
  3) echo "== NO SWAP: the trade ended by refund or abort; every coin went back to its sender. Send run.log to Claude." ;;
  *) echo "== stopped ($code). Run the same command again to carry on; send run.log to Claude if it repeats." ;;
esac
exit $code
