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
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd); ENGINE=$HERE/../engine
ROOT=${MONZERO_ROOT:-$HOME/projects/Monzero-App}
MONERO_DIR=${MONERO_DIR:-$ROOT/core/build/monero-official}
WORK=${WORK:-$HOME/dex-real/xmr-bnb-1}
ACCOUNTS_FILE=${ACCOUNTS_FILE:-$HOME/.config/monzero-dex/xmr-bnb-mainnet-accounts.json}
RELAY_PORT=18797
: "${BNB_AMOUNT:?set BNB_AMOUNT (BNB the maker pays), e.g. 0.03}"
: "${XMR_AMOUNT:?set XMR_AMOUNT (XMR the taker sells), e.g. 0.06}"
# A ceiling for this test (both are about 60 GBP at November 2026 prices): a typo cannot send more
VALUE_WEI=$(python3 -c "from decimal import Decimal as D; v=D('$BNB_AMOUNT'); assert 0 < v <= D('0.1'), 'BNB_AMOUNT must be above 0 and at most 0.1'; print(int(v*10**18))")
ATOMS=$(python3 -c "from decimal import Decimal as D; v=D('$XMR_AMOUNT'); assert 0 < v <= D('0.3'), 'XMR_AMOUNT must be above 0 and at most 0.3'; print(int(v*10**12))")

T=$ROOT/core/build/dex-rust-toolchain-1.95.0/sysroot/usr
command -v cargo >/dev/null || export PATH=$T/bin:$PATH LD_LIBRARY_PATH=$T/lib/x86_64-linux-gnu
export CARGO_HOME=${CARGO_HOME:-$ROOT/core/build/dex-cargo-home}

echo "== Monero programs (official release, checked against its signed hash list)"
( cd "$MONERO_DIR/dl" && grep 'monero-linux-x64-v' hashes.txt | sha256sum -c - )

echo "== test accounts"
if [ ! -f "$ACCOUNTS_FILE" ]; then
  ( cd "$ROOT/monzero-app-eth/gui/dex-contracts" && node scripts/new-test-phrase.js "$ACCOUNTS_FILE" >/dev/null )
  echo "   made $ACCOUNTS_FILE (private). Back it up while it holds coins."
else
  echo "   $ACCOUNTS_FILE"
fi
mkdir -p "$WORK"; chmod 700 "$WORK"

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

echo "== local relay (BNB Smart Chain, XMR offers on mainnet accepted: this test only)"
mkdir -p "$WORK/relay-data"
cat > "$WORK/relay.json" <<J
{"listen": "127.0.0.1:$RELAY_PORT", "dataDir": "$WORK/relay-data", "xmrMainnet": true,
 "chains": [{"chainId": 56, "rpc": "https://bsc-rpc.publicnode.com", "contract": "0x9073290952DCDF3225D8f65d787cF9148a490feb", "maxValueWei": "100000000000000000"}]}
J
( cd "$HERE/../relay" && cargo build --offline --release 2>&1 | tail -1 )
( cd "$ENGINE" && cargo build --offline --release --example market_e2e 2>&1 | tail -1 )
systemctl --user stop dex-xmr-bnb-relay 2>/dev/null || true
trap 'systemctl --user stop dex-xmr-bnb-relay 2>/dev/null || true' EXIT
systemd-run --user --quiet --unit=dex-xmr-bnb-relay --collect "$HERE/../relay/target/release/monzero-dex-relay" "$WORK/relay.json"
for _ in $(seq 1 30); do curl -s -m 2 "http://127.0.0.1:$RELAY_PORT/v1/info" | grep -q 'BNB/XMR' && break; sleep 1; done
curl -s -m 5 "http://127.0.0.1:$RELAY_PORT/v1/info" | grep -q 'BNB/XMR' || { echo "!! the relay did not come up with the BNB/XMR pair"; exit 1; }

echo "== the trade: $BNB_AMOUNT BNB for $XMR_AMOUNT XMR (allow 1-2 hours once funded)"
mkdir -p "$WORK/run"
code=0
NET=mainnet COIN=xmr CHAIN_ID=56 FUNDER_RPC=none OWNER_INDEX=0 CLAIMER_INDEX=1 \
  ACCOUNTS_FILE=$ACCOUNTS_FILE RELAY=http://127.0.0.1:$RELAY_PORT \
  XMZ_NODE=$WALLET_NODE TRUSTED_DAEMON=$TRUSTED WITNESSES=$WITNESSES \
  WALLET_RPC_BIN=$MONERO_DIR/linux-x64/monero-wallet-rpc VALUE_WEI=$VALUE_WEI XMZ_ATOMS=$ATOMS \
  "$ENGINE/target/release/examples/market_e2e" "$WORK/run" 2>&1 | tee -a "$WORK/run.log" || code=${PIPESTATUS[0]}
case $code in
  0) echo "== PASSED. The maker's XMR is in $WORK/run/maker/main, the taker's BNB on account 1." ;;
  3) echo "== NO SWAP: the trade ended by refund or abort; every coin went back to its sender. Send run.log to Claude." ;;
  *) echo "== stopped ($code). Run the same command again to carry on; send run.log to Claude if it repeats." ;;
esac
exit $code
