#!/bin/bash
# Register the Telegram command menus (scoped) for the Monzero bot: pool alerts in private
# chats, info commands in groups, and moderator commands for group admins. The bot code
# (lib/telegram.php) already handles them; this only populates the "/" menu Telegram shows.
# Run in your own terminal so the token is never shown to an assistant:
#   bash utils/website/telegram-commands.sh
set -euo pipefail
read -rsp "Paste the bot token from @BotFather (input hidden): " TOKEN; echo
[[ "$TOKEN" =~ ^[0-9]{5,15}:[A-Za-z0-9_-]{30,60}$ ]] || { echo "!! that does not look like a bot token"; exit 2; }
api() { curl -sS -m 20 "https://api.telegram.org/bot$TOKEN/$1" -H 'Content-Type: application/json' -d "$2" >/dev/null && echo "  ok $1 ${3:-}"; }

# Private chats (and default): the pool-alert commands.
api setMyCommands '{"scope":{"type":"default"},"commands":[
  {"command":"watch","description":"Alerts for a pool address"},
  {"command":"list","description":"What you watch"},
  {"command":"stop","description":"Stop alerts for an address (or: all)"},
  {"command":"drop","description":"Hashrate-drop alert percent"},
  {"command":"offline","description":"Offline-worker alert minutes"},
  {"command":"help","description":"Show commands"}]}' '' default

# All group chats: info commands anyone can use.
api setMyCommands '{"scope":{"type":"all_group_chats"},"commands":[
  {"command":"rules","description":"Group rules"},
  {"command":"scam","description":"Stay-safe / scam warning"},
  {"command":"links","description":"Official Monzero links"},
  {"command":"mining","description":"How to mine XMZ"},
  {"command":"pool","description":"Mining pool details"},
  {"command":"wallet","description":"Wallets and downloads"},
  {"command":"dex","description":"Atomic-swap DEX"},
  {"command":"explorer","description":"Block explorer"},
  {"command":"security","description":"Report a security issue"},
  {"command":"price","description":"Price / listing status"},
  {"command":"report","description":"Reply to flag a message to mods"},
  {"command":"help","description":"Show commands"}]}' '' groups

# Group admins: the moderator commands on top of the info ones.
api setMyCommands '{"scope":{"type":"all_chat_administrators"},"commands":[
  {"command":"ban","description":"Reply: ban the user"},
  {"command":"kick","description":"Reply: remove (can rejoin)"},
  {"command":"mute","description":"Reply: mute [30m|2h|1d]"},
  {"command":"unmute","description":"Reply: lift a mute"},
  {"command":"warn","description":"Reply: warn (auto-mute at 3)"},
  {"command":"unwarn","description":"Reply: remove a warning"},
  {"command":"del","description":"Reply: delete the message"},
  {"command":"pin","description":"Reply: pin the message"},
  {"command":"modhelp","description":"Moderator command help"},
  {"command":"rules","description":"Group rules"},
  {"command":"scam","description":"Stay-safe / scam warning"},
  {"command":"links","description":"Official Monzero links"}]}' '' admins

echo "Done. In @MonzeroOfficial, make the bot an admin with: Ban users, Delete messages, Pin messages."
