#!/bin/bash
# Connect @Monzero_bot to the community group or channel: website announcements (optionally pinned)
# and a pool update every N hours. Checks with Telegram that the bot may post there, then saves
# the settings in ~/monzero-private/telegram-config.php. Posts nothing unless --test is given.
#   bash utils/website/telegram-feed.sh [@MonzeroOfficial] [--hours 24] [--no-pin] [--test] [--off]
set -euo pipefail
WEB=${WEB_HOST:-monzero-webspace}
CHAT=@MonzeroOfficial HOURS=24 PIN=true TEST=false OFF=false
while [ $# -gt 0 ]; do
  case "$1" in
    @*|-[0-9]*) CHAT=$1;; --hours) HOURS=$2; shift;; --no-pin) PIN=false;; --test) TEST=true;; --off) OFF=true;;
    *) echo "unknown option $1"; exit 2;;
  esac; shift
done
[[ "$HOURS" =~ ^[0-9]+$ ]] && [ "$HOURS" -le 168 ] || { echo "!! --hours must be 0-168 (0 = no pool updates)"; exit 2; }

setconfig() {  # JSON object on stdin, merged into the private config on the webspace (null removes a key)
  ssh -o BatchMode=yes "$WEB" 'php -r '"'"'$p = getenv("HOME") . "/monzero-private/telegram-config.php"; $c = require $p; $set = json_decode(stream_get_contents(STDIN), true);
    if (!is_array($set)) { fwrite(STDERR, "bad settings\n"); exit(1); }
    foreach ($set as $k => $v) { if ($v === null) unset($c[$k]); else $c[$k] = $v; }
    $t = $p . ".tmp"; umask(077); file_put_contents($t, "<?php return " . var_export($c, true) . ";\n"); rename($t, $p);
    if (function_exists("opcache_invalidate")) opcache_invalidate($p, true); echo "   website config updated\n";'"'"''
}
if $OFF; then echo '{"feed_chat":null}' | setconfig; echo "Group feed turned off."; exit 0; fi

TOKEN=$(ssh -o BatchMode=yes "$WEB" 'php -r '"'"'$c = require getenv("HOME") . "/monzero-private/telegram-config.php"; echo $c["bot_token"];'"'"'' 2>/dev/null)
[[ "$TOKEN" =~ ^[0-9]{5,15}:[A-Za-z0-9_-]{30,60}$ ]] || { echo "!! no bot token configured on the website; run telegram-setup.sh first"; exit 1; }
api() { local body=${2:-'{}'}; printf 'url = "https://api.telegram.org/bot%s/%s"\n' "$TOKEN" "$1" | curl -sS -m 20 -K - -H 'Content-Type: application/json' -d "$body"; }
BOT_ID=${TOKEN%%:*}
REPORT=$(mktemp); trap 'rm -f "$REPORT"' EXIT
{ api getChat "{\"chat_id\":\"$CHAT\"}"; echo; api getChatMember "{\"chat_id\":\"$CHAT\",\"user_id\":$BOT_ID}"; echo; } > "$REPORT"
python3 - "$REPORT" "$PIN" <<'PY'
import json, sys
chat, member = [json.loads(line) for line in open(sys.argv[1]) if line.strip()]
if not chat.get("ok"):
    sys.exit(f"!! Telegram cannot find that chat: {chat.get('description')}. Use the public @name of the group or channel.")
c = chat["result"]
print(f"   chat: {c.get('title')} ({c['type']}, id {c['id']})")
if not member.get("ok"):
    sys.exit(f"!! the bot is not in that chat yet: {member.get('description')}")
m = member["result"]
status = m["status"]
print(f"   bot status there: {status}")
if c["type"] == "channel" and not (status == "administrator" and m.get("can_post_messages")):
    sys.exit("!! in a channel the bot must be an administrator with 'Post messages'")
if c["type"] != "channel" and status not in ("administrator", "member"):
    sys.exit("!! add the bot to the group first")
if sys.argv[2] == "true" and not (status == "administrator" and m.get("can_pin_messages", c["type"] == "channel" and m.get("can_edit_messages"))):
    print("   note: the bot cannot pin messages there; announcements will be posted without pinning")
PY
python3 -c 'import json,sys;print(json.dumps({"feed_chat":sys.argv[1],"feed_pin":sys.argv[2]=="true","feed_pool_hours":int(sys.argv[3])}))' "$CHAT" "$PIN" "$HOURS" | setconfig
if $TEST; then
  api sendMessage "{\"chat_id\":\"$CHAT\",\"text\":\"✅ Monzero Alerts is connected. This chat will get website announcements and a pool update every $HOURS hours. Personal alerts: t.me/Monzero_bot\"}" \
    | python3 -c 'import sys,json;d=json.load(sys.stdin);print("   test message:", "sent" if d.get("ok") else d.get("description"))'
fi
echo "Done. Announcements: tick 'Also post to the Telegram group' in /admin. Pool updates: every $HOURS hours (the first run only records a starting point)."
