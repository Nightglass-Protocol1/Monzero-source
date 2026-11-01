#!/bin/bash
# Prepares and builds the Monzero web wallet (see DESIGN.md).
#   web-wallet/build.sh prepare   pinned monero-ts + monero-cpp, Monzero core in place of Monero, patches
#   web-wallet/build.sh build     the WebAssembly wallet and bundle (needs Emscripten 3.1.66 active)
# Work happens in $WEB_WALLET_WORK (default ../web-wallet-build next to this repository).
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/.." && pwd)
WORK=${WEB_WALLET_WORK:-$(cd "$REPO/.." && pwd)/web-wallet-build}
MONERO_TS_URL=https://github.com/woodser/monero-ts.git
MONERO_TS_COMMIT=d2ce5bf7c42969095769006bdd1b9c08a41df576   # 2026-10-23
MONERO_CPP_COMMIT=65e5356a9bb8ae487b3feba79f8c07624a5c76d0  # 2026-10-22 (monero-ts submodule pin)
EMSCRIPTEN_VERSION=3.1.66

prepare() {
  mkdir -p "$WORK"
  local ts="$WORK/monero-ts"
  if [ ! -d "$ts/.git" ]; then
    git clone -q "$MONERO_TS_URL" "$ts"
  fi
  git -C "$ts" fetch -q origin "$MONERO_TS_COMMIT" 2>/dev/null || true
  git -C "$ts" checkout -q --detach "$MONERO_TS_COMMIT"
  git -C "$ts" submodule update --init --depth 1 external/monero-cpp
  [ "$(git -C "$ts/external/monero-cpp" rev-parse HEAD)" = "$MONERO_CPP_COMMIT" ] || { echo "monero-cpp is not the pinned commit"; exit 1; }
  # Monzero changes to the wrappers, each reviewable in web-wallet/patches
  git -C "$ts/external/monero-cpp" checkout -q -- src
  git -C "$ts" checkout -q -- src CMakeLists.txt
  git -C "$ts/external/monero-cpp" apply "$HERE/patches/monero-cpp-0001-monzero-amounts-and-uri.patch"
  git -C "$ts/external/monero-cpp" apply "$HERE/patches/monero-cpp-0002-monzero-wallet2-stop.patch"
  git -C "$ts" apply "$HERE/patches/monero-ts-0001-monzero-amounts.patch"
  git -C "$ts" apply "$HERE/patches/monero-ts-0002-monzero-wallet-sources.patch"
  # Declarations Monzero's sources include but the browser never uses (see shims/)
  rm -rf "$ts/monzero-shims"; cp -r "$HERE/shims" "$ts/monzero-shims"
  # Monzero's own core replaces Monero's source: addresses, networks, forks and amounts come from it
  local project="$ts/external/monero-cpp/external/monero-project"
  rm -rf "$project"
  ln -s "$REPO/core" "$project"
  echo "prepared $ts (monero-ts $MONERO_TS_COMMIT, monero-cpp $MONERO_CPP_COMMIT, core $(git -C "$REPO" rev-parse --short HEAD))"
}

build() {
  command -v emcc >/dev/null || { echo "Emscripten is not active: source <emsdk>/emsdk_env.sh (version $EMSCRIPTEN_VERSION)"; exit 1; }
  local v; v=$(emcc --version | head -1 | grep -oE '[0-9]+\.[0-9]+\.[0-9]+' | head -1)
  [ "$v" = "$EMSCRIPTEN_VERSION" ] || { echo "Emscripten $v is active; the web wallet is built only with $EMSCRIPTEN_VERSION"; exit 1; }
  cd "$WORK/monero-ts"
  ./bin/build_all.sh
}

case "${1:-}" in
  prepare) prepare ;;
  build) build ;;
  *) echo "usage: $0 prepare|build"; exit 1 ;;
esac
