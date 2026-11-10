#!/usr/bin/env bash
# Owner-run: open ETH/XMZ and BNB/XMZ (the MetaMask coins) at the opening limits.
#
#   open-metamask-coins.sh SEQUENCE [WEBSITE_DIR]
#
# Signs this week's eth-mainnet and bnb-mainnet control documents with the owner's control key
# ($25 per trade, $250 per day at today's prices) and writes them to WEBSITE_DIR/dex/control/.
# It uploads nothing: the next commands are printed. SEQUENCE must be higher than the last one
# published. BTC stays closed (no document). To close a coin again, publish a document signed
# without --enabled and with a higher sequence.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
seq=${1:?sequence number}
site=${2:-$HOME/projects/Monzero-App/monzero-app/website}
key=${DEX_CONTROL_KEY:-$HOME/monzero-private/dex-control-btc-mainnet/control-signing.key}
out=$site/dex/control
mkdir -p "$out"
python3 "$here/opening-limits.py" --networks eth-mainnet,bnb-mainnet --sequence "$seq" --key "$key" | grep '^python3 ' |
while read -r _ _ rest; do
    network=$(sed -n 's/.*--network \([a-z-]*\).*/\1/p' <<<"$rest")
    args=${rest% > *}
    # shellcheck disable=SC2086
    python3 "$here/sign-control.py" ${args/#\~/$HOME} > "$out/$network.json.new"
    mv "$out/$network.json.new" "$out/$network.json"
    echo "signed $out/$network.json"
done
cat <<NEXT

Next, from the monzero-app checkout:
  python3 utils/website/deploy.py            # plan: should list dex/control/eth-mainnet.json and bnb-mainnet.json
  python3 utils/website/deploy.py --apply
  curl -s https://monzero.org/dex/control/eth-mainnet.json
When the public app release is out:
  utils/website/dex-realfunds.sh enable I_ENABLE_REAL_FUNDS
Kill switch at any time:
  utils/website/dex-realfunds.sh disable
NEXT
