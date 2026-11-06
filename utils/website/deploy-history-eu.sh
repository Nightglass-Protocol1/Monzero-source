#!/bin/bash
# Install the network history collector on monzero-eu: one observation per minute from
# https://monzero.org/stats/api.php into a private archive, and an upload of the six public
# exports to the website (/network/data/) every five minutes. Safe to re-run; keeps the archive.
# Run from the owner's PC:  bash utils/website/deploy-history-eu.sh
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
EU=${EU_HOST:-monzero-eu} WEB=${WEB_HOST:-monzero-webspace}

echo "== copying the collector to $EU"
scp -q "$HERE/network_history.py" "$HERE/history_upload.py" "$EU:/tmp/"
ssh -o BatchMode=yes "$EU" sudo bash -s <<'REMOTE'
set -euo pipefail
id xmznethistory >/dev/null 2>&1 || useradd --system --home /var/lib/xmznethistory --shell /usr/sbin/nologin xmznethistory
install -d -o xmznethistory -g xmznethistory -m 750 /var/lib/xmznethistory
install -d -o xmznethistory -g xmznethistory -m 700 /var/lib/xmznethistory/archive
install -d -o xmznethistory -g xmznethistory -m 755 /var/lib/xmznethistory/public
install -d -o root -g xmznethistory -m 750 /etc/xmznethistory
install -d -o root -g root -m 755 /opt/xmznethistory
install -o root -g root -m 644 /tmp/network_history.py /tmp/history_upload.py /opt/xmznethistory/ && rm -f /tmp/network_history.py /tmp/history_upload.py
(umask 077; [ -s /etc/xmznethistory/secret ] || openssl rand -hex 32 > /etc/xmznethistory/secret)
chown root:xmznethistory /etc/xmznethistory/secret; chmod 640 /etc/xmznethistory/secret

common='User=xmznethistory
Group=xmznethistory
NoNewPrivileges=yes
ProtectSystem=strict
ProtectHome=yes
PrivateTmp=yes
UMask=0077
ReadWritePaths=/var/lib/xmznethistory'
cat > /etc/systemd/system/xmznethistory-collect.service <<EOF
[Unit]
Description=Monzero network history: record one observation and export
After=network-online.target
Wants=network-online.target
[Service]
Type=oneshot
ExecStart=/usr/bin/python3 -I /opt/xmznethistory/network_history.py --source live --database /var/lib/xmznethistory/archive/observations.sqlite --output-directory /var/lib/xmznethistory/public
TimeoutStartSec=60
Nice=10
$common
EOF
cat > /etc/systemd/system/xmznethistory-collect.timer <<'EOF'
[Unit]
Description=Network history observation every minute
[Timer]
OnBootSec=2min
OnUnitInactiveSec=60s
AccuracySec=5s
[Install]
WantedBy=timers.target
EOF
cat > /etc/systemd/system/xmznethistory-upload.service <<EOF
[Unit]
Description=Monzero network history: upload public exports to the website
After=network-online.target
Wants=network-online.target
[Service]
Type=oneshot
ExecStart=/usr/bin/python3 -I /opt/xmznethistory/history_upload.py --directory /var/lib/xmznethistory/public --secret-file /etc/xmznethistory/secret
TimeoutStartSec=90
$common
EOF
cat > /etc/systemd/system/xmznethistory-upload.timer <<'EOF'
[Unit]
Description=Network history upload every five minutes
[Timer]
OnBootSec=4min
OnUnitInactiveSec=5min
[Install]
WantedBy=timers.target
EOF
systemctl daemon-reload
echo "== first observation"
systemctl start xmznethistory-collect.service
journalctl -u xmznethistory-collect -n 3 --no-pager -o cat | grep -v '^Starting\|^Finished' || true
ls /var/lib/xmznethistory/public
REMOTE

echo "== giving the website the upload secret"
ssh -o BatchMode=yes "$EU" sudo cat /etc/xmznethistory/secret | ssh -o BatchMode=yes "$WEB" \
  'umask 077; read -r s; case "$s" in *[!0-9a-f]*|"") echo "!! bad secret"; exit 1;; esac; [ ${#s} -eq 64 ] || { echo "!! bad secret"; exit 1; }
   printf "<?php return [\"secret\" => \"%s\"];\n" "$s" > ~/monzero-private/network-history-config.php.tmp && mv ~/monzero-private/network-history-config.php.tmp ~/monzero-private/network-history-config.php && echo "   website config written"'

echo "== first upload and timers"
ssh -o BatchMode=yes "$EU" 'sudo systemctl start xmznethistory-upload.service; sudo journalctl -u xmznethistory-upload -n 2 --no-pager -o cat | grep -v "^Starting\|^Finished"; sudo systemctl enable --now xmznethistory-collect.timer xmznethistory-upload.timer >/dev/null 2>&1; systemctl is-active xmznethistory-collect.timer xmznethistory-upload.timer'
curl -s -m 15 https://monzero.org/network/data/24h.json | head -c 300; echo
