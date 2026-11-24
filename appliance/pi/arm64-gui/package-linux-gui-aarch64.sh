#!/usr/bin/env bash
# Packages the Linux graphical wallet with the node, the command-line tools and the DEX helper
# programs the wallet runs: monzero-<version>-linux-gui-aarch64.tar.gz (deterministic archive).
set -euo pipefail

usage() {
  echo "Usage: $0 <gui-build-bin-dir> <output-dir> <version>" >&2
  echo "Set ALLOW_DIRTY=1 only for an explicitly marked development artifact." >&2
  echo "MONERO_RELEASE=<monero-linux-x64-vX.tar.bz2> with MONERO_HASHES=<its signed hashes.txt> bundles" >&2
  echo "Monero's own monero-wallet-rpc (for XMR trading), byte for byte as published." >&2
  exit 2
}
[[ $# -eq 3 ]] || usage

source_root=$(git rev-parse --show-toplevel)
core="$source_root/core"
build_bin=$(realpath "$1")
output_dir=$(realpath -m "$2")
version=$3

[[ $version =~ ^v[0-9]+\.[0-9]+\.[0-9]+$ ]] || { echo "Invalid version label" >&2; exit 2; }
grep -Fqx "Release label: $version" "$core/docs/RELEASE_NOTES.md" || {
  echo "RELEASE_NOTES.md does not match release label: $version" >&2; exit 1;
}
binaries=(monzero-wallet-gui monzerod monzero-wallet-cli monzero-wallet-rpc)
helpers=(monzero-dex/monzero-dex-evm-helper)   # the BTC/LTC helpers are not built for ARM (those pairs are not public)
for f in "${binaries[@]}" "${helpers[@]}"; do
  [[ -x "$build_bin/$f" ]] || { echo "Missing executable: $build_bin/$f" >&2; exit 2; }
done

source_commit=$(git -C "$source_root" rev-parse HEAD)
source_short=${source_commit:0:9}
daemon_version=$("$build_bin/monzerod" --version | head -n 1)
for binary in monzero-wallet-cli monzero-wallet-rpc; do
  [[ $("$build_bin/$binary" --version | head -n 1) == "$daemon_version" ]] || {
    echo "Refusing to package binaries with inconsistent versions: $binary" >&2; exit 1;
  }
done
# A build of the tagged release commit names itself "<version>-release" (the tag must point at it)
tag_commit=$(git -C "$source_root" rev-parse -q --verify "refs/tags/$version^{commit}" || true)
for binary in "${binaries[@]}"; do
  { strings "$build_bin/$binary" | grep -F "${version#v}-$source_short" >/dev/null ||
    { [[ $tag_commit == "$source_commit" ]] && strings "$build_bin/$binary" | grep -F "${version#v}-release" >/dev/null; }; } || {
    echo "Binary version does not match source commit ${version#v}-$source_short: $binary" >&2; exit 1;
  }
done
# Nothing in the package may need more than a plain 64-bit PC or a library the system may lack
for f in "${binaries[@]}" "${helpers[@]}"; do
  if readelf -n "$build_bin/$f" | grep -q 'x86-64-v[234]'; then
    echo "Built for a newer CPU level than plain x86-64: $f" >&2; exit 1
  fi
done

source_epoch=${SOURCE_DATE_EPOCH:-$(git -C "$source_root" show -s --format=%ct "$source_commit")}
dirty=false
if [[ -n $(git -C "$source_root" status --porcelain --untracked-files=normal) ]]; then
  dirty=true
  [[ ${ALLOW_DIRTY:-0} == 1 ]] || { echo "Refusing to package a dirty source tree. Commit first." >&2; exit 1; }
  version="${version}-development-dirty"
fi

package_name="monzero-${version}-linux-gui-aarch64"
mkdir -p "$output_dir"
work_dir=$(mktemp -d "${TMPDIR:-/tmp}/monzero-package.XXXXXX")
trap 'rm -rf -- "$work_dir"' EXIT
package_dir="$work_dir/$package_name"
mkdir -p "$package_dir/monzero-dex" "$package_dir/monzero-light"

for f in "${binaries[@]}" "${helpers[@]}"; do
  install -m 0755 "$build_bin/$f" "$package_dir/$f"
  "${STRIP:-strip}" --strip-all "$package_dir/$f"
done
# The wallet runs a helper only if its digest is the one compiled in: stripping must not change it
for f in "${helpers[@]}"; do
  cmp -s "$build_bin/$f" "$package_dir/$f" || { echo "Helper changed when stripped (bundle it already stripped): $f" >&2; exit 1; }
done

for f in start-monzerod.sh start-monzero-wallet-cli.sh start-monzero-miner.sh stop-monzero-miner.sh; do
  install -m 0755 "$core/$f" "$package_dir/$f"
done
install -m 0644 "$core/LICENSE" "$package_dir/LICENSE"
install -m 0644 "$core/README.md" "$package_dir/README.md"
install -m 0644 "$core/WHITEPAPER.md" "$package_dir/WHITEPAPER.md"
install -m 0644 "$core/MONZERO_CHAIN_SPEC.md" "$package_dir/MONZERO_CHAIN_SPEC.md"
install -m 0644 "$core/docs/RELEASE_NOTES.md" "$package_dir/RELEASE_NOTES.md"
install -m 0644 "$core/docs/UPGRADE.md" "$package_dir/UPGRADE.md"
install -m 0644 "$core/utils/systemd/README.md" "$package_dir/README-SYSTEMD.md"
install -m 0644 "$core/utils/systemd/monzerod.service" "$package_dir/monzerod.service"
install -m 0755 "$core/utils/release/verify-network-readiness.sh" "$package_dir/verify-network-readiness.sh"
install -m 0644 "$core/utils/release/linux-gui/START-HERE.txt" "$package_dir/START-HERE.txt"
install -m 0755 "$core/utils/release/linux-gui/install-desktop-entry.sh" "$package_dir/install-desktop-entry.sh"
install -m 0644 "$source_root/gui/images/appicons/256x256.png" "$package_dir/monzero.png"

# XMR trading: Monero's official wallet RPC, unchanged (the wallet hashes it before each start).
# Only from an archive whose SHA-256 is in Monero's hash list (its signature checked beforehand).
monero_rpc=none
if [[ -n ${MONERO_RELEASE:-} ]]; then
  hashes=${MONERO_HASHES:?MONERO_HASHES (the signed Monero hashes.txt) is needed with MONERO_RELEASE}
  archive_sha=$(sha256sum "$MONERO_RELEASE" | cut -d' ' -f1)
  grep -q "^$archive_sha  $(basename "$MONERO_RELEASE")$" "$hashes" || { echo "Monero archive not in the hash list: $MONERO_RELEASE" >&2; exit 1; }
  monero_dir="$work_dir/monero"; mkdir -p "$monero_dir"
  tar -xjf "$MONERO_RELEASE" -C "$monero_dir" --strip-components=1 --wildcards '*/monero-wallet-rpc' '*/LICENSE'
  install -m 0755 "$monero_dir/monero-wallet-rpc" "$package_dir/monero-wallet-rpc"
  install -m 0644 "$monero_dir/LICENSE" "$package_dir/LICENSE-MONERO"
  monero_rpc="$(basename "$MONERO_RELEASE") sha256=$archive_sha"
fi

{
  echo "package=$package_name"
  echo "source_commit=$source_commit"
  echo "source_date_epoch=$source_epoch"
  echo "source_tree_dirty=$dirty"
  echo "binary_version=$daemon_version"
  echo "binaries_stripped=true"
  echo "dex_edition=public"
  echo "binary_build_reproducibility=${BINARY_BUILD_REPRODUCIBILITY:-unverified}"
  echo "assets_consensus_enabled=false"
  echo "native_ticker=XMZ"
  echo "monero_wallet_rpc=$monero_rpc"
  echo "linking=dynamic (Debian 13 / Raspberry Pi OS trixie: Qt 5.15, Boost 1.83 from the system)"
} > "$package_dir/BUILD-MANIFEST.txt"

(
  cd "$package_dir"
  find . -type f ! -name SHA256SUMS -printf '%P\n' | LC_ALL=C sort | xargs -r sha256sum > SHA256SUMS
  sha256sum --quiet -c SHA256SUMS
)

archive="$output_dir/$package_name.tar.gz"
tar --sort=name --mtime="@$source_epoch" --owner=0 --group=0 --numeric-owner \
  --pax-option=delete=atime,delete=ctime -C "$work_dir" -cf - "$package_name" | gzip -n -9 > "$archive"
(cd "$output_dir" && sha256sum "$(basename "$archive")" > "$(basename "$archive").sha256")
echo "$archive"
echo "$archive.sha256"
