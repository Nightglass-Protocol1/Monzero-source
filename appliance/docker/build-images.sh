#!/bin/bash
# Builds the three Monzero Docker images for one signed release:
#   bash appliance/docker/build-images.sh 1.3.6        (docker, or ENGINE=podman)
# The release archive is fetched and checked inside the build by monzero-update (signed
# release file, pinned key, SHA-256); the relay is built from this checkout.
set -euo pipefail
VERSION=${1:?usage: build-images.sh <version>}
HERE=$(cd "$(dirname "$0")/.." && pwd); REPO=$(cd "$HERE/.." && pwd)
ENGINE=${ENGINE:-$(command -v docker >/dev/null && echo docker || echo podman)}
ROOT=${MONZERO_ROOT:-$HOME/projects/Monzero-App}
W=$(mktemp -d); trap 'rm -rf "$W"' EXIT
if [ -z "${RELAY_BIN:-}" ]; then
  T=$ROOT/core/build/dex-rust-toolchain-1.95.0/sysroot/usr
  ( cd "$REPO/gui/dex-contracts/relay" && PATH=$T/bin:$PATH LD_LIBRARY_PATH=$T/lib/x86_64-linux-gnu CARGO_HOME=$ROOT/core/build/dex-cargo-home cargo build --offline --release 2>&1 | tail -1 )
  RELAY_BIN=$REPO/gui/dex-contracts/relay/target/release/monzero-dex-relay
fi
mkdir -p "$W/rootfs/usr/local/sbin" "$W/docker"
cp "$HERE/rootfs/usr/local/sbin/monzero-update" "$W/rootfs/usr/local/sbin/"
cp "$HERE/docker/relay.json" "$HERE/docker/wallet-entry.sh" "$W/docker/"
cp "$RELAY_BIN" "$W/monzero-dex-relay"
gpg --dearmor < "$HERE/rootfs/usr/share/monzero/release-public.asc" > "$W/release-keyring.gpg"
for t in node relay wallet; do
  echo "== monzero/$t:$VERSION"
  $ENGINE build -q --target $t --build-arg MONZERO_VERSION="$VERSION" -t monzero/$t:$VERSION -t monzero/$t:latest -f "$HERE/docker/Containerfile" "$W" >/dev/null
done
$ENGINE images --filter reference='monzero/*' --format '{{.Repository}}:{{.Tag}} {{.Size}}'
