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
# app.js.LICENSE.txt carries the bundled libraries' licence notices and ships with the release
(cd "$OUT_DIR" && sha256sum index.html style.css app.js app.js.LICENSE.txt monero.worker.js > SHA256SUMS)
# The release page: this release's id and hashes, how to check the signature and the files
ORIGIN=$([ "$NET" = mainnet ] && echo https://wallet.monzero.org || echo https://testnet-wallet.monzero.org)
python3 - "$HERE/src/verify.html" "$OUT_DIR" "$RELEASE" "$NET" "$ORIGIN" <<'PY'
import sys, html, datetime
src, out, release, net, origin = sys.argv[1:]
sums = open(out + "/SHA256SUMS").read().strip()
page = open(src).read()
for k, v in {"@@RELEASE@@": release, "@@NETWORK@@": net, "@@NETBADGE@@": "" if net == "mainnet" else net.upper(),
             "@@ORIGIN@@": origin, "@@SUMS@@": sums, "@@BUILT@@": datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%d")}.items():
    page = page.replace(k, html.escape(v))
assert "@@" not in page
open(out + "/verify.html", "w").write(page)
PY
echo "built $OUT_DIR (release $RELEASE)"; cat "$OUT_DIR/SHA256SUMS"
