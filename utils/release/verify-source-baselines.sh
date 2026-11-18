#!/usr/bin/env bash
set -euo pipefail

core_root=$(git rev-parse --show-toplevel)
bundle_dir=${1:-/home/pinhead/Projects/Monzero-Fork-backups/source-baselines-20260815}
gui_root=${2:-"$core_root/monzero-gui"}

core_commit=28f1919249465d3230f45ed21686c5a836d56df0
core_tag=monzero-phase0-assets-prototype-20260815
gui_commit=cb8d4378527e9816abfadb9c0dbc9259a1ea1385
gui_tag=monzero-gui-phase0-20260815-r1
core_bundle="$bundle_dir/monzero-core-phase0.bundle"
gui_bundle="$bundle_dir/monzero-gui-phase0.bundle"

fail() {
  echo "Source baseline verification failed: $*" >&2
  exit 1
}

[[ -d "$gui_root/.git" ]] || fail "GUI repository not found at $gui_root"
[[ -f "$core_bundle" ]] || fail "core bundle not found at $core_bundle"
[[ -f "$gui_bundle" ]] || fail "GUI bundle not found at $gui_bundle"

[[ $(git -C "$core_root" rev-parse "$core_tag^{commit}") == "$core_commit" ]] ||
  fail "core tag does not resolve to the recorded commit"
[[ $(git -C "$gui_root" rev-parse "$gui_tag^{commit}") == "$gui_commit" ]] ||
  fail "GUI tag does not resolve to the recorded commit"
[[ $(git -C "$gui_root" ls-tree "$gui_commit" monero | awk '{print $3}') == "$core_commit" ]] ||
  fail "GUI revision does not pin the recorded core commit"

(
  cd "$bundle_dir"
  sha256sum -c SHA256SUMS
)

git -C "$core_root" bundle verify "$core_bundle" >/dev/null
git -C "$gui_root" bundle verify "$gui_bundle" >/dev/null

echo "Core: $core_commit ($core_tag)"
echo "GUI:  $gui_commit ($gui_tag)"
echo "Source baseline verification passed"
