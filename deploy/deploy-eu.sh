#!/bin/bash
# Deploy xmzpool to monzero-eu (node.monzero.org) with payouts OFF.
# Run from the owner's PC:  bash ~/projects/Monzero-App/pool/xmzpool/deploy/deploy-eu.sh
# Safe to re-run: every step checks what is already there.
set -euo pipefail
HOST=${HOST:-monzero-eu}   # monzero-admin, passwordless sudo
HERE=$(cd "$(dirname "$0")/.." && pwd)
WALLET_RPC_BIN=${WALLET_RPC_BIN:-$HOME/projects/Monzero-App/testnet/bin/monzero-genesis-pre16-linux-x86_64/monzero-wallet-rpc}
TS=$(date -u +%Y%m%d-%H%M%S)

echo "== copying pool files"
tar -C "$HERE" -cf - xmzpool.py web systemd pool.example.json | ssh -o BatchMode=yes "$HOST" "sudo rm -rf /opt/xmzpool.new && sudo mkdir -p /opt/xmzpool.new && sudo tar -C /opt/xmzpool.new -xf - --no-same-owner"
if ! ssh -o BatchMode=yes "$HOST" test -x /usr/bin/monzero-wallet-rpc; then
  echo "== installing monzero-wallet-rpc from the pre16 release"
  scp -q "$WALLET_RPC_BIN" "$HOST:/tmp/monzero-wallet-rpc.new"
  ssh -o BatchMode=yes "$HOST" "sudo install -m 755 /tmp/monzero-wallet-rpc.new /usr/bin/monzero-wallet-rpc && rm /tmp/monzero-wallet-rpc.new"
fi

ssh -o BatchMode=yes "$HOST" sudo TS="$TS" bash -s <<'REMOTE'
set -euo pipefail
CONF=/etc/monzero/monzerod.conf
rpc() { local params=${3:-"{}"}; curl -s -m 20 "http://127.0.0.1:$1/json_rpc" -d "{\"jsonrpc\":\"2.0\",\"id\":0,\"method\":\"$2\",\"params\":$params}" ${4:+--digest -u "$4"}; }

echo "== node: public restricted RPC on 6175, private full RPC on 127.0.0.1:6185"
if ! grep -q '^rpc-bind-port=6185' "$CONF"; then
  cp -p "$CONF" "$CONF.before-pool-$TS"
  sed -i -E 's/^(rpc-bind-ip|rpc-bind-port|restricted-rpc|rpc-restricted-bind-ip|rpc-restricted-bind-port)\b/# before-pool: &/' "$CONF"
  cat >> "$CONF" <<EOF

# xmzpool ($TS): public restricted RPC + private full RPC for the pool
rpc-restricted-bind-ip=0.0.0.0
rpc-restricted-bind-port=6175
rpc-bind-ip=127.0.0.1
rpc-bind-port=6185
EOF
  systemctl restart monzerod
  for i in $(seq 1 90); do rpc 6185 get_info | grep -q '"status": "OK"' && break; sleep 2; done
  public=$(rpc 6175 get_info | grep -c '"status": "OK"' || true)
  private=$(rpc 6185 calc_pow '{"major_version":16,"height":1,"block_blob":"zz","seed_hash":""}' | grep -c 'Wrong block' || true)
  restricted=$(rpc 6175 calc_pow '{"major_version":16,"height":1,"block_blob":"zz","seed_hash":""}' | grep -c 'Method not found' || true)
  if [ "$public" -lt 1 ] || [ "$private" -lt 1 ] || [ "$restricted" -lt 1 ]; then
    echo "!! node RPC check failed (public=$public private=$private restricted=$restricted); restoring config"
    cp -p "$CONF.before-pool-$TS" "$CONF"; systemctl restart monzerod; exit 1
  fi
  echo "   ok: public restricted RPC answers, calc_pow only on localhost (backup $CONF.before-pool-$TS)"
else
  echo "   already configured"
fi

echo "== users and folders"
id xmzwallet >/dev/null 2>&1 || useradd --system --home /var/lib/xmzwallet --shell /usr/sbin/nologin xmzwallet
id xmzpool  >/dev/null 2>&1 || useradd --system --home /var/lib/xmzpool  --shell /usr/sbin/nologin xmzpool
install -d -o xmzwallet -g xmzwallet -m 700 /var/lib/xmzwallet
install -d -o root -g xmzwallet -m 750 /etc/xmzwallet
install -d -o xmzpool -g xmzpool -m 700 /var/lib/xmzpool
install -d -o root -g xmzpool -m 750 /etc/xmzpool
[ -s /etc/xmzwallet/password ]  || (umask 077; head -c 32 /dev/urandom | base64 | tr -d '/+=' > /etc/xmzwallet/password)
[ -s /etc/xmzwallet/rpc-login ] || (umask 077; echo "pool:$(head -c 24 /dev/urandom | base64 | tr -d '/+=')" > /etc/xmzwallet/rpc-login)
chown root:xmzwallet /etc/xmzwallet/password /etc/xmzwallet/rpc-login; chmod 640 /etc/xmzwallet/password /etc/xmzwallet/rpc-login
LOGIN=$(cat /etc/xmzwallet/rpc-login)

