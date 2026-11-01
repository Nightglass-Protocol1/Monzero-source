#!/bin/bash
# Installs a signed web wallet release on monzero-eu. Run as root from the release folder:
#   sudo bash install-page.sh testnet|mainnet
# Refuses unless every file matches SHA256SUMS and SHA256SUMS.asc is present (the signature is
# checked by users and by the key holder before upload). Keeps the previous release for rollback.
set -euo pipefail
NET=${1:?testnet or mainnet}
HERE=$(cd "$(dirname "$0")" && pwd)
[ "$(id -u)" = 0 ] || { echo "run with sudo"; exit 1; }
case "$NET" in testnet) DEST=/srv/monzero-wallet-testnet ;; mainnet) DEST=/srv/monzero-wallet ;; *) echo "testnet or mainnet"; exit 1 ;; esac
P="$HERE/page"
for f in index.html style.css app.js app.js.LICENSE.txt monero.worker.js verify.html SHA256SUMS SHA256SUMS.asc; do
  [ -f "$P/$f" ] || { echo "missing page/$f (sign the release first)"; exit 1; }
done
(cd "$P" && sha256sum -c --quiet SHA256SUMS) || { echo "files do not match SHA256SUMS"; exit 1; }
grep -q "$(grep 'app.js$' "$P/SHA256SUMS" | cut -c1-64)" "$P/verify.html" || { echo "verify.html is for another release"; exit 1; }
rm -rf "$DEST.new"; mkdir -p "$DEST.new"; install -m 644 "$P"/* "$DEST.new/"
STAMP=$(date -u +%Y%m%dT%H%M%SZ)
[ -d "$DEST" ] && mv "$DEST" "$DEST.previous-$STAMP"
mv "$DEST.new" "$DEST"
(cd "$DEST" && sha256sum -c --quiet SHA256SUMS) && echo "installed $NET release into $DEST (previous: $DEST.previous-$STAMP)"
echo "rollback: rm -rf $DEST && mv $DEST.previous-$STAMP $DEST"
