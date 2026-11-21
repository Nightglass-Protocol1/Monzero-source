#!/bin/bash
# Our own Dogecoin node + Electrum server, so the light wallets (DOGE) need not rely on public
# Electrum servers. Installs the official Dogecoin Core release (signature and checksum
# verified), runs dogecoind under systemd as its own user, and ElectrumX (Electrum protocol,
# Dogecoin support built in) next to it.
#
# Run as root on the node server (Debian/Ubuntu), from the owner's PC for example:
#   scp install-dogecoin-node.sh SERVER: && ssh SERVER sudo bash install-dogecoin-node.sh install
#   ssh SERVER sudo bash install-dogecoin-node.sh status
#   ssh SERVER sudo bash install-dogecoin-node.sh tls doge.example.org   # public TLS (50002), after DNS
# Safe to run again (an upgrade: set DOGECOIN_VERSION / ELECTRUMX_VERSION).
#
# Ports: 22556 peer-to-peer (open to all, helps the network); 22555 node RPC (this machine
# only); 50001 Electrum TCP (this machine only); 50002 Electrum over TLS (open, after `tls`).
# Disk: the whole chain (unpruned: ElectrumX needs every block) about 80-100 GB plus about
# 15-25 GB for ElectrumX's index. `install` refuses to start syncing with less than
# MIN_FREE_GB free. First sync: about a day for the node, then some hours for the index.
set -euo pipefail
VERSION=${DOGECOIN_VERSION:-1.14.9}
EX_VERSION=${ELECTRUMX_VERSION:-2.0.1}
MIN_FREE_GB=${MIN_FREE_GB:-160}
TARBALL=dogecoin-$VERSION-x86_64-linux-gnu.tar.gz
BASE=https://github.com/dogecoin/dogecoin/releases/download/v$VERSION
# Dogecoin Core releases are signed by the maintainers whose keys are in the dogecoin repository
# (contrib/gitian-keys); 1.14.9's SHA256SUMS.asc is signed by Patrick Lodder's key below.
SIGNER_FPR=${DOGECOIN_SIGNER_FPR:-DC6EF4A8BF9F1B1E4DE1EE522D3A345B98D0DC1F}
KEY_URL=https://raw.githubusercontent.com/dogecoin/dogecoin/v$VERSION/contrib/gitian-keys/patricklodder-key.pgp
CMD=${1:-}

rpc() { curl -s -m 10 --user "$(cat /etc/dogecoin/rpc-user):$(cat /etc/dogecoin/rpc-password)" -H 'content-type: text/plain' \
  --data "{\"jsonrpc\":\"1.0\",\"id\":0,\"method\":\"$1\",\"params\":${2:-[]}}" http://127.0.0.1:22555/; }

case "$CMD" in
  status)
    echo "== dogecoind: $(systemctl is-active dogecoind 2>/dev/null || true)"
    rpc getblockchaininfo | python3 -c 'import sys,json; r=json.load(sys.stdin)["result"]; print("height %s of %s headers, %.2f%% verified, %s GB on disk" % (r["blocks"], r["headers"], 100*r["verificationprogress"], round(r.get("size_on_disk",0)/1e9,1)))' 2>/dev/null || echo "   (not answering yet)"
    echo "== electrumx: $(systemctl is-active electrumx 2>/dev/null || true)"
    journalctl -u electrumx -n 200 --no-pager 2>/dev/null | grep -E 'height|caught up|flush|ERROR' | tail -2 || true
    du -sh /var/lib/dogecoin /var/lib/electrumx 2>/dev/null || true
    df -h /var/lib | tail -1
    exit 0;;
  tls)
    NAME=${2:-}; [ -n "$NAME" ] || { echo "usage: $0 tls <dns name pointing at this server>"; exit 2; }
    [ "$(id -u)" = 0 ] || { echo "run as root (sudo)"; exit 1; }
    command -v caddy >/dev/null || { echo "!! needs Caddy (it fetches and renews the certificate)"; exit 1; }
    grep -q "^$NAME {" /etc/caddy/Caddyfile || { printf '\n%s {\n\trespond "Monzero Dogecoin Electrum server: %s:50002 (TLS)" 200\n}\n' "$NAME" "$NAME" >> /etc/caddy/Caddyfile; systemctl reload caddy; }
    CERTS=/var/lib/caddy/.local/share/caddy/certificates
    echo "== waiting for Caddy's certificate for $NAME"
    for _ in $(seq 1 60); do ls $CERTS/*/"$NAME"/"$NAME".crt >/dev/null 2>&1 && break; sleep 5; done
    ls $CERTS/*/"$NAME"/"$NAME".crt >/dev/null 2>&1 || { echo "!! no certificate yet: check that $NAME points at this server"; exit 1; }
    # Copy the certificate where ElectrumX (its own user) can read it; a daily timer repeats this
    # after Caddy renews it and restarts ElectrumX only when it changed.
    cat > /usr/local/sbin/electrumx-cert-sync <<EOF
