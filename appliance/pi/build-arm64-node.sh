#!/bin/bash
# Builds Monzero's node, CLI wallet and wallet RPC for 64-bit ARM Linux (Raspberry Pi) from a
# release's signed source tarball, by cross-compiling (no emulation needed), and packages them
# as the release artifact "linux-aarch64-node" (no graphical wallet yet).
#
#   bash appliance/pi/build-arm64-node.sh <version> [work dir]
#
# Checks the source tarball against the signed release file first (signature by the release key,
# then SHA-256). Output: <work>/dist/monzero-v<version>-linux-aarch64-node.tar.gz (+ .sha256) and
# the JSON entry to add to that release's artifacts before signing.
set -euo pipefail
V=${1:?usage: build-arm64-node.sh <version> [work dir]}
P=${MONZERO_ROOT:-$HOME/projects/Monzero-App}
W=${2:-$P/build/arm64-$V}
EV=$P/release-evidence/v$V; SRC=$P/build/v$V/dist/monzero-v$V-source.tar.gz
mkdir -p "$W"
G=$(mktemp -d); trap 'rm -rf "$G"' EXIT
GNUPGHOME=$G gpg -q --import "$P/release-evidence/v1.0.0/monzero-release-public.asc" 2>/dev/null
GNUPGHOME=$G gpg --status-fd 1 --verify "$EV/v$V.json.asc" "$EV/v$V.json" 2>/dev/null | grep -q 'VALIDSIG.* 4C37C618789F86D81FEF28DF390AEFB735F270D6$' || { echo "!! v$V.json is not signed by the release key"; exit 1; }
python3 -I -c 'import hashlib,json,sys; d=json.load(open(sys.argv[1])); assert hashlib.sha256(open(sys.argv[2],"rb").read()).hexdigest()==d["source"]["sha256"]; print("source tarball matches the signed release file")' "$EV/v$V.json" "$SRC"
[ -d "$W/src" ] || { mkdir "$W/src"; tar -xzf "$SRC" -C "$W/src" --strip-components=1; }
cat > "$W/build.sh" <<'IN'
#!/bin/bash
set -euo pipefail
apt-get update -qq
DEBIAN_FRONTEND=noninteractive apt-get install -y -qq --no-install-recommends build-essential g++-aarch64-linux-gnu gcc-aarch64-linux-gnu \
  cmake make pkg-config curl ca-certificates git python3 autoconf automake libtool gperf bison xz-utils bzip2 file >/dev/null
export TAR_OPTIONS=--no-same-owner     # rootless podman: keep the archives' owners out of it
cd /w/src/core
make -C contrib/depends HOST=aarch64-linux-gnu NO_QT=1 -j"$(nproc)" > /w/depends.log 2>&1
mkdir -p build/aarch64 && cd build/aarch64
cmake -DCMAKE_TOOLCHAIN_FILE=/w/src/core/contrib/depends/aarch64-linux-gnu/share/toolchain.cmake -DBUILD_TESTS=OFF -DCMAKE_BUILD_TYPE=Release -DSTATIC=ON -DARCH=armv8-a ../.. > /w/cmake.log 2>&1
make -j"$(nproc)" daemon simplewallet wallet_rpc_server > /w/make.log 2>&1
IN
podman run --rm -v "$W:/w" docker.io/library/debian:bookworm bash /w/build.sh
N=monzero-v$V-linux-aarch64-node; D=$W/dist; rm -rf "$D"; mkdir -p "$D/$N"
cp "$W"/src/core/build/aarch64/bin/{monzerod,monzero-wallet-cli,monzero-wallet-rpc} "$D/$N/"
printf 'Monzero v%s for 64-bit ARM Linux: monzerod, monzero-wallet-cli, monzero-wallet-rpc (no graphical wallet).\nBuilt from the signed v%s source tarball (depends HOST=aarch64-linux-gnu, Debian 12 cross compiler). Needs glibc 2.36+.\n' "$V" "$V" > "$D/$N/README.txt"
(cd "$D" && tar --owner=0 --group=0 --sort=name --mtime="2026-01-01 00:00Z" -czf "$N.tar.gz" "$N" && sha256sum "$N.tar.gz" > "$N.tar.gz.sha256")
python3 -I -c 'import json,os,sys; p=sys.argv[1]; print(json.dumps({"filename": os.path.basename(p), "size_bytes": os.path.getsize(p), "sha256": open(p+".sha256").read().split()[0], "platform": "linux-aarch64-node", "minimum_glibc": "2.36", "contains": "monzerod, command-line wallet, wallet RPC for 64-bit ARM (Raspberry Pi); no graphical wallet"}, indent=1))' "$D/$N.tar.gz"
