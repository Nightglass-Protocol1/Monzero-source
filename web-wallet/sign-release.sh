#!/bin/bash
# Signs a built web wallet release with the Monzero release key (run by the key holder, on their
# own machine; the key never leaves it):   web-wallet/sign-release.sh testnet|mainnet
set -euo pipefail
NET=${1:?testnet or mainnet}
DIR=$(cd "$(dirname "$0")" && pwd)/app/dist/$NET
FPR=4C37C618789F86D81FEF28DF390AEFB735F270D6
[ -f "$DIR/SHA256SUMS" ] || { echo "build first: web-wallet/app/build.sh $NET"; exit 1; }
(cd "$DIR" && sha256sum -c --quiet SHA256SUMS) || { echo "files do not match SHA256SUMS"; exit 1; }
rm -f "$DIR/SHA256SUMS.asc"
gpg --local-user "$FPR" --armor --detach-sign --output "$DIR/SHA256SUMS.asc" "$DIR/SHA256SUMS"
gpg --verify "$DIR/SHA256SUMS.asc" "$DIR/SHA256SUMS"
echo "signed: $DIR/SHA256SUMS.asc"
