#!/bin/bash
# Install or update the DEX liquidity bot on monzero-eu (next to the pool). Run from the owner's PC:
#   bash install-liquidity-bot.sh            install/update; the service stays STOPPED
#   bash install-liquidity-bot.sh start      start it (after the bot's wallet and trade accounts are funded)
#   bash install-liquidity-bot.sh stop       stop it (open offers expire on their own; running trades
#                                            continue when it starts again)
#   bash install-liquidity-bot.sh accounts   the bot's XMZ address and each slot's trade account
#   bash install-liquidity-bot.sh status     what it holds and is doing
#   bash install-liquidity-bot.sh layout 2,5,10,20,35,50 2000
#                                            set the ladder steps (percent above the reference
#                                            price, one offer slot per step and pair) and the XMZ
#                                            per offer; restarts the bot. A new slot needs its trade
#                                            account funded for network fees (see 'accounts').
#   bash install-liquidity-bot.sh update-bot only the bot program (not the trade helper); restarts it
#   bash install-liquidity-bot.sh market on 0.0008 1
#                                            the reference price follows XMZ on NoirTrade (floor in
#                                            USD per XMZ; smallest part a buyer may take, in USD);
#                                            'market off' goes back to the fixed/dynamic price
# Config on the server: /etc/monzero-liquidity/config.json (written once, never overwritten).
# Keys and wallet: /var/lib/monzero-liquidity/{keys,main-wallet} - back these up after the first install.
set -euo pipefail
HOST=${HOST:-monzero-eu}
HERE=$(cd "$(dirname "$0")/.." && pwd)
ENGINE=$HERE/../engine
ACTION=${1:-install}
remote() { ssh -o BatchMode=yes "$HOST" "$@"; }
as_bot() { remote sudo -u monzero-liquidity /usr/bin/python3 -I /opt/monzero-liquidity/liquidity_bot.py /etc/monzero-liquidity/config.json "$@"; }

case "$ACTION" in
  start)    remote sudo systemctl enable --now monzero-liquidity; sleep 3; remote systemctl is-active monzero-liquidity; exit ;;
  stop)     remote sudo systemctl disable --now monzero-liquidity; exit ;;
  accounts) as_bot accounts 2>&1 | grep -v ' INFO '; exit ;;
  status)   as_bot status; remote sudo journalctl -u monzero-liquidity -n 15 --no-pager -o cat || true; exit ;;
  layout)
    LADDER=${2:-}; OFFER=${3:-}
    [[ "$LADDER" =~ ^[0-9]+(\.[0-9]+)?(,[0-9]+(\.[0-9]+)?)*$ ]] || { echo "ladder: percents separated by commas, e.g. 2,5,10,20"; exit 1; }
    [[ "$OFFER" =~ ^[0-9]+$ ]] && [ "$OFFER" -ge 1 ] || { echo "offer: whole XMZ per offer"; exit 1; }
    remote sudo LADDER="$LADDER" OFFER="$OFFER" bash -s <<'REMOTE'
set -euo pipefail
C=/etc/monzero-liquidity/config.json
TS=$(date -u +%Y%m%d-%H%M%S)
cp -p "$C" "$C.before-layout-$TS"
python3 - "$C" <<'PY'
import json, os, sys
c = json.load(open(sys.argv[1]))
print("was: ladder %s, %s XMZ per offer" % (c.get("ladder_percent", "default"), c.get("offer_xmz", "default")))
c["ladder_percent"] = [float(x) if "." in x else int(x) for x in os.environ["LADDER"].split(",")]
c["offer_xmz"] = os.environ["OFFER"]
open(sys.argv[1], "w").write(json.dumps(c, indent=1) + "\n")
print("now: ladder %s, %s XMZ per offer" % (c["ladder_percent"], c["offer_xmz"]))
PY
# The bot checks the config as it loads it: a refused one is put back
if ! sudo -u monzero-liquidity /usr/bin/python3 -I /opt/monzero-liquidity/liquidity_bot.py "$C" status >/dev/null; then
  cp -p "$C.before-layout-$TS" "$C"; echo "!! the bot refused the new config; the old one is back"; exit 1
