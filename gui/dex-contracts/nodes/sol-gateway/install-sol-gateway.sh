#!/bin/bash
# Monzero's Solana RPC gateway (sol_gateway.py) behind Caddy (HTTPS with automatic certificates).
# Run as root on the node server from this folder:
#   sudo bash install-sol-gateway.sh [DOMAIN]        # default sol.monzero.org
# Needs a DNS A record DOMAIN -> this server first (HTTPS certificates are issued for it).
# Providers: /etc/sol-gateway/config.json. Starts with the free public endpoints; put paid
# providers (with their API keys) first in the list and `systemctl restart sol-gateway`.
# Safe to run again: the config is kept, the program and service are updated.
set -euo pipefail
DOMAIN=${1:-sol.monzero.org}
HERE=$(cd "$(dirname "$0")" && pwd)
[ "$(id -u)" = 0 ] || { echo "run as root (sudo)"; exit 1; }

echo "== 1. packages"
apt-get update -qq
DEBIAN_FRONTEND=noninteractive apt-get install -y -qq python3-aiohttp caddy >/dev/null

echo "== 2. gateway"
id solgw >/dev/null 2>&1 || useradd --system --no-create-home --shell /usr/sbin/nologin solgw
install -d -m 755 /opt/sol-gateway
install -m 644 "$HERE/sol_gateway.py" /opt/sol-gateway/sol_gateway.py
install -d -m 750 -o root -g solgw /etc/sol-gateway
if [ ! -f /etc/sol-gateway/config.json ]; then
  cat > /etc/sol-gateway/config.json <<'EOF'
{
  "listen": "127.0.0.1:8899",
  "providers": [
    {"name": "publicnode", "http": "https://solana-rpc.publicnode.com", "ws": "wss://solana-rpc.publicnode.com"},
    {"name": "solana-foundation", "http": "https://api.mainnet-beta.solana.com", "ws": "wss://api.mainnet-beta.solana.com"}
  ],
  "rate_per_sec": 20,
  "burst": 100,
  "max_lag_slots": 150,
  "trust_proxy": true
}
EOF
  echo "wrote /etc/sol-gateway/config.json (public providers only)"
else
  echo "keeping /etc/sol-gateway/config.json"
fi
chown root:solgw /etc/sol-gateway/config.json; chmod 640 /etc/sol-gateway/config.json
python3 -c 'import json,sys; c=json.load(open(sys.argv[1])); assert c["providers"]' /etc/sol-gateway/config.json

cat > /etc/systemd/system/sol-gateway.service <<'EOF'
[Unit]
Description=Monzero Solana RPC gateway
After=network-online.target
Wants=network-online.target

[Service]
User=solgw
Group=solgw
ExecStart=/usr/bin/python3 /opt/sol-gateway/sol_gateway.py
Restart=always
RestartSec=5
NoNewPrivileges=yes
ProtectSystem=strict
ProtectHome=yes
PrivateTmp=yes
PrivateDevices=yes
MemoryMax=1G

[Install]
WantedBy=multi-user.target
EOF
systemctl daemon-reload
systemctl enable --now sol-gateway >/dev/null 2>&1
systemctl restart sol-gateway

echo "== 3. HTTPS for $DOMAIN (Caddy)"
cat > /etc/caddy/Caddyfile <<EOF
# Written by install-sol-gateway.sh
$DOMAIN {
	encode gzip
	reverse_proxy 127.0.0.1:8899
	log {
		output discard
	}
}
EOF
caddy validate --config /etc/caddy/Caddyfile --adapter caddyfile >/dev/null
systemctl enable caddy >/dev/null 2>&1
systemctl reload caddy 2>/dev/null || systemctl restart caddy

echo "== 4. firewall"
if ufw status | grep -q "Status: active"; then
  ufw allow 80/tcp comment "caddy (certificates)" >/dev/null; ufw allow 443/tcp comment "caddy https" >/dev/null
  echo "ufw: 80 and 443 open"
fi

sleep 3
systemctl is-active sol-gateway caddy
curl -s -m 10 -X POST -H 'Content-Type: application/json' -d '{"jsonrpc":"2.0","id":1,"method":"getSlot"}' http://127.0.0.1:8899/ ; echo
echo "DNS: $DOMAIN -> $(getent ahostsv4 "$DOMAIN" | awk 'NR==1{print $1}')"
echo "Then: curl -s https://$DOMAIN/health"
