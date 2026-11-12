#!/bin/bash
# Build monzero-wallet-rpc for Android: the XMZ wallet service the DEX trade helper talks to (the
# desktop wallet runs the same program). It is built inside the image that external-libs/Makefile
# makes for the wallet's own native libraries (same core source, same dependencies), then packaged
# as jniLibs/<abi>/libmonzero_wallet_rpc.so, the only place Android lets an app run a program from.
#
#   build-wallet-rpc.sh [x86_64] [arm64-v8a]      (default: both)
#
# Needs podman and the images monzero-android-x86_64 / monzero-android-arm64 (cd external-libs && make).
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
ABIS=("$@"); [ ${#ABIS[@]} -gt 0 ] || ABIS=(x86_64 arm64-v8a)
for abi in "${ABIS[@]}"; do
  case "$abi" in
    x86_64) image=localhost/monzero-android-x86_64; triple=x86_64-linux-android ;;
    arm64-v8a) image=localhost/monzero-android-arm64; triple=aarch64-linux-android ;;
    *) echo "!! unknown ABI $abi"; exit 1 ;;
  esac
  out="$HERE/../app/src/main/jniLibs/$abi"; mkdir -p "$out"
  podman run --rm -v "$out:/out" "$image" bash -euc '
    cd /src/build/release
    # The targets the wallet library build left out; the last link needs two fixes (below)
    PATH=${HOST_PATH:-$PATH} make -j'"${NPROC:-$(nproc)}"' wallet_rpc_server >/tmp/build.log 2>&1 || true
    cd src/wallet
    # The app gives the wallet library its hardware-wallet transports through JNI; a stand-alone
    # program has none
    cat > /tmp/stub.c <<STUB
int LedgerExchange(unsigned char *c, unsigned int cl, unsigned char *r, unsigned int rl) { (void)c;(void)cl;(void)r;(void)rl; return -1; }
int BtExchange(unsigned char *c, unsigned int cl, unsigned char *r, unsigned int rl) { (void)c;(void)cl;(void)r;(void)rl; return -1; }
STUB
    '"$triple"'-clang -fPIC -c /tmp/stub.c -o /tmp/stub.o
    # zstd is optional and not built for Android: its "not found" marker must not reach the linker
    sed -e "s/ -lZSTD_LIBRARY-NOTFOUND//" -e "s|\$| /tmp/stub.o|" CMakeFiles/wallet_rpc_server.dir/link.txt > /tmp/link.sh
    bash /tmp/link.sh || { tail -40 /tmp/build.log; exit 1; }
    '"$triple"'-strip -o /out/libmonzero_wallet_rpc.so ../../bin/monzero-wallet-rpc
  '
  sha256sum "$out/libmonzero_wallet_rpc.so"
done
