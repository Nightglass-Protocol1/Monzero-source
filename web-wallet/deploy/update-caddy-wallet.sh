#!/bin/bash
# Refreshes only the wallet sections of /etc/caddy/Caddyfile on monzero-eu (wallet.monzero.org and
# testnet-wallet.monzero.org) from Caddyfile.wallet and Caddyfile.testnet-wallet next to this script.
# pool.monzero.org (everything before "wallet.monzero.org {") is kept. Backup first; validated before
# reload, else nothing changes.   sudo bash update-caddy-wallet.sh
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
[ "$(id -u)" = 0 ] || { echo "run with sudo"; exit 1; }
grep -q '^pool.monzero.org {' /etc/caddy/Caddyfile || { echo "unexpected Caddyfile"; exit 1; }
B=/etc/caddy/Caddyfile.before-wallet-update-$(date -u +%Y%m%dT%H%M%SZ)
cp -p /etc/caddy/Caddyfile "$B"
awk '/^wallet\.monzero\.org \{/{exit} /^testnet-wallet\.monzero\.org \{/{exit} {print}' "$B" > /etc/caddy/Caddyfile.new
{ printf '\n'; cat "$HERE/Caddyfile.wallet"; printf '\n'; cat "$HERE/Caddyfile.testnet-wallet"; } >> /etc/caddy/Caddyfile.new
if caddy validate --config /etc/caddy/Caddyfile.new --adapter caddyfile >/tmp/caddy-validate.log 2>&1; then
  mv /etc/caddy/Caddyfile.new /etc/caddy/Caddyfile
  systemctl reload caddy && echo "Caddy validated and reloaded (backup $B)"
else
  rm -f /etc/caddy/Caddyfile.new; echo "INVALID - nothing changed"; tail -5 /tmp/caddy-validate.log; exit 1
fi