echo "== pool wallet"
if [ ! -f /var/lib/xmzwallet/pool.keys ]; then
  sudo -u xmzwallet /usr/bin/monzero-wallet-rpc --daemon-address 127.0.0.1:6185 --trusted-daemon --rpc-bind-ip 127.0.0.1 \
    --rpc-bind-port 6191 --disable-rpc-login --wallet-dir /var/lib/xmzwallet --non-interactive --log-file /var/lib/xmzwallet/create.log >/dev/null 2>&1 &
  CPID=$!
  for i in $(seq 1 60); do rpc 6191 get_version | grep -q result && break; sleep 1; done
  rpc 6191 create_wallet "{\"filename\":\"pool\",\"password\":\"$(cat /etc/xmzwallet/password)\",\"language\":\"English\"}" | grep -q '"result"' || { kill $CPID; echo "!! wallet creation failed"; exit 1; }
  rpc 6191 close_wallet >/dev/null; kill $CPID; wait $CPID 2>/dev/null || true
  cp -p /var/lib/xmzwallet/pool /var/lib/xmzwallet/pool.keys /root/ 2>/dev/null || true
  echo "   created /var/lib/xmzwallet/pool (copy also in /root/)"
fi
cat > /etc/systemd/system/xmzwallet.service <<'EOF'
[Unit]
Description=Monzero pool wallet RPC
After=monzerod.service
Requires=monzerod.service
[Service]
User=xmzwallet
Group=xmzwallet
ExecStart=/bin/sh -c 'exec /usr/bin/monzero-wallet-rpc --daemon-address 127.0.0.1:6185 --trusted-daemon --rpc-bind-ip 127.0.0.1 --rpc-bind-port 6190 --rpc-login "$$(cat /etc/xmzwallet/rpc-login)" --wallet-file /var/lib/xmzwallet/pool --password-file /etc/xmzwallet/password --non-interactive --log-file /var/lib/xmzwallet/wallet-rpc.log'
Restart=on-failure
RestartSec=10
NoNewPrivileges=yes
ProtectSystem=strict
ProtectHome=yes
PrivateTmp=yes
ReadWritePaths=/var/lib/xmzwallet
[Install]
WantedBy=multi-user.target
EOF
systemctl daemon-reload; systemctl enable --now xmzwallet >/dev/null 2>&1; systemctl restart xmzwallet
for i in $(seq 1 60); do rpc 6190 get_address '{"account_index":0}' "$LOGIN" | grep -q '"address"' && break; sleep 2; done
ADDR=$(rpc 6190 get_address '{"account_index":0}' "$LOGIN" | python3 -c 'import sys,json;print(json.load(sys.stdin)["result"]["address"])')
echo "   pool address: $ADDR"

echo "== pool"
rm -rf /opt/xmzpool.prev; [ -d /opt/xmzpool ] && mv /opt/xmzpool /opt/xmzpool.prev; mv /opt/xmzpool.new /opt/xmzpool
chown -R root:root /opt/xmzpool; chmod -R go-w /opt/xmzpool
if [ ! -f /etc/xmzpool/pool.json ]; then
  python3 - "$ADDR" "$LOGIN" <<'PY'
import json, sys
cfg = json.load(open("/opt/xmzpool/pool.example.json"))
cfg.update(pool_address=sys.argv[1], wallet_rpc_login=sys.argv[2], public_host="node.monzero.org",
           api_bind="0.0.0.0", api_port=8117)
cfg["payouts"]["enabled"] = False
json.dump(cfg, open("/etc/xmzpool/pool.json", "w"), indent=2)
PY
fi
chown root:xmzpool /etc/xmzpool/pool.json; chmod 640 /etc/xmzpool/pool.json
cp /opt/xmzpool/systemd/xmzpool.service /etc/systemd/system/xmzpool.service
sed -i 's/^After=network-online.target monzerod.service/After=network-online.target monzerod.service xmzwallet.service/' /etc/systemd/system/xmzpool.service
systemctl daemon-reload; systemctl enable --now xmzpool >/dev/null 2>&1; systemctl restart xmzpool
sleep 5

echo "== firewall: stratum 3333, 5555 and stats API 8117"
if command -v ufw >/dev/null && ufw status | grep -q 'Status: active'; then
  for p in 3333 5555 8117; do ufw allow $p/tcp >/dev/null; done; echo "   ufw rules added"
elif command -v firewall-cmd >/dev/null && firewall-cmd --state >/dev/null 2>&1; then
  for p in 3333 5555 8117; do firewall-cmd -q --permanent --add-port=$p/tcp; done; firewall-cmd -q --reload; echo "   firewalld rules added"
else
  echo "   no ufw/firewalld active; check the IONOS cloud firewall for these ports"
fi

echo "== checks"
systemctl is-active monzerod xmzwallet xmzpool
curl -s -m 10 http://127.0.0.1:8117/api/stats | python3 -c 'import sys,json;s=json.load(sys.stdin);print("   API ok: network height",s["network"]["height"],"payouts enabled:",s["config"]["paymentsEnabled"])'
python3 - <<'PY'
import json, socket
s = socket.create_connection(("127.0.0.1", 3333), 10)
s.sendall(b'{"id":1,"jsonrpc":"2.0","method":"login","params":{"login":"x","pass":"x"}}\n')
print("   stratum answers:", json.loads(s.makefile().readline())["error"]["message"][:40])
PY
echo
echo "Pool wallet address: $ADDR"
echo "Back up its seed now (on the server as root): sudo -u xmzwallet monzero-wallet-cli --wallet-file /var/lib/xmzwallet/pool --password-file /etc/xmzwallet/password --daemon-address 127.0.0.1:6185   then type: seed   and write the 25 words down offline; then: exit"
REMOTE

echo
echo "== from outside"
curl -s -m 10 http://node.monzero.org:8117/api/stats >/dev/null && echo "   http://node.monzero.org:8117/ reachable" || echo "   port 8117 not reachable from here yet (IONOS cloud firewall?)"
timeout 5 bash -c 'exec 3<>/dev/tcp/node.monzero.org/3333' 2>/dev/null && echo "   stratum node.monzero.org:3333 reachable" || echo "   stratum 3333 not reachable yet (IONOS cloud firewall?)"
