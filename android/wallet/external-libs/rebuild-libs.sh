#!/bin/bash
# Rebuild the Android wallet native libs (arm64-v8a, x86_64) from ../../monzero-core with podman.
set -euo pipefail
cd "$(dirname "$0")"
for pair in "android64.Dockerfile monzero-android-arm64 arm64-v8a" "android64_x86.Dockerfile monzero-android-x86_64 x86_64"; do
  set -- $pair
  echo "== $3"
  podman build -f $1 -t $2 --build-arg NPROC=16 monero/ > build-$3-rings.log 2>&1 || { echo "BUILD FAILED $3"; tail -20 build-$3-rings.log; exit 1; }
  podman rm -f $2-out >/dev/null 2>&1 || true
  podman create --name $2-out $2 bash >/dev/null
  rm -rf $3.new && mkdir -p $3.new/monero
  podman cp $2-out:/opt/android/prefix/lib/. $3.new/
  podman cp $2-out:/src/build/release/lib/. $3.new/monero
  podman rm -f $2-out >/dev/null
  rm -rf $3.old && mv $3 $3.old && mv $3.new $3
  ls -la $3/monero/libwallet.a
done
echo "MONZERO wallet-android monzero/main with monzero android/wallet-lib $(git -C monero rev-parse --short=9 HEAD)" > VERSION
echo LIBS-DONE
