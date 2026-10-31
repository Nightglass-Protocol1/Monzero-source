#!/usr/bin/env bash
# Independent verification of the Monzero BTC/XMZ DEX settlement engine.
# Run this from anywhere inside the monzero-gui repository:
#
#   bash tools/dex-engine-port/verify-dex-engine.sh
#
# It runs, in order:
#   1. A toolchain sanity check (the pinned local Rust 1.95.0 build used for
#      all engine work -- see README.md's "Reproduce with that project-local
#      compiler" section for what it is and is not).
#   2. The full swap-machine unit/integration test suite (currently 94 tests)
#      against the pinned upstream revision.
#   3. Ordered reconstruction of every patch in this directory against a
#      fresh index of that pinned revision, proving the accumulated engine
#      sources are exactly and only what the numbered patches produce --
#      zero undocumented drift.
#
# This verifies the engine (src/native_funding_rpc/*, native_trade_quote.rs,
# etc.) that the GUI's DexEngine links against. It does not build or run the
# Qt/QML GUI itself, and it does not open a wallet, start a daemon, or touch
# any live network: everything here runs against disposable regtest/fakechain
# fixtures or, for the Rust suite below, entirely in-process.
#
# Exit code is nonzero if either the test suite or the patch reconstruction
# fails.
set -euo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)

echo "=== 1/3: toolchain check ==="
bash "$script_dir/check-upstream.sh" check
echo

echo "=== 2/3: swap-machine test suite ==="
bash "$script_dir/check-upstream.sh" machine-test
echo

echo "=== 3/3: ordered patch-chain reconstruction (zero-drift proof) ==="
bash "$script_dir/verify-upstream-patches.sh"
echo

echo "ALL CHECKS PASSED"
