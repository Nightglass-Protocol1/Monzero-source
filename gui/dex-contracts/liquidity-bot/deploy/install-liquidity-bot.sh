#!/bin/bash
# Install or update the DEX liquidity bot on monzero-eu (next to the pool). Run from the owner's PC:
#   bash install-liquidity-bot.sh            install/update; the service stays STOPPED
#   bash install-liquidity-bot.sh start      start it (after the bot's wallet and trade accounts are funded)
#   bash install-liquidity-bot.sh stop       stop it (open offers expire on their own; running trades
#                                            continue when it starts again)
#   bash install-liquidity-bot.sh accounts   the bot's XMZ address and each slot's trade account
#   bash install-liquidity-bot.sh status     what it holds and is doing
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
  install)  ;;
  *) echo "usage: $0 [install|start|stop|accounts|status]"; exit 1 ;;
esac

echo "== checking here"
python3 -m py_compile "$HERE/liquidity_bot.py"
python3 -W ignore "$HERE/test_liquidity_bot.py" 2>&1 | tail -1
HELPER=$ENGINE/target/host-test/release/monzero-dex-evm-helper
[ -x "$HELPER" ] || { echo "!! build the helper first (see README.md)"; exit 1; }
"$HELPER" </dev/null | grep -q '"ok":false' || { echo "!! $HELPER does not run"; exit 1; }

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
/opt/monzero-liquidity/monzero-dex-evm-helper </dev/null | grep -q '"ok":false' || {
  echo "!! the trade helper does not run on this server (too old a system library?)"; exit 1; }
systemctl daemon-reload
if systemctl is-active -q monzero-liquidity; then systemctl restart monzero-liquidity; echo "restarted (it was running)"; fi
echo "== the bot's wallet and keys (made on the first install only)"
sudo -u monzero-liquidity /usr/bin/python3 -I /opt/monzero-liquidity/liquidity_bot.py /etc/monzero-liquidity/config.json init 2>&1 | grep -v ' INFO '
REMOTE
echo
echo "Next: bash $0 accounts   (fund the XMZ wallet and the trade accounts), then: bash $0 start"
