#!/bin/bash
# Make external-libs/monero for the Android wallet build: a copy of this repo's core/ (Monzero
# v1.0.0) with the Monerujo patches in ../core-patches applied. Run from anywhere; re-run to refresh.
# Needs the core submodules:  git submodule update --init --recursive
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
CORE=$(cd "$HERE/../../../core" && pwd)
[ -e "$CORE/external/randomx/CMakeLists.txt" ] || { echo "!! core submodules missing: run git submodule update --init --recursive"; exit 1; }
rm -rf "$HERE/monero"
rsync -a --exclude build/ "$CORE/" "$HERE/monero/"
for p in "$HERE"/../core-patches/*.patch; do
  # patch, not git apply: inside this repo git apply silently skips paths outside the current folder
  patch -d "$HERE/monero" -p1 --forward --quiet < "$p"
  echo "applied $(basename "$p")"
done
[ -f "$HERE/monero/src/device/device_sidekick.cpp" ] || { echo "!! patches did not apply"; exit 1; }
echo "external-libs/monero ready; next: cd external-libs && make (see the top-level README)"