fi
echo "backup: $C.before-layout-$TS"
if systemctl is-active -q monzero-liquidity; then systemctl restart monzero-liquidity; echo "bot restarted with the new layout"; fi
REMOTE
    exit ;;
  update-bot)
    python3 -m py_compile "$HERE/liquidity_bot.py"
    python3 -W ignore "$HERE/test_liquidity_bot.py" 2>&1 | tail -1 | grep -qx OK || { echo "!! the bot's tests fail"; exit 1; }
    scp -q "$HERE/liquidity_bot.py" "$HOST:/tmp/liquidity_bot.py"
    remote sudo bash -s <<'REMOTE'
set -euo pipefail
TS=$(date -u +%Y%m%d-%H%M%S)
cp -p /opt/monzero-liquidity/liquidity_bot.py /opt/monzero-liquidity/liquidity_bot.py.before-$TS
install -o root -g root -m 644 /tmp/liquidity_bot.py /opt/monzero-liquidity/liquidity_bot.py; rm -f /tmp/liquidity_bot.py
if ! sudo -u monzero-liquidity /usr/bin/python3 -I /opt/monzero-liquidity/liquidity_bot.py /etc/monzero-liquidity/config.json status >/dev/null; then
  cp -p /opt/monzero-liquidity/liquidity_bot.py.before-$TS /opt/monzero-liquidity/liquidity_bot.py; echo "!! the new bot refused the config; the old one is back"; exit 1
fi
echo "bot updated (backup liquidity_bot.py.before-$TS)"
if systemctl is-active -q monzero-liquidity; then systemctl restart monzero-liquidity; echo "restarted"; fi
REMOTE
    exit ;;
  market)
    MODE=${2:-}; FLOOR=${3:-0.0008}; PART=${4:-1}
    case "$MODE" in on|off) ;; *) echo "usage: $0 market on|off [floor_usd] [min_part_usd]"; exit 1;; esac
    [[ "$FLOOR" =~ ^[0-9]*\.?[0-9]+$ && "$PART" =~ ^[0-9]*\.?[0-9]+$ ]] || { echo "floor and part must be numbers"; exit 1; }
    remote sudo MODE="$MODE" FLOOR="$FLOOR" PART="$PART" bash -s <<'REMOTE'
set -euo pipefail
C=/etc/monzero-liquidity/config.json
TS=$(date -u +%Y%m%d-%H%M%S)
cp -p "$C" "$C.before-market-$TS"
python3 - "$C" <<'PY'
import json, os, sys
c = json.load(open(sys.argv[1]))
m = c.setdefault("market_price", {})
m["enabled"] = os.environ["MODE"] == "on"
if m["enabled"]:
    m["floor_usd"] = os.environ["FLOOR"]
    c["min_part_usd"] = os.environ["PART"]
open(sys.argv[1], "w").write(json.dumps(c, indent=1) + "\n")
print("market price %s%s" % ("ON" if m["enabled"] else "off", ", floor $%s per XMZ, smallest part $%s" % (m["floor_usd"], c["min_part_usd"]) if m["enabled"] else ""))
PY
if ! sudo -u monzero-liquidity /usr/bin/python3 -I /opt/monzero-liquidity/liquidity_bot.py "$C" status >/dev/null; then
  cp -p "$C.before-market-$TS" "$C"; echo "!! the bot refused the new config (run update-bot first?); the old one is back"; exit 1
fi
echo "backup: $C.before-market-$TS"
if systemctl is-active -q monzero-liquidity; then systemctl restart monzero-liquidity; echo "bot restarted"; fi
REMOTE
    exit ;;
  install)  ;;
  *) echo "usage: $0 [install|start|stop|accounts|status|layout STEPS XMZ|update-bot|market on|off FLOOR PART]"; exit 1 ;;
