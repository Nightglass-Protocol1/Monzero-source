#!/bin/bash
# One-time setup of the Telegram miner alerts. Create a bot first: open @BotFather in Telegram,
# send /newbot, pick a name and a username ending in "bot", and copy the token it gives you.
# Run this in your own terminal (not through an assistant), so the token is never shown or logged:
#   bash utils/website/telegram-setup.sh
set -euo pipefail
WEB=${WEB_HOST:-monzero-webspace}
SITE=${SITE_URL:-https://monzero.org}
read -rsp "Paste the bot token from @BotFather (input hidden): " TOKEN; echo
[[ "$TOKEN" =~ ^[0-9]{5,15}:[A-Za-z0-9_-]{30,60}$ ]] || { echo "!! that does not look like a bot token"; exit 2; }
api() { printf 'url = "https://api.telegram.org/bot%s/%s"\n' "$TOKEN" "$1" | curl -sS -m 20 -K - -H 'Content-Type: application/json' -d "$2"; }

ME=$(api getMe '{}')
USERNAME=$(printf '%s' "$ME" | python3 -c 'import sys,json;d=json.load(sys.stdin);print(d["result"]["username"] if d.get("ok") else "")')
[ -n "$USERNAME" ] || { echo "!! Telegram did not accept the token"; exit 1; }
SECRET=$(openssl rand -hex 32)
echo "== bot @$USERNAME; writing the website configuration"
printf '%s\n%s\n%s\n' "$TOKEN" "$SECRET" "$USERNAME" | ssh -o BatchMode=yes "$WEB" \
  'umask 077; read -r t; read -r s; read -r u
   printf "<?php return [\"bot_token\" => \"%s\", \"webhook_secret\" => \"%s\", \"bot_username\" => \"%s\"];\n" "$t" "$s" "$u" > ~/monzero-private/telegram-config.php.tmp && mv ~/monzero-private/telegram-config.php.tmp ~/monzero-private/telegram-config.php && echo "   written"'
echo "== registering the webhook and commands"
api setWebhook "{\"url\":\"$SITE/api/telegram/\",\"secret_token\":\"$SECRET\",\"allowed_updates\":[\"message\",\"chat_join_request\",\"callback_query\"],\"drop_pending_updates\":true}" | python3 -c 'import sys,json;d=json.load(sys.stdin);print("   webhook:",d.get("description",d))'
api setMyCommands '{"commands":[{"command":"watch","description":"Alerts for a pool address"},{"command":"list","description":"What you watch"},{"command":"stop","description":"Stop alerts (address or all)"},{"command":"drop","description":"Hashrate drop alert percent"},{"command":"offline","description":"Offline alert minutes"},{"command":"help","description":"How it works"}]}' >/dev/null
api setMyDescription '{"description":"Alerts for Monzero pool miners: offline workers, hashrate drops and payouts. Send /watch with your pool address."}' >/dev/null
curl -s -m 15 "$SITE/api/telegram/"; echo
echo "Done. Open https://t.me/$USERNAME and send /watch YOUR_POOL_ADDRESS to try it."
