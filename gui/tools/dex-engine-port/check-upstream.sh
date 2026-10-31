#!/usr/bin/env bash
# Local investigation only; does not start wallets, daemons or a trading service.
set -euo pipefail

mode=${1:-check}
package=swap-core
case "$mode" in
  check|test) ;;
  machine-test) mode=test; package=swap-machine ;;
  *) echo 'Usage: bash check-upstream.sh [check|test|machine-test]' >&2; exit 2 ;;
esac
if (( $# > 1 )); then
  echo 'Unexpected arguments' >&2
  exit 2
fi

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
# Worktrees outside core/ point these at the shared toolchain and a private engine copy.
core_dir=${DEX_CORE_DIR:-$(cd -- "$script_dir/../../.." && pwd)}
engine_dir=${DEX_ENGINE_DIR:-"$core_dir/build/eigenwallet-engine-port"}
sysroot="$core_dir/build/dex-rust-toolchain-1.95.0/sysroot"
expected=0e17c7f7cd8f0657af176c8852aa4c9949586051
actual=$(git -C "$engine_dir" rev-parse HEAD)
if [[ "$actual" != "$expected" ]]; then
  echo 'Unexpected upstream revision; review the pin before building' >&2
  exit 1
fi
for tool in cargo rustc; do
  [[ -x "$sysroot/usr/bin/$tool" ]] || { echo "Missing local $tool" >&2; exit 1; }
done
available_kib=$(df -Pk "$engine_dir" | awk 'NR == 2 {print $4}')
if [[ ! "$available_kib" =~ ^[0-9]+$ ]] || (( available_kib < 3145728 )); then
  echo 'At least 3 GiB free workspace space is required before starting' >&2
  exit 1
fi

cd -- "$engine_dir"
args=("$mode" -p "$package" --locked -j 2)
[[ "$mode" != test ]] || args+=(--lib)
exec env LD_LIBRARY_PATH="$sysroot/usr/lib/x86_64-linux-gnu" \
  RUSTC="$sysroot/usr/bin/rustc" CARGO_HOME="$core_dir/build/dex-cargo-home" \
  CARGO_INCREMENTAL=0 CARGO_PROFILE_DEV_DEBUG=0 \
  "$sysroot/usr/bin/cargo" "${args[@]}"
