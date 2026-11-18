#!/usr/bin/env bash
set -euo pipefail

usage() {
  echo "Usage: $0 <build-bin-dir> <output-dir> <version>" >&2
  echo "Set ALLOW_DIRTY=1 only for an explicitly marked development artifact." >&2
  echo "Set BINARY_BUILD_REPRODUCIBILITY=verified only after an independent byte-for-byte build comparison." >&2
  exit 2
}

[[ $# -eq 3 ]] || usage

source_root=$(git rev-parse --show-toplevel)
build_bin=$(realpath "$1")
output_dir=$(realpath -m "$2")
version=$3

[[ $version =~ ^[0-9A-Za-z][0-9A-Za-z._-]*$ ]] || { echo "Invalid version label" >&2; exit 2; }
grep -Fqx "Release label: $version" "$source_root/docs/RELEASE_NOTES.md" || {
  echo "RELEASE_NOTES.md does not match release label: $version" >&2; exit 1;
}
[[ -x "$build_bin/monzerod" ]] || { echo "Missing executable: $build_bin/monzerod" >&2; exit 2; }
[[ -x "$build_bin/monzero-wallet-cli" ]] || { echo "Missing executable: $build_bin/monzero-wallet-cli" >&2; exit 2; }

source_commit=$(git -C "$source_root" rev-parse HEAD)
source_short=${source_commit:0:9}
daemon_version=$("$build_bin/monzerod" --version | head -n 1)
wallet_version=$("$build_bin/monzero-wallet-cli" --version | head -n 1)
[[ -n $daemon_version && $daemon_version == "$wallet_version" ]] || {
  echo "Refusing to package binaries with inconsistent versions:" >&2
  echo "  monzerod: $daemon_version" >&2
  echo "  wallet:   $wallet_version" >&2
  exit 1
}
if [[ -x "$build_bin/monzero-wallet-rpc" ]]; then
  rpc_version=$("$build_bin/monzero-wallet-rpc" --version | head -n 1)
  [[ $rpc_version == "$daemon_version" ]] || {
    echo "Refusing to package wallet RPC with inconsistent version: $rpc_version" >&2
    exit 1
  }
fi
for binary in monzerod monzero-wallet-cli monzero-wallet-rpc; do
  [[ ! -x "$build_bin/$binary" ]] ||
    strings "$build_bin/$binary" | grep -F "1.0.0-$source_short" >/dev/null || {
      echo "Binary version does not match source commit: $binary" >&2
      exit 1
    }
done

source_epoch=${SOURCE_DATE_EPOCH:-$(git -C "$source_root" show -s --format=%ct "$source_commit")}
[[ $source_epoch =~ ^[0-9]+$ ]] || { echo "SOURCE_DATE_EPOCH must be an integer" >&2; exit 2; }
build_reproducibility=${BINARY_BUILD_REPRODUCIBILITY:-unverified}
[[ $build_reproducibility == unverified || $build_reproducibility == verified ]] || {
  echo "BINARY_BUILD_REPRODUCIBILITY must be 'unverified' or 'verified'" >&2
  exit 2
}

dirty=false
if [[ -n $(git -C "$source_root" status --porcelain --untracked-files=normal) ]]; then
  dirty=true
  [[ ${ALLOW_DIRTY:-0} == 1 ]] || {
    echo "Refusing to package a dirty source tree. Commit the reviewed baseline first." >&2
    exit 1
  }
  version="${version}-development-dirty"
fi

package_name="monzero-${version}-linux-x86_64"
mkdir -p "$output_dir"
work_dir=$(mktemp -d "${TMPDIR:-/tmp}/monzero-package.XXXXXX")
trap 'rm -rf -- "$work_dir"' EXIT
package_dir="$work_dir/$package_name"
mkdir -p "$package_dir"

install -m 0755 "$build_bin/monzerod" "$package_dir/monzerod"
install -m 0755 "$build_bin/monzero-wallet-cli" "$package_dir/monzero-wallet-cli"
for optional in monzero-wallet-rpc monzero-gen-ssl-cert; do
  [[ -x "$build_bin/$optional" ]] && install -m 0755 "$build_bin/$optional" "$package_dir/$optional"
done

strip_tool=${STRIP:-strip}
command -v "$strip_tool" >/dev/null || { echo "Strip tool not found: $strip_tool" >&2; exit 1; }
"$strip_tool" --strip-all "$package_dir/monzerod" "$package_dir/monzero-wallet-cli"
[[ ! -x "$package_dir/monzero-wallet-rpc" ]] ||
  "$strip_tool" --strip-all "$package_dir/monzero-wallet-rpc"
[[ ! -x "$package_dir/monzero-gen-ssl-cert" ]] ||
  "$strip_tool" --strip-all "$package_dir/monzero-gen-ssl-cert"

install -m 0755 "$source_root/start-monzerod.sh" "$package_dir/start-monzerod.sh"
install -m 0755 "$source_root/start-monzero-wallet-cli.sh" "$package_dir/start-monzero-wallet-cli.sh"
install -m 0755 "$source_root/start-monzero-miner.sh" "$package_dir/start-monzero-miner.sh"
install -m 0755 "$source_root/stop-monzero-miner.sh" "$package_dir/stop-monzero-miner.sh"
install -m 0644 "$source_root/LICENSE" "$package_dir/LICENSE"
install -m 0644 "$source_root/README.md" "$package_dir/README.md"
install -m 0644 "$source_root/WHITEPAPER.md" "$package_dir/WHITEPAPER.md"
install -m 0644 "$source_root/MONZERO_CHAIN_SPEC.md" "$package_dir/MONZERO_CHAIN_SPEC.md"
install -m 0644 "$source_root/docs/RELEASE_CHECKLIST.md" "$package_dir/RELEASE_CHECKLIST.md"
install -m 0644 "$source_root/docs/RELEASE_STATUS.md" "$package_dir/RELEASE_STATUS.md"
install -m 0644 "$source_root/docs/RELEASE_NOTES.md" "$package_dir/RELEASE_NOTES.md"
install -m 0644 "$source_root/docs/INDEPENDENT_REPRODUCTION.md" "$package_dir/INDEPENDENT_REPRODUCTION.md"
install -m 0644 "$source_root/docs/UPGRADE.md" "$package_dir/UPGRADE.md"
install -m 0644 "$source_root/utils/systemd/README.md" "$package_dir/README-SYSTEMD.md"
install -m 0644 "$source_root/utils/systemd/monzerod.service" "$package_dir/monzerod.service"
install -m 0644 "$source_root/utils/systemd/monzero-readiness.service" "$package_dir/monzero-readiness.service"
install -m 0644 "$source_root/utils/systemd/monzero-readiness.timer" "$package_dir/monzero-readiness.timer"
install -m 0755 "$source_root/utils/release/verify-network-readiness.sh" "$package_dir/verify-network-readiness.sh"

{
  echo "package=$package_name"
  echo "source_commit=$source_commit"
  echo "source_date_epoch=$source_epoch"
  echo "source_tree_dirty=$dirty"
  echo "binary_version=$daemon_version"
  echo "binaries_stripped=true"
  echo "binary_build_reproducibility=$build_reproducibility"
  echo "assets_consensus_enabled=false"
  echo "native_ticker=XMZ"
} > "$package_dir/BUILD-MANIFEST.txt"

(
  cd "$package_dir"
  find . -maxdepth 1 -type f ! -name SHA256SUMS -printf '%P\n' \
    | LC_ALL=C sort \
    | xargs -r sha256sum > SHA256SUMS
  sha256sum -c SHA256SUMS
)

archive="$output_dir/$package_name.tar.gz"
tar --sort=name \
  --mtime="@$source_epoch" \
  --owner=0 --group=0 --numeric-owner \
  --pax-option=delete=atime,delete=ctime \
  -C "$work_dir" -cf - "$package_name" \
  | gzip -n -9 > "$archive"
(cd "$output_dir" && sha256sum "$(basename "$archive")" > "$(basename "$archive").sha256")
echo "$archive"
echo "$archive.sha256"