esac

echo "== checking here"
python3 -m py_compile "$HERE/liquidity_bot.py"
python3 -W ignore "$HERE/test_liquidity_bot.py" 2>&1 | tail -1
HELPER=$ENGINE/target/host-test/release/monzero-dex-evm-helper
[ -x "$HELPER" ] || { echo "!! build the helper first (see README.md)"; exit 1; }
# An empty request is answered with an error (exit 1): that answer shows the program runs
grep -q '"ok":false' <<<"$("$HELPER" </dev/null 2>&1 || true)" || { echo "!! $HELPER does not run"; exit 1; }

echo "== copying"
scp -q "$HERE/liquidity_bot.py" "$HELPER" "$HERE/deploy/monzero-liquidity.service" "$HERE/config.example.json" "$HOST:/tmp/"
remote sudo bash -s <<'REMOTE'
set -euo pipefail
id monzero-liquidity >/dev/null 2>&1 || useradd --system --home-dir /var/lib/monzero-liquidity --shell /usr/sbin/nologin monzero-liquidity
install -d -o root -g root -m 755 /opt/monzero-liquidity
install -d -o monzero-liquidity -g monzero-liquidity -m 700 /var/lib/monzero-liquidity
install -d -o root -g monzero-liquidity -m 750 /etc/monzero-liquidity
TS=$(date -u +%Y%m%d-%H%M%S)
for f in liquidity_bot.py monzero-dex-evm-helper; do
  [ -e /opt/monzero-liquidity/$f ] && cp -p /opt/monzero-liquidity/$f /opt/monzero-liquidity/$f.before-$TS
done
install -o root -g root -m 644 /tmp/liquidity_bot.py /opt/monzero-liquidity/liquidity_bot.py
install -o root -g root -m 755 /tmp/monzero-dex-evm-helper /opt/monzero-liquidity/monzero-dex-evm-helper
install -o root -g root -m 644 /tmp/monzero-liquidity.service /etc/systemd/system/monzero-liquidity.service
if [ ! -e /etc/monzero-liquidity/config.json ]; then
  # The pool's own node (trusted, on this host)
  DAEMON=$(python3 -c 'import json;u=json.load(open("/etc/xmzpool/pool.json"))["daemon_rpc"];print(u.split("//")[-1].split("/")[0])')
  python3 - "$DAEMON" <<'PY'
import json, sys
c = json.load(open("/tmp/config.example.json"))
c["daemon"] = sys.argv[1]
open("/etc/monzero-liquidity/config.json", "w").write(json.dumps(c, indent=1) + "\n")
PY
  chown root:monzero-liquidity /etc/monzero-liquidity/config.json; chmod 640 /etc/monzero-liquidity/config.json
  echo "wrote /etc/monzero-liquidity/config.json (daemon $DAEMON)"
fi
rm -f /tmp/liquidity_bot.py /tmp/monzero-dex-evm-helper /tmp/monzero-liquidity.service /tmp/config.example.json
grep -q '"ok":false' <<<"$(/opt/monzero-liquidity/monzero-dex-evm-helper </dev/null 2>&1 || true)" || {
  echo "!! the trade helper does not run on this server (too old a system library?)"; exit 1; }
systemctl daemon-reload
if systemctl is-active -q monzero-liquidity; then systemctl restart monzero-liquidity; echo "restarted (it was running)"; fi
# First install only: make the keys and wallet. Never while the service runs (it has the wallet open).
if [ ! -e /var/lib/monzero-liquidity/keys/seed.hex ] && ! systemctl is-active -q monzero-liquidity; then
  echo "== the bot's wallet and keys"
  sudo -u monzero-liquidity /usr/bin/python3 -I /opt/monzero-liquidity/liquidity_bot.py /etc/monzero-liquidity/config.json init 2>&1 | grep -v ' INFO '
fi
REMOTE
echo
echo "Next: bash $0 accounts   (fund the XMZ wallet and the trade accounts), then: bash $0 start"
