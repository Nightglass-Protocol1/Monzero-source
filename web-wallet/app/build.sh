#!/bin/bash
# Builds the web wallet page for one network into app/dist/<network>/ with SHA256SUMS.
#   app/build.sh testnet|mainnet
set -euo pipefail
NET=${1:?testnet or mainnet}; [ "$NET" = testnet ] || [ "$NET" = mainnet ] || { echo "testnet or mainnet"; exit 1; }
HERE=$(cd "$(dirname "$0")" && pwd)
export MONERO_TS=${MONERO_TS:-$(cd "$HERE/../../.." && pwd)/web-wallet-build/monero-ts}
[ -f "$MONERO_TS/dist/monero.worker.js" ] || { echo "build the wallet first: web-wallet/build.sh build"; exit 1; }
export NETWORK=$NET RELEASE="$(git -C "$HERE" rev-parse --short HEAD)$(git -C "$HERE" diff --quiet -- . || echo -dirty)"
export OUT_DIR="$HERE/dist/$NET"
rm -rf "$OUT_DIR"; mkdir -p "$OUT_DIR"
node "$MONERO_TS/node_modules/webpack-cli/bin/cli.js" --config "$HERE/webpack.config.cjs"
cp "$HERE/src/index.html" "$HERE/src/style.css" "$MONERO_TS/dist/monero.worker.js" "$OUT_DIR/"
(cd "$OUT_DIR" && sha256sum index.html style.css app.js monero.worker.js > SHA256SUMS)
echo "built $OUT_DIR (release $RELEASE)"; cat "$OUT_DIR/SHA256SUMS"
