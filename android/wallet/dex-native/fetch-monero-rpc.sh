#!/bin/bash
# Monero's own wallet RPC for the phone's Monero pairs (BNB/XMR, SOL/XMR, USDT/XMR, ...): taken
# unchanged from Monero's official Android release, as the desktop wallet takes Monero's official
# Linux and Windows releases. Nothing is built here.
#
#   fetch-monero-rpc.sh [download folder]
#
# 1. Downloads monero-android-armv8-<version>.tar.bz2 and hashes.txt from downloads.getmonero.org
#    (or uses them from the download folder, default ~/projects/Monzero-App/core/build/monero-official/dl)
# 2. Checks hashes.txt's signature: binaryfate's key (Monero's release signer), fingerprint below
# 3. Checks the archive's SHA-256 against hashes.txt
# 4. Copies monero-wallet-rpc, unchanged, to jniLibs/arm64-v8a/libmonero_wallet_rpc.so
#    (Android runs programs only from the app's native library folder, hence the name) and checks
#    that it loads on 16 KB page phones (LOAD alignment at least 16384)
#
# Monero publishes Android builds for armv7 and armv8 only: the x86_64 emulator gets no Monero
# program, and the wallet then simply offers no Monero pairs there.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
VERSION=v0.18.5.4
DL=${1:-$HOME/projects/Monzero-App/core/build/monero-official/dl}
ARCHIVE=monero-android-armv8-$VERSION.tar.bz2
# binaryfate (Monero release signing key)
FPR=81AC591FE9C4B65C5806AFC3F0AF4D462A0BDF92
OUT=$HERE/../app/src/main/jniLibs/arm64-v8a/libmonero_wallet_rpc.so

mkdir -p "$DL"
cd "$DL"
[ -f hashes.txt ] || curl -fsSL --retry 3 -o hashes.txt https://www.getmonero.org/downloads/hashes.txt
[ -f binaryfate.asc ] || curl -fsSL --retry 3 -o binaryfate.asc https://raw.githubusercontent.com/monero-project/monero/master/utils/gpg_keys/binaryfate.asc
[ -f "$ARCHIVE" ] || curl -fSL --retry 3 -o "$ARCHIVE" "https://downloads.getmonero.org/cli/$ARCHIVE"

G=$(mktemp -d); trap 'rm -rf "$G"' EXIT
GNUPGHOME=$G gpg -q --import binaryfate.asc 2>/dev/null
GNUPGHOME=$G gpg --status-fd 1 --verify hashes.txt 2>/dev/null | grep -q "VALIDSIG $FPR" \
  || { echo "!! hashes.txt is not signed by binaryfate ($FPR)"; exit 1; }
want=$(grep " $ARCHIVE\$" hashes.txt | cut -d' ' -f1)
[ -n "$want" ] && [ "$(sha256sum "$ARCHIVE" | cut -d' ' -f1)" = "$want" ] || { echo "!! $ARCHIVE does not match hashes.txt"; exit 1; }
echo "signed checksums ok: $ARCHIVE"

tar -xjf "$ARCHIVE" -C "$G" --wildcards '*/monero-wallet-rpc'
rpc=$(find "$G" -name monero-wallet-rpc -type f)
file "$rpc" | grep -q "ARM aarch64" || { echo "!! not an arm64 program"; exit 1; }
# Every LOAD segment aligned for 16 KB pages (Android 15+ phones with 16 KB pages refuse 4 KB ones)
if readelf -lW "$rpc" | awk '$1=="LOAD"{print $NF}' | while read -r a; do [ $((a)) -ge 16384 ] || exit 1; done; then :; else
  echo "!! monero-wallet-rpc has a LOAD alignment under 16 KB"; exit 1; fi
mkdir -p "$(dirname "$OUT")"
install -m 0755 "$rpc" "$OUT"
echo "$(sha256sum "$OUT" | cut -d' ' -f1)  libmonero_wallet_rpc.so ($VERSION, official, unchanged)"
