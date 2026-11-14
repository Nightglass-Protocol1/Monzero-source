#!/bin/bash
# One public-offer trade ETH/XMR on public test networks: Sepolia + Monero stagenet.
# Test coins only. Starts a stagenet funding wallet and a local relay, then runs the trade
# through the helper's commands (post, take, approve, tick, report). Safe to run again: it
# picks the same trade up where it stopped.
#
#   bash tests/run-xmr-stagenet.sh
#
# First run: it prints the funding wallet's stagenet address and stops. Send it at least
# 0.05 stagenet XMR from a faucet, wait ~20 minutes (10 blocks), run again.
# The Sepolia test accounts (~/.config/monzero-dex/eth-sepolia-accounts.json, index 1 pays)
# need a little Sepolia ETH: 0.001 for the trade plus gas.
set -euo pipefail
cd "$(dirname "$0")/.."
ROOT=${MONZERO_ROOT:-$HOME/projects/Monzero-App}
MONERO_DIR=${MONERO_DIR:-$ROOT/core/build/monero-official}
WORK=${WORK:-$ROOT/core/build/xmr-stagenet}
NODE=${XMZ_NODE:-node.monerodevs.org:38089}
T=$ROOT/core/build/dex-rust-toolchain-1.95.0/sysroot/usr
export PATH=$T/bin:$PATH LD_LIBRARY_PATH=$T/lib/x86_64-linux-gnu CARGO_HOME=$ROOT/core/build/dex-cargo-home
FUNDER_PORT=38183; RELAY_PORT=18796
ATOMS=${XMZ_ATOMS:-10000000000}                 # 0.01 XMR traded
NEED=$((3 * ATOMS))                             # funding wallet must hold this, unlocked

( cd "$MONERO_DIR/dl" && grep 'monero-linux-x64-v' hashes.txt | sha256sum -c - )
mkdir -p "$WORK/funder" "$WORK/relay-data" "$WORK/run"
rpc() { curl -s -m 600 "http://127.0.0.1:$FUNDER_PORT/json_rpc" -H 'content-type: application/json' -d "{\"jsonrpc\":\"2.0\",\"id\":0,\"method\":\"$1\",\"params\":${2:-{\}}}"; }
field() { python3 -c "import json,sys; print(json.load(sys.stdin).get('result',{}).get('$1',''))"; }

echo "== stagenet funding wallet"
systemctl --user stop dex-xmr-relay 2>/dev/null || true
# A funding wallet left running by an earlier run is used as it is
rpc get_version | grep -q result || systemd-run --user --quiet --unit=dex-xmr-funder --collect "$MONERO_DIR/linux-x64/monero-wallet-rpc" --stagenet \
  --wallet-dir "$WORK/funder" --daemon-address "$NODE" --trusted-daemon --disable-rpc-login \
  --rpc-bind-ip 127.0.0.1 --rpc-bind-port $FUNDER_PORT --non-interactive --log-file "$WORK/funder.log"
trap 'systemctl --user stop dex-xmr-funder dex-xmr-relay 2>/dev/null || true' EXIT
for _ in $(seq 1 60); do rpc get_version | grep -q result && break; sleep 1; done
if [ -f "$WORK/funder/funder.keys" ]; then rpc open_wallet '{"filename":"funder","password":""}' >/dev/null
else rpc create_wallet '{"filename":"funder","password":"","language":"English"}' >/dev/null; fi
ADDRESS=$(rpc get_address | field address)
rpc refresh >/dev/null
UNLOCKED=$(rpc get_balance | field unlocked_balance); TOTAL=$(rpc get_balance | field balance)
echo "   address:  $ADDRESS"
echo "   balance:  ${TOTAL:-0} atoms, ${UNLOCKED:-0} spendable (1 XMR = 1000000000000 atoms)"
if [ "${UNLOCKED:-0}" -lt "$NEED" ]; then
  echo "!! The funding wallet needs $NEED atoms ($(python3 -c "print($NEED/1e12)") XMR) spendable."
  echo "   Send stagenet XMR to the address above, wait about 20 minutes, then run this again."
  exit 2
fi

echo "== local relay (Sepolia)"
cat > "$WORK/relay.json" <<J
{"listen": "127.0.0.1:$RELAY_PORT", "dataDir": "$WORK/relay-data",
 "chains": [{"chainId": 11155111, "rpc": "https://ethereum-sepolia-rpc.publicnode.com", "contract": "0xcFc3b72EA8E5fa41905a2119c457c43162d9Ab30"}]}
J
( cd ../relay && cargo build --offline --release 2>&1 | tail -1 )
systemd-run --user --quiet --unit=dex-xmr-relay --collect "$PWD/../relay/target/release/monzero-dex-relay" "$WORK/relay.json"
for _ in $(seq 1 30); do curl -s -m 2 "http://127.0.0.1:$RELAY_PORT/v1/info" | grep -q XMR && break; sleep 1; done
curl -s -m 5 "http://127.0.0.1:$RELAY_PORT/v1/info" | grep -q 'ETH/XMR' || { echo "!! the relay did not come up with the ETH/XMR pair"; exit 1; }

echo "== the trade: 0.001 SepETH for $(python3 -c "print($ATOMS/1e12)") stagenet XMR (expect 40-60 minutes)"
COIN=xmr RELAY=http://127.0.0.1:$RELAY_PORT FUNDER_RPC=127.0.0.1:$FUNDER_PORT XMZ_NODE=$NODE XMZ_ATOMS=$ATOMS \
  WALLET_RPC_BIN=$MONERO_DIR/linux-x64/monero-wallet-rpc VALUE_WEI=${VALUE_WEI:-1000000000000000} \
  WITNESSES=${WITNESSES:-http://stagenet.xmr-tw.org:38081,http://node2.monerodevs.org:38089} \
  cargo run --offline --example market_e2e -- "$WORK/run" 2>&1 | tee -a "$WORK/run.log"
