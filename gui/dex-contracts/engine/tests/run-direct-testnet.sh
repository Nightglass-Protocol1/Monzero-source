#!/bin/bash
# One direct (coin <-> coin) trade on public test networks: Sepolia ETH for BNB Smart Chain
# testnet BNB, through a local relay, with the wallet helper's commands only. Test coins only.
#
#   bash tests/run-direct-testnet.sh
#
# Accounts (see: cargo run --example direct_testnet -- accounts):
#   maker  ~/.config/monzero-dex/eth-sepolia-accounts.json index 2: gives 0.002 SepETH, needs
#          ~0.003 SepETH more for gas and ~0.002 tBNB to claim
#   taker  ~/.config/monzero-dex/bsc-testnet-accounts.json index 3: gives 0.01 tBNB, needs
#          ~0.001 SepETH to claim
# Safe to run again: it picks the same trade up. After a finished trade (swapped or not) its
# folder is kept as run-ended-<time> and the next run trades again.
set -euo pipefail
cd "$(dirname "$0")/.."
ROOT=${MONZERO_ROOT:-$HOME/projects/Monzero-App}
WORK=${WORK:-$ROOT/core/build/direct-testnet}
T=$ROOT/core/build/dex-rust-toolchain-1.95.0/sysroot/usr
export PATH=$T/bin:$PATH LD_LIBRARY_PATH=$T/lib/x86_64-linux-gnu CARGO_HOME=$ROOT/core/build/dex-cargo-home
RELAY_PORT=18797
export RELAY=http://127.0.0.1:$RELAY_PORT
# A trade reads logs back to the block where it started: fine on publicnode for a fresh trade;
# for one resumed after days use an endpoint that keeps old logs (RPC_SEPOLIA=https://sepolia.gateway.tenderly.co)
export RPC_SEPOLIA=${RPC_SEPOLIA:-https://ethereum-sepolia-rpc.publicnode.com}
export RPC_BSC_TESTNET=${RPC_BSC_TESTNET:-https://bsc-testnet-rpc.publicnode.com}
mkdir -p "$WORK/relay-data" "$WORK/run"

# PAIR=eth-sol: Sepolia ETH for devnet SOL (the relay also watches the devnet swap program);
# fund the Solana accounts first:  cargo run --example direct_testnet -- fund-sol
export PAIR=${PAIR:-eth-bnb}
SOL_CHAIN=""
case "$PAIR" in eth-sol|sol-bnb) SOLANA=1 ;; *) SOLANA= ;; esac
[ "$PAIR" = sol-bnb ] && export GIVES_WEI=${GIVES_WEI:-30000000} WANTS_WEI=${WANTS_WEI:-5000000000000000}   # 0.03 SOL for 0.005 tBNB
if [ -n "$SOLANA" ]; then
  export RPC_SOL_DEVNET=${RPC_SOL_DEVNET:-https://api.devnet.solana.com}
  SOL_CHAIN=", {\"chainId\": 501000102, \"rpc\": \"$RPC_SOL_DEVNET\", \"solProgram\": \"9idXkjjEeyu6nLrYSnCcX6PjmRemKQvuz7VwwwYMtihh\"}"
  export WANTS_WEI=${WANTS_WEI:-50000000}     # 0.05 SOL in lamports
  WORK=${WORK_SOL:-$ROOT/core/build/direct-testnet-$PAIR}; mkdir -p "$WORK/relay-data" "$WORK/run"
fi
echo "== local relay (Sepolia + BSC testnet${SOL_CHAIN:+ + Solana devnet})"
cat > "$WORK/relay.json" <<J
{"listen": "127.0.0.1:$RELAY_PORT", "dataDir": "$WORK/relay-data",
 "chains": [{"chainId": 11155111, "rpc": "$RPC_SEPOLIA", "contract": "0xcFc3b72EA8E5fa41905a2119c457c43162d9Ab30"},
            {"chainId": 97, "rpc": "$RPC_BSC_TESTNET", "contract": "0xf7c19f735C3494b356091eFBeFf3ab53617190b7"}$SOL_CHAIN]}
J
( cd ../relay && cargo build --offline --release 2>&1 | tail -1 )
systemctl --user stop dex-direct-relay 2>/dev/null || true
trap 'systemctl --user stop dex-direct-relay 2>/dev/null || true' EXIT
systemd-run --user --quiet --unit=dex-direct-relay --collect "$PWD/../relay/target/release/monzero-dex-relay" "$WORK/relay.json"
for _ in $(seq 1 30); do curl -s -m 2 "$RELAY/v1/info" >/dev/null && break; sleep 1; done
curl -s -m 5 "$RELAY/v1/info" >/dev/null || { echo "!! the relay did not come up"; exit 1; }

echo "== the trade ($PAIR): gives ${GIVES_WEI:-2000000000000000}, wants ${WANTS_WEI:-10000000000000000} (smallest units)"
cargo run --offline --example direct_testnet -- "$WORK/run" 2>&1 | grep --line-buffered -v -E '^warning|^ *-->|^ *\||^ *= note|^$' | tee -a "$WORK/run.log" || true
if grep -q -E '^SWAPPED|^NO SWAP' <(tail -3 "$WORK/run.log"); then
  mv "$WORK/run" "$WORK/run-ended-$(date +%Y%m%d-%H%M%S)"
fi
tail -1 "$WORK/run.log" | grep -q '^SWAPPED'
