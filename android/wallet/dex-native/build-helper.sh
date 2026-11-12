#!/bin/bash
# Build the DEX trade helper (gui/dex-contracts/engine, the same source the desktop wallet runs)
# for Android. The result is an executable the wallet starts as a child process; Android only
# lets an app run programs from its native library folder, so it is packaged as
# jniLibs/<abi>/libmonzero_dex_helper.so.
#
#   build-helper.sh [x86_64] [arm64-v8a]      (default: both)
#
# Needs podman, the rust:1.95-bookworm image and an Android NDK (ANDROID_NDK, default
# ~/Android/Sdk/ndk/27.2.12479018). CARGO_REGISTRY may point at a cargo home to reuse downloads.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../../.." && pwd)
NDK=${ANDROID_NDK:-$HOME/Android/Sdk/ndk/27.2.12479018}
IMAGE=${RUST_IMAGE:-docker.io/library/rust:1.95-bookworm}
API=21
[ -d "$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin" ] || { echo "!! no NDK at $NDK"; exit 1; }
ABIS=("$@"); [ ${#ABIS[@]} -gt 0 ] || ABIS=(x86_64 arm64-v8a)
CACHE=${CARGO_REGISTRY:-$REPO/gui/dex-contracts/engine/target/android-cargo-home}
mkdir -p "$CACHE"

for abi in "${ABIS[@]}"; do
  case "$abi" in
    x86_64) triple=x86_64-linux-android ;;
    arm64-v8a) triple=aarch64-linux-android ;;
    *) echo "!! unknown ABI $abi"; exit 1 ;;
  esac
  under=${triple//-/_}; upper=$(echo "$under" | tr a-z A-Z)
  bin=/ndk/toolchains/llvm/prebuilt/linux-x86_64/bin
  podman run --rm --network host \
    -v "$REPO/gui/dex-contracts:/src" -v "$NDK:/ndk:ro" -v "$CACHE:/cargo-home" \
    -e CARGO_HOME=/cargo-home -e CARGO_TARGET_DIR=/src/engine/target/android \
    -e CARGO_INCREMENTAL=0 -e CARGO_HTTP_LOW_SPEED_LIMIT=0 \
    -e ANDROID_NDK_ROOT=/ndk -e ANDROID_NDK_HOME=/ndk \
    -e "CARGO_TARGET_${upper}_LINKER=$bin/${triple}${API}-clang" \
    -e "CC_${under}=$bin/${triple}${API}-clang" -e "CXX_${under}=$bin/${triple}${API}-clang++" \
    -e "AR_${under}=$bin/llvm-ar" -e "RANLIB_${under}=$bin/llvm-ranlib" \
    -w /src/engine "$IMAGE" bash -euc "
      command -v cmake >/dev/null || { apt-get update -qq && apt-get install -y -qq cmake >/dev/null; }
      rustup target add $triple >/dev/null
      cargo build --release ${CARGO_LOCKED---locked} --target $triple --bin monzero-dex-evm-helper
    "
  out="$HERE/../app/src/main/jniLibs/$abi"
  mkdir -p "$out"
  "$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-strip" -o "$out/libmonzero_dex_helper.so" \
    "$REPO/gui/dex-contracts/engine/target/android/$triple/release/monzero-dex-evm-helper"
  sha256sum "$out/libmonzero_dex_helper.so"
done
