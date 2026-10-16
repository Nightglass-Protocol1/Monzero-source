#!/bin/bash
# Serve the pool page and API at https://pool.monzero.org (Caddy, automatic certificate).
# Needs TCP 80 and 443 open in the IONOS cloud firewall first.
# Run from the owner's PC:  bash ~/projects/Monzero-App/pool/xmzpool/deploy/enable-https.sh
set -euo pipefail
HOST=${HOST:-monzero-eu}
DOMAIN=${DOMAIN:-pool.monzero.org}
ssh -o BatchMode=yes "$HOST" sudo DOMAIN="$DOMAIN" bash -s <<'REMOTE'
set -euo pipefail
if ss -ltn | grep -qE ':(80|443)\s'; then
  echo "!! something already listens on 80/443:"; ss -ltnp | grep -E ':(80|443)\s'; exit 1
fi
command -v caddy >/dev/null || { apt-get update -qq && DEBIAN_FRONTEND=noninteractive apt-get install -y -qq caddy >/dev/null; }
caddy version
[ -f /etc/caddy/Caddyfile ] && cp -p /etc/caddy/Caddyfile /etc/caddy/Caddyfile.before-pool
cat > /etc/caddy/Caddyfile <<EOF
$DOMAIN {
	encode gzip
	reverse_proxy 127.0.0.1:8117
	header Strict-Transport-Security "max-age=31536000"
}
EOF
caddy validate --config /etc/caddy/Caddyfile --adapter caddyfile >/dev/null
if command -v ufw >/dev/null && ufw status | grep -q 'Status: active'; then ufw allow 80/tcp >/dev/null; ufw allow 443/tcp >/dev/null; fi
systemctl enable --now caddy >/dev/null 2>&1; systemctl reload caddy || systemctl restart caddy

# Show the HTTPS address on the pool page.
python3 - "$DOMAIN" <<'PY'
import json, sys
p = "/etc/xmzpool/pool.json"; cfg = json.load(open(p))
if cfg.get("public_host") != sys.argv[1]:
    cfg["public_host"] = sys.argv[1]; json.dump(cfg, open(p, "w"), indent=2)
PY
systemctl restart xmzpool
sleep 15
systemctl is-active caddy xmzpool
curl -s -m 20 "https://$DOMAIN/api/stats" | python3 -c 'import sys,json;s=json.load(sys.stdin);print("https ok: pool host",s["config"]["poolHost"],"| height",s["network"]["height"])' \
  || echo "!! HTTPS not answering yet: check 80/443 in the IONOS firewall, then: journalctl -u caddy -n 50"
REMOTE
