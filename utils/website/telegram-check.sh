#!/bin/bash
# Show the Telegram bot's settings as Telegram sees them (webhook, commands, texts), without
# printing the token. Run from the owner's PC:  bash utils/website/telegram-check.sh
set -euo pipefail
WEB=${WEB_HOST:-monzero-webspace}
TOKEN=$(ssh -o BatchMode=yes "$WEB" 'php -r '"'"'$c = require getenv("HOME") . "/monzero-private/telegram-config.php"; echo $c["bot_token"];'"'"'' 2>/dev/null)
[[ "$TOKEN" =~ ^[0-9]{5,15}:[A-Za-z0-9_-]{30,60}$ ]] || { echo "!! no bot token configured on the website"; exit 1; }
api() { local body=${2:-'{}'}; printf 'url = "https://api.telegram.org/bot%s/%s"\n' "$TOKEN" "$1" | curl -sS -m 20 -K - -H 'Content-Type: application/json' -d "$body"; }
REPORT=$(mktemp); trap 'rm -f "$REPORT"' EXIT
{ api getMe; echo; api getWebhookInfo; echo; api getMyCommands; echo; api getMyDescription; echo; api getMyShortDescription; echo; } > "$REPORT"
python3 - "$REPORT" <<'PY'
import json, sys, time
me, hook, cmds, desc, short = [json.loads(line)["result"] for line in open(sys.argv[1]) if line.strip()]
print(f"bot:            @{me['username']} ({me['first_name']})")
print(f"join groups:    {me.get('can_join_groups')}   reads all group messages: {me.get('can_read_all_group_messages')}   inline: {me.get('supports_inline_queries')}")
print(f"webhook:        {hook.get('url') or 'NOT SET'}")
print(f"pending:        {hook.get('pending_update_count', 0)} updates   allowed: {hook.get('allowed_updates', 'all')}")
if hook.get("last_error_date"):
    print(f"last error:     {time.strftime('%Y-%m-%d %H:%M UTC', time.gmtime(hook['last_error_date']))}: {hook.get('last_error_message')}")
else:
    print("last error:     none")
print("commands:       " + (", ".join("/" + c["command"] for c in cmds) or "none"))
print(f"description:    {desc.get('description') or '(empty)'}")
print(f"short (about):  {short.get('short_description') or '(empty)'}")
PY
