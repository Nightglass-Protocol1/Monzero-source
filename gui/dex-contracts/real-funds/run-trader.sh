#!/bin/bash
# One side of the real-funds XMZ <-> BNB test on mainnet (see CHECKLIST.md). Run each side in
# its own terminal:
#   run-trader.sh buyer  <run-dir>                       # pays BNB, receives XMZ (account 0)
#   run-trader.sh seller <run-dir> <buyer-run-dir>       # sells XMZ, receives BNB (account 1)
#   run-trader.sh <buyer|seller> <run-dir> resume        # after a crash, reboot or Ctrl-C
# Needs BNB_AMOUNT and XMZ_AMOUNT (whole coins), e.g. BNB_AMOUNT=0.01 XMZ_AMOUNT=7000 ($0.001 per XMZ with BNB at about $700).
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
ENGINE=$HERE/../engine
ROLE=${1:?buyer or seller}; DIR=${2:?run directory}; ARG3=${3:-}
: "${BNB_AMOUNT:?set BNB_AMOUNT, e.g. 0.01}" "${XMZ_AMOUNT:?set XMZ_AMOUNT, e.g. 7000}"
export NETWORK=mainnet BNB_AMOUNT XMZ_AMOUNT
export ACCOUNTS_FILE=${ACCOUNTS_FILE:-$HOME/.config/monzero-dex/bsc-mainnet-accounts.json}
export XMZ_NODE=${XMZ_NODE:-127.0.0.1:6175}
export WALLET_RPC_BIN=${WALLET_RPC_BIN:-$(command -v monzero-wallet-rpc || echo "$HOME/projects/Monzero-App/testnet/bin/monzero-v1.0.0-linux-x86_64/monzero-wallet-rpc")}
[ -f "$ACCOUNTS_FILE" ] || { echo "no $ACCOUNTS_FILE: run node scripts/new-test-phrase.js first"; exit 1; }
[ -x "$WALLET_RPC_BIN" ] || { echo "monzero-wallet-rpc not found: set WALLET_RPC_BIN"; exit 1; }
[ -f "$HERE/../deployments/bsc-mainnet.json" ] || { echo "no deployments/bsc-mainnet.json: deploy the contract first"; exit 1; }

# The pinned Rust toolchain when cargo is not installed
if ! command -v cargo >/dev/null; then
  S=$HOME/projects/Monzero-App/core/build/dex-rust-toolchain-1.95.0/sysroot/usr
  export PATH=$S/bin:$PATH LD_LIBRARY_PATH=$S/lib/x86_64-linux-gnu
fi
(cd "$ENGINE" && cargo build --locked --release --example bsc_trader >/dev/null)
BIN=$ENGINE/target/release/examples/bsc_trader

mkdir -p "$DIR"; chmod 700 "$DIR"
case "$ROLE:$ARG3" in
  buyer:resume)  args=(owner resume "$DIR" 47101) ;;
  seller:resume) args=(claimer resume "$DIR") ;;
  buyer:)        args=(owner new "$DIR" 47101) ;;
  seller:?*)     [ -s "$ARG3/peer.txt" ] || { echo "start the buyer first ($ARG3/peer.txt missing)"; exit 1; }
                 args=(claimer new "$DIR" "$ARG3/offer.json" "$(cat "$ARG3/peer.txt")") ;;
  *) echo "usage: see the top of this file"; exit 1 ;;
esac
echo "MAINNET: real BNB and XMZ. $ROLE, $BNB_AMOUNT BNB for $XMZ_AMOUNT XMZ. Log: $DIR/trader.log"
"$BIN" "${args[@]}" 2>&1 | tee -a "$DIR/trader.log"
