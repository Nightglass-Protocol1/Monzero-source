#!/bin/bash
# In an arm64 Debian 13 container: install only the wallet's runtime packages, package the build,
# then start the packaged wallet on a virtual screen (as the x86 smoke test does).
set -euo pipefail
export DEBIAN_FRONTEND=noninteractive
command -v bzip2 >/dev/null || apt-get update -qq
command -v bzip2 >/dev/null || apt-get install -y -qq --no-install-recommends ca-certificates git binutils file bzip2 xvfb xauth curl procps $(cat /w/runtime-packages.txt) > /w/runtime-apt.log 2>&1
git config --global --add safe.directory '*'
mkdir -p /w/dist && rm -f /w/dist/*
cd "$S/core"
MONERO_RELEASE=$M/monero-linux-armv8-v0.18.5.4.tar.bz2 MONERO_HASHES=$M/hashes.txt bash /w/package-linux-gui-aarch64.sh /w/build/bin /w/dist v1.3.8 | tail -2
echo "== smoke"
mkdir -p /t && tar -xzf /w/dist/monzero-v1.3.8-linux-gui-aarch64.tar.gz -C /t && cd /t/monzero-v1.3.8-linux-gui-aarch64
sha256sum --quiet -c SHA256SUMS && echo "checksums ok"
./monzerod --version; ./monzero-wallet-cli --version; ./monzero-wallet-rpc --version | head -1; ./monero-wallet-rpc --version
file monzero-wallet-gui | cut -c1-80
echo "missing libraries: $(for f in monzero-wallet-gui monzerod monzero-wallet-rpc monero-wallet-rpc monzero-dex/monzero-dex-evm-helper; do ldd ./$f; done | grep -c 'not found')"
export HOME=/root XDG_RUNTIME_DIR=/tmp
(xvfb-run -a -s "-screen 0 1280x800x24" ./monzero-wallet-gui > /tmp/app.log 2>&1 &)
sleep 90
echo "page bridge: $(curl -s -m 5 -H 'Origin: https://monzero.org' http://127.0.0.1:38471/v1/hello | cut -c1-70)"
grep -a 'Qt:5' /tmp/app.log | cut -c1-120
grep -aiE 'module .* is not installed|cannot|failed|error|abort|segfault' /tmp/app.log | grep -avi fontconfig | head -8 | cut -c1-200
pgrep -a monero-wallet-rpc | cut -c1-60 || true
pkill -f monzero-wallet-gui || true
echo SMOKE-DONE