#!/bin/bash
set -e
SRC=\$(ls -d $CERTS/*/$NAME | head -1)
if ! cmp -s "\$SRC/$NAME.crt" /etc/electrumx/tls.crt; then
  install -o root -g electrumx -m 640 "\$SRC/$NAME.crt" /etc/electrumx/tls.crt
  install -o root -g electrumx -m 640 "\$SRC/$NAME.key" /etc/electrumx/tls.key
  systemctl try-restart electrumx
fi
EOF
    chmod 755 /usr/local/sbin/electrumx-cert-sync
    printf '[Unit]\nDescription=Copy the renewed TLS certificate to ElectrumX\n[Service]\nType=oneshot\nExecStart=/usr/local/sbin/electrumx-cert-sync\n' > /etc/systemd/system/electrumx-cert-sync.service
    printf '[Unit]\nDescription=Daily ElectrumX certificate check\n[Timer]\nOnCalendar=daily\nRandomizedDelaySec=1h\nPersistent=true\n[Install]\nWantedBy=timers.target\n' > /etc/systemd/system/electrumx-cert-sync.timer
    /usr/local/sbin/electrumx-cert-sync
    sed -i '/^SERVICES=/d; /^SSL_CERTFILE=/d; /^SSL_KEYFILE=/d; /^REPORT_SERVICES=/d' /etc/electrumx/electrumx.env
    printf 'SERVICES=tcp://127.0.0.1:50001,ssl://0.0.0.0:50002,rpc://127.0.0.1:8000\nREPORT_SERVICES=ssl://%s:50002\nSSL_CERTFILE=/etc/electrumx/tls.crt\nSSL_KEYFILE=/etc/electrumx/tls.key\n' "$NAME" >> /etc/electrumx/electrumx.env
    systemctl daemon-reload; systemctl enable --now electrumx-cert-sync.timer; systemctl restart electrumx
    ufw status | grep -q "Status: active" && ufw allow 50002/tcp comment "dogecoin electrum tls" >/dev/null
    echo "TLS on $NAME:50002 (also open TCP 50002 in the IONOS Cloud Panel firewall if it blocks)"
    exit 0;;
  install) ;;
  *) sed -n '2,19p' "$0"; exit 2;;
esac
[ "$(id -u)" = 0 ] || { echo "run as root (sudo)"; exit 1; }
[ "$(uname -m)" = x86_64 ] || { echo "this installs the x86_64 release"; exit 1; }

echo "== 1. packages"
apt-get update -qq
DEBIAN_FRONTEND=noninteractive apt-get install -y -qq curl gnupg ca-certificates ufw python3-venv >/dev/null

echo "== 2. Dogecoin Core $VERSION, verified"
if [ "$(/opt/dogecoin/dogecoind -version 2>/dev/null | grep -o 'v[0-9.]*' | head -1)" = "v$VERSION" ]; then
  echo "already installed"
else
  W=$(mktemp -d); trap 'rm -rf "$W"' EXIT
  export GNUPGHOME=$W/gnupg; mkdir -m 700 "$GNUPGHOME"
  curl -fsSL "$KEY_URL" -o "$W/key.asc"
  gpg -q --import "$W/key.asc" 2>/dev/null
  curl -fsSL "$BASE/SHA256SUMS.asc" -o "$W/SHA256SUMS.asc"
  # VALIDSIG: a correct signature by exactly this key (the key has since expired; it was valid
  # when it signed the release)
  gpg --status-fd 1 --verify "$W/SHA256SUMS.asc" 2>/dev/null | grep -q "VALIDSIG $SIGNER_FPR" || { echo "!! SHA256SUMS.asc is not signed by $SIGNER_FPR"; exit 1; }
  WANT=$(grep -E "^[0-9a-f]{64}  $TARBALL\$" "$W/SHA256SUMS.asc" | cut -d' ' -f1)
  [ -n "$WANT" ] || { echo "!! $TARBALL is not in the signed checksums"; exit 1; }
  curl -fSL --progress-bar "$BASE/$TARBALL" -o "$W/$TARBALL"
  echo "$WANT  $W/$TARBALL" | sha256sum -c --quiet || { echo "!! checksum mismatch"; exit 1; }
  tar -xzf "$W/$TARBALL" -C "$W"
  install -d /opt/dogecoin
  install -m 755 "$W"/dogecoin-$VERSION/bin/dogecoind "$W"/dogecoin-$VERSION/bin/dogecoin-cli /opt/dogecoin/
  echo "installed $(/opt/dogecoin/dogecoind -version | head -1)"
fi

echo "== 3. users, folders, config"
id dogecoin >/dev/null 2>&1 || useradd --system --home-dir /var/lib/dogecoin --shell /usr/sbin/nologin dogecoin
id electrumx >/dev/null 2>&1 || useradd --system --home-dir /var/lib/electrumx --shell /usr/sbin/nologin electrumx
install -d -o dogecoin -g dogecoin -m 750 /var/lib/dogecoin
install -d -o electrumx -g electrumx -m 750 /var/lib/electrumx
install -d -m 755 /etc/dogecoin
install -d -o root -g electrumx -m 750 /etc/electrumx
if [ ! -f /etc/dogecoin/rpc-password ]; then
  echo electrumx > /etc/dogecoin/rpc-user
  head -c 32 /dev/urandom | base64 | tr -dc A-Za-z0-9 > /etc/dogecoin/rpc-password
  chmod 600 /etc/dogecoin/rpc-password
fi
PASS=$(cat /etc/dogecoin/rpc-password)
# rpcauth line: the node keeps only a salted hash of the password
SALT=$(openssl rand -hex 16)
HASH=$(printf %s "$PASS" | openssl dgst -sha256 -hmac "$SALT" | awk '{print $NF}')
CONF=/etc/dogecoin/dogecoin.conf
if [ ! -f "$CONF" ]; then
  cat > "$CONF" <<EOF
# Monzero's own Dogecoin node. Written by install-dogecoin-node.sh; edit and restart dogecoind.
datadir=/var/lib/dogecoin
server=1
disablewallet=1
listen=1
port=22556
maxconnections=40
rpcbind=127.0.0.1
rpcallowip=127.0.0.1
rpcport=22555
rpcauth=electrumx:$SALT\$$HASH
dbcache=2000
# ElectrumX reads every block: no pruning, no txindex needed
EOF
  echo "wrote $CONF"
else
  echo "keeping $CONF"
fi

cat > /etc/systemd/system/dogecoind.service <<'EOF'
[Unit]
Description=Dogecoin node
After=network-online.target
Wants=network-online.target

[Service]
User=dogecoin
Group=dogecoin
ExecStart=/opt/dogecoin/dogecoind -conf=/etc/dogecoin/dogecoin.conf -printtoconsole=0
Restart=on-failure
RestartSec=30
TimeoutStopSec=600
LimitNOFILE=65536
NoNewPrivileges=yes
ProtectSystem=strict
ProtectHome=yes
PrivateTmp=yes
PrivateDevices=yes
ReadWritePaths=/var/lib/dogecoin

[Install]
WantedBy=multi-user.target
EOF

echo "== 4. ElectrumX $EX_VERSION"
if [ "$(/opt/electrumx/bin/pip show e-x 2>/dev/null | awk '/^Version/{print $2}')" != "$EX_VERSION" ]; then
  python3 -m venv /opt/electrumx
  /opt/electrumx/bin/pip install -q --upgrade pip
  /opt/electrumx/bin/pip install -q "e-x==$EX_VERSION" "plyvel==1.5.1"
fi
echo "installed ElectrumX $(/opt/electrumx/bin/pip show e-x | awk '/^Version/{print $2}')"
if [ ! -f /etc/electrumx/electrumx.env ]; then
  cat > /etc/electrumx/electrumx.env <<EOF
COIN=Dogecoin
NET=mainnet
DB_DIRECTORY=/var/lib/electrumx
DB_ENGINE=leveldb
DAEMON_URL=http://electrumx:$PASS@127.0.0.1:22555/
SERVICES=tcp://127.0.0.1:50001,rpc://127.0.0.1:8000
CACHE_MB=1200
MAX_SESSIONS=1000
COST_SOFT_LIMIT=1000
COST_HARD_LIMIT=10000
EOF
  chown root:electrumx /etc/electrumx/electrumx.env; chmod 640 /etc/electrumx/electrumx.env
fi
cat > /etc/systemd/system/electrumx.service <<'EOF'
[Unit]
Description=ElectrumX (Dogecoin)
After=dogecoind.service
Requires=dogecoind.service

[Service]
User=electrumx
Group=electrumx
EnvironmentFile=/etc/electrumx/electrumx.env
ExecStart=/opt/electrumx/bin/electrumx_server
ExecStop=/opt/electrumx/bin/electrumx_rpc -p 8000 stop
Restart=on-failure
RestartSec=60
TimeoutStopSec=900
LimitNOFILE=65536
NoNewPrivileges=yes
ProtectSystem=strict
ProtectHome=yes
PrivateTmp=yes
ReadWritePaths=/var/lib/electrumx

[Install]
WantedBy=multi-user.target
EOF
systemctl daemon-reload

echo "== 5. firewall"
if ufw status | grep -q "Status: active"; then
  ufw allow 22556/tcp comment "dogecoin p2p" >/dev/null
  echo "ufw: 22556 open (50002 opens with: $0 tls <name>)"
fi

FREE_GB=$(df -BG --output=avail /var/lib | tail -1 | tr -dc 0-9)
if [ "$FREE_GB" -lt "$MIN_FREE_GB" ]; then
  echo "!! only $FREE_GB GB free (want $MIN_FREE_GB): installed but NOT started. Free space, then:"
  echo "   systemctl enable --now dogecoind electrumx"
  exit 0
fi
systemctl enable --now dogecoind electrumx
sleep 5
systemctl is-active dogecoind electrumx || true
echo
echo "Started. The node syncs first (about a day), ElectrumX indexes as blocks arrive."
echo "Progress:  sudo bash $0 status     Public TLS after DNS:  sudo bash $0 tls <name>"
