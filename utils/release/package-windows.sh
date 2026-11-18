#!/usr/bin/env bash
set -euo pipefail

[[ $# -eq 3 ]] || { echo "Usage: $0 <build-bin-dir> <output-dir> <version>" >&2; exit 2; }

source_root=$(git rev-parse --show-toplevel)
build_bin=$(realpath "$1")
output_dir=$(realpath -m "$2")
version=$3
[[ $version =~ ^[0-9A-Za-z][0-9A-Za-z._-]*$ ]] || { echo "Invalid version label" >&2; exit 2; }
grep -Fqx "Release label: $version" "$source_root/docs/RELEASE_NOTES.md" || {
  echo "RELEASE_NOTES.md does not match release label: $version" >&2; exit 1;
}

for binary in monzerod.exe monzero-wallet-cli.exe monzero-wallet-rpc.exe; do
  [[ -f "$build_bin/$binary" ]] || { echo "Missing executable: $build_bin/$binary" >&2; exit 1; }
done

source_commit=$(git -C "$source_root" rev-parse HEAD)
source_short=${source_commit:0:9}
source_epoch=${SOURCE_DATE_EPOCH:-$(git -C "$source_root" show -s --format=%ct "$source_commit")}
[[ $source_epoch =~ ^[0-9]+$ ]] || { echo "SOURCE_DATE_EPOCH must be an integer" >&2; exit 2; }
build_reproducibility=${BINARY_BUILD_REPRODUCIBILITY:-unverified}
[[ $build_reproducibility == unverified || $build_reproducibility == verified ]] || {
  echo "BINARY_BUILD_REPRODUCIBILITY must be 'unverified' or 'verified'" >&2; exit 2;
}

dirty=false
if [[ -n $(git -C "$source_root" status --porcelain --untracked-files=normal) ]]; then
  dirty=true
  [[ ${ALLOW_DIRTY:-0} == 1 ]] || { echo "Refusing to package a dirty source tree" >&2; exit 1; }
  version="${version}-development-dirty"
fi

for binary in monzerod.exe monzero-wallet-cli.exe monzero-wallet-rpc.exe; do
  strings "$build_bin/$binary" | grep -F "1.0.0-$source_short" >/dev/null || {
    echo "Binary version does not match source commit: $binary" >&2; exit 1;
  }
done
binary_version="Monzero 'Genesis' (core v1.0.0-${source_short})"

package_name="monzero-${version}-windows-x64"
mkdir -p "$output_dir"
work_dir=$(mktemp -d "${TMPDIR:-/tmp}/monzero-windows-package.XXXXXX")
trap 'rm -rf -- "$work_dir"' EXIT
package_dir="$work_dir/$package_name"
mkdir -p "$package_dir"

for binary in monzerod.exe monzero-wallet-cli.exe monzero-wallet-rpc.exe; do
  install -m 0755 "$build_bin/$binary" "$package_dir/$binary"
done
strip_tool=${STRIP:-x86_64-w64-mingw32-strip}
command -v "$strip_tool" >/dev/null || { echo "Strip tool not found: $strip_tool" >&2; exit 1; }
SOURCE_DATE_EPOCH=$source_epoch "$strip_tool" --strip-all "$package_dir"/*.exe

for document in LICENSE README.md WHITEPAPER.md MONZERO_CHAIN_SPEC.md; do
  install -m 0644 "$source_root/$document" "$package_dir/$document"
done
for document in INDEPENDENT_REPRODUCTION.md RELEASE_CHECKLIST.md RELEASE_NOTES.md RELEASE_STATUS.md UPGRADE.md; do
  install -m 0644 "$source_root/docs/$document" "$package_dir/$document"
done
install -m 0644 "$source_root/utils/release/windows/README-WINDOWS.txt" "$package_dir/README-WINDOWS.txt"
for launcher in start-node.bat start-wallet-cli.bat start-mining.bat stop-mining.bat; do
  install -m 0644 "$source_root/utils/release/windows/$launcher" "$package_dir/$launcher"
done
install -m 0644 "$source_root/utils/release/windows/monzero-miner-reporter.ps1" "$package_dir/monzero-miner-reporter.ps1"

{
  echo "package=$package_name"
  echo "source_commit=$source_commit"
  echo "source_date_epoch=$source_epoch"
  echo "source_tree_dirty=$dirty"
  echo "binary_version=$binary_version"
  echo "binaries_stripped=true"
  echo "binary_build_reproducibility=$build_reproducibility"
  echo "assets_consensus_enabled=false"
  echo "native_ticker=XMZ"
} > "$package_dir/BUILD-MANIFEST.txt"

(
  cd "$package_dir"
  find . -maxdepth 1 -type f ! -name SHA256SUMS -printf '%P\n' | LC_ALL=C sort |
    xargs -r sha256sum > SHA256SUMS
  sha256sum -c SHA256SUMS
  TZ=UTC find . -type f -exec touch -d "@$source_epoch" {} +
)

archive="$output_dir/$package_name.zip"
(cd "$work_dir" && TZ=UTC find "$package_name" -type f -printf '%p\n' | LC_ALL=C sort | zip -X -9 -q "$archive" -@)
(cd "$output_dir" && sha256sum "$(basename "$archive")" > "$(basename "$archive").sha256")
echo "$archive"
echo "$archive.sha256"
