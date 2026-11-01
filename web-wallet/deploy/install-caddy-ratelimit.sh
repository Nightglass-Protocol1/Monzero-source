#!/bin/bash
# Adds per-visitor rate limiting to the web wallet gateways on monzero-eu. Run as root from the
# unpacked folder (next to Caddyfile.wallet and Caddyfile.testnet-wallet):
#   sudo bash install-caddy-ratelimit.sh
# 1. Downloads the official Caddy build with the caddy-ratelimit plugin from caddyserver.com to
#    /usr/local/bin/caddy-ratelimit (Ubuntu's caddy package in /usr/bin is left untouched) and checks
#    that it contains the rate_limit module.
# 2. Builds the new Caddyfile (wallet sections replaced, pool.monzero.org kept) and validates it with
#    the new program.
# 3. Points caddy.service at the new program with a systemd drop-in and restarts Caddy (a second or
#    two of downtime; certificates in /var/lib/caddy are reused).
# 4. Checks the sites answer; on any failure puts everything back as it was.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
[ "$(id -u)" = 0 ] || { echo "run with sudo"; exit 1; }
for f in Caddyfile.wallet Caddyfile.testnet-wallet; do [ -f "$HERE/$f" ] || { echo "missing $f"; exit 1; }; done
grep -q '^pool.monzero.org {' /etc/caddy/Caddyfile || { echo "unexpected Caddyfile"; exit 1; }
NEW=/usr/local/bin/caddy-ratelimit
DROPIN=/etc/systemd/system/caddy.service.d/ratelimit.conf
STAMP=$(date -u +%Y%m%dT%H%M%SZ)

echo "== 1. Caddy with the rate-limit plugin"
curl -fsSL -o "$NEW.download" "https://caddyserver.com/api/download?os=linux&arch=amd64&p=github.com/mholt/caddy-ratelimit"
chmod 755 "$NEW.download"
"$NEW.download" version
"$NEW.download" list-modules | grep -qx 'http.handlers.rate_limit' || { echo "the download has no rate_limit module"; rm -f "$NEW.download"; exit 1; }
echo "sha256 $(sha256sum "$NEW.download" | cut -d' ' -f1)  (record this)"

echo "== 2. New Caddyfile"
B=/etc/caddy/Caddyfile.before-ratelimit-$STAMP
cp -p /etc/caddy/Caddyfile "$B"
awk '/^wallet\.monzero\.org \{/{exit} /^testnet-wallet\.monzero\.org \{/{exit} {print}' "$B" > /etc/caddy/Caddyfile.new
{ printf '\n'; cat "$HERE/Caddyfile.wallet"; printf '\n'; cat "$HERE/Caddyfile.testnet-wallet"; } >> /etc/caddy/Caddyfile.new
"$NEW.download" validate --config /etc/caddy/Caddyfile.new --adapter caddyfile >/tmp/caddy-validate.log 2>&1 || {
  echo "new config INVALID - nothing changed"; tail -5 /tmp/caddy-validate.log; rm -f /etc/caddy/Caddyfile.new "$NEW.download"; exit 1; }

rollback() {
  echo "!! rolling back"
  rm -f "$DROPIN"; cp -p "$B" /etc/caddy/Caddyfile
  systemctl daemon-reload; systemctl restart caddy || true
  systemctl is-active caddy
  exit 1
}

echo "== 3. Switch Caddy to the new program"
[ -f "$NEW" ] && cp -p "$NEW" "$NEW.previous-$STAMP"
mv "$NEW.download" "$NEW"
mv /etc/caddy/Caddyfile.new /etc/caddy/Caddyfile
mkdir -p "$(dirname "$DROPIN")"
printf '[Service]\nExecStart=\nExecStart=%s run --environ --config /etc/caddy/Caddyfile\nExecReload=\nExecReload=%s reload --config /etc/caddy/Caddyfile --force\n' "$NEW" "$NEW" > "$DROPIN"
systemctl daemon-reload
systemctl restart caddy || rollback
sleep 3

echo "== 4. Checks"
systemctl is-active caddy || rollback
for u in https://pool.monzero.org/ https://wallet.monzero.org/ https://testnet-wallet.monzero.org/; do
  code=$(curl -s -o /dev/null -m 20 -w '%{http_code}' --resolve "$(echo $u | cut -d/ -f3):443:127.0.0.1" "$u" || true)
  echo "$u -> $code"; [ "$code" = 200 ] || rollback
done
code=$(curl -s -o /dev/null -m 20 -w '%{http_code}' --resolve wallet.monzero.org:443:127.0.0.1 -X POST https://wallet.monzero.org/json_rpc -H 'Content-Type: application/json' -d '{"jsonrpc":"2.0","id":0,"method":"get_info"}' || true)
echo "wallet gateway -> $code"; [ "$code" = 200 ] || rollback
echo "done: Caddy $($NEW version | cut -d' ' -f1) with rate limiting (backup $B; undo: rm $DROPIN && cp $B /etc/caddy/Caddyfile && systemctl daemon-reload && systemctl restart caddy)"
