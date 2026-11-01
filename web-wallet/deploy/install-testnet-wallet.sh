#!/bin/bash
# Installs the testnet web wallet on monzero-eu. Run as root from the unpacked deploy folder:
#   sudo bash install-testnet-wallet.sh
# It:
#  1. installs and starts monzerod-testnet.service (hardened like monzerod.service; RPC on localhost
#     only, restricted; p2p on 16174) and opens 16174/tcp in ufw;
#  2. installs the testnet page into /srv/monzero-wallet-testnet after checking every file against
#     SHA256SUMS;
#  3. rewrites the wallet sections of /etc/caddy/Caddyfile: wallet.monzero.org (node calls at the
#     site root, mainnet node, still the placeholder page) and testnet-wallet.monzero.org (testnet
#     page and node). Everything before "wallet.monzero.org {" (pool.monzero.org) is kept as is.
#     The old file is backed up; the new one is validated before reload, else the backup comes back.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
[ "$(id -u)" = 0 ] || { echo "run with sudo"; exit 1; }
for f in monzerod-testnet.service Caddyfile.wallet Caddyfile.testnet-wallet page/SHA256SUMS; do
  [ -f "$HERE/$f" ] || { echo "missing $f"; exit 1; }
done
(cd "$HERE/page" && sha256sum -c --quiet SHA256SUMS) || { echo "page files do not match SHA256SUMS"; exit 1; }
grep -q '^pool.monzero.org {' /etc/caddy/Caddyfile || { echo "unexpected Caddyfile (no pool.monzero.org)"; exit 1; }

echo "== 1. testnet node"
install -m 644 "$HERE/monzerod-testnet.service" /etc/systemd/system/monzerod-testnet.service
systemctl daemon-reload
systemctl enable --now monzerod-testnet
ufw allow 16174/tcp comment 'Monzero testnet p2p (testnet web wallet)' >/dev/null
for i in $(seq 1 30); do curl -s -m 3 http://127.0.0.1:16175/get_info >/dev/null && break; sleep 2; done
curl -s -m 5 http://127.0.0.1:16175/get_info | python3 -c "import sys,json;d=json.load(sys.stdin);print('testnet node:',d['nettype'],'height',d['height'],'restricted',d['restricted'])"

echo "== 2. testnet page"
rm -rf /srv/monzero-wallet-testnet.new; mkdir -p /srv/monzero-wallet-testnet.new
install -m 644 "$HERE"/page/* /srv/monzero-wallet-testnet.new/
rm -rf /srv/monzero-wallet-testnet.old; [ -d /srv/monzero-wallet-testnet ] && mv /srv/monzero-wallet-testnet /srv/monzero-wallet-testnet.old
mv /srv/monzero-wallet-testnet.new /srv/monzero-wallet-testnet
(cd /srv/monzero-wallet-testnet && sha256sum -c --quiet SHA256SUMS) && echo "page installed and verified"

echo "== 3. Caddy"
B=/etc/caddy/Caddyfile.before-testnet-wallet-$(date -u +%Y%m%dT%H%M%SZ)
cp -p /etc/caddy/Caddyfile "$B"
awk '/^wallet\.monzero\.org \{/{exit} /^testnet-wallet\.monzero\.org \{/{exit} {print}' "$B" > /etc/caddy/Caddyfile.new
printf '\n' >> /etc/caddy/Caddyfile.new
cat "$HERE/Caddyfile.wallet" >> /etc/caddy/Caddyfile.new
printf '\n' >> /etc/caddy/Caddyfile.new
cat "$HERE/Caddyfile.testnet-wallet" >> /etc/caddy/Caddyfile.new
if caddy validate --config /etc/caddy/Caddyfile.new --adapter caddyfile >/tmp/caddy-validate.log 2>&1; then
  mv /etc/caddy/Caddyfile.new /etc/caddy/Caddyfile
  systemctl reload caddy && echo "Caddy validated and reloaded (backup $B)"
else
  rm -f /etc/caddy/Caddyfile.new
  echo "Caddy config INVALID - nothing changed (see /tmp/caddy-validate.log)"; tail -5 /tmp/caddy-validate.log; exit 1
fi
systemctl is-active caddy monzerod monzerod-testnet
echo "done"
