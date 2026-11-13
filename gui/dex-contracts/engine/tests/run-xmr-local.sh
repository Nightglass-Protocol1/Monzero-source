#!/bin/bash
# Run the swap tests with Monero (XMR) as the private coin, on local test chains only:
# a local Monero fakechain (monerod --regtest) and a local EVM chain (hardhat). No real coins,
# no public network. Uses the official Monero release, checked against its signed hash list.
#
#   bash tests/run-xmr-local.sh
#
# Needs: MONERO_DIR (default ~/projects/Monzero-App/core/build/monero-official) holding
#   dl/hashes.txt, dl/monero-linux-x64-v*.tar.bz2 and linux-x64/{monerod,monero-wallet-rpc};
#   HARDHAT_DIR with node_modules (default: the monzero-app-eth checkout);
#   the pinned Rust toolchain in core/build.
set -euo pipefail
cd "$(dirname "$0")/.."
ROOT=${MONZERO_ROOT:-$HOME/projects/Monzero-App}
MONERO_DIR=${MONERO_DIR:-$ROOT/core/build/monero-official}
HARDHAT_DIR=${HARDHAT_DIR:-$ROOT/monzero-app-eth/gui/dex-contracts}
T=$ROOT/core/build/dex-rust-toolchain-1.95.0/sysroot/usr
export PATH=$T/bin:$PATH LD_LIBRARY_PATH=$T/lib/x86_64-linux-gnu CARGO_HOME=$ROOT/core/build/dex-cargo-home

echo "== Monero release"
( cd "$MONERO_DIR/dl" && grep 'monero-linux-x64-v' hashes.txt | sha256sum -c - )
"$MONERO_DIR/linux-x64/monerod" --version
[ -f ../artifacts/contracts/XmzSwap.sol/XmzSwap.json ] || { echo "!! ../artifacts is missing: run 'npx hardhat compile' in dex-contracts (or copy artifacts from another checkout)"; exit 1; }

STARTED=false
if ! ss -ltn | grep -q '127.0.0.1:8545'; then
  echo "== starting a local EVM chain"
  systemd-run --user --quiet --unit=dex-xmr-hardhat --collect --working-directory="$HARDHAT_DIR" npx hardhat node
  STARTED=true
  for _ in $(seq 1 60); do ss -ltn | grep -q '127.0.0.1:8545' && break; sleep 1; done
fi
trap '$STARTED && systemctl --user stop dex-xmr-hardhat 2>/dev/null || true' EXIT

export MONZERO_PRIVATE_COIN=xmr MONZERO_XMR_BIN=$MONERO_DIR/linux-x64 MONZERO_EVM_RPC=http://127.0.0.1:8545
echo "== wallet level: lock, watch, rebuild after a power cut, sweep (Monero fakechain)"
cargo test --offline --test xmz_local -- --nocapture --test-threads=1
echo "== full swaps and refunds over real Monero wallets + the swap contract"
cargo test --offline --test driver_local -- --test-threads=1
echo "== all Monero swap tests passed"
