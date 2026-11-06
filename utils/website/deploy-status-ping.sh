#!/bin/bash
# Install the 10-minute status check-in on monzero-eu and monzero-us and give the website the
# shared secret and the alert address. Safe to re-run; keeps the secret.
# Run from the owner's PC:  bash utils/website/deploy-status-ping.sh [alert-email]
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
WEB=${WEB_HOST:-monzero-webspace}
ALERT_TO=${1:-security@monzero.org}
case "$ALERT_TO" in *@*.*) ;; *) echo "!! alert address must be an email address"; exit 2;; esac

install_node() {  # host node-name
  echo "== installing the check-in on $1"
  scp -q "$HERE/status_ping.py" "$1:/tmp/status_ping.py"
  ssh -o BatchMode=yes "$1" sudo bash -s -- "$2" <<'REMOTE'
set -euo pipefail
id xmzstatus >/dev/null 2>&1 || useradd --system --home /nonexistent --no-create-home --shell /usr/sbin/nologin xmzstatus
install -d -o root -g xmzstatus -m 750 /etc/xmzstatus
install -d -o root -g root -m 755 /opt/xmzstatus
install -o root -g root -m 644 /tmp/status_ping.py /opt/xmzstatus/status_ping.py && rm -f /tmp/status_ping.py
cat > /etc/systemd/system/xmzstatus-ping.service <<EOF
[Unit]
Description=Monzero status check-in with monzero.org
After=network-online.target
Wants=network-online.target
[Service]
Type=oneshot
User=xmzstatus
Group=xmzstatus
ExecStart=/usr/bin/python3 -I /opt/xmzstatus/status_ping.py --node $1 --secret-file /etc/xmzstatus/secret
TimeoutStartSec=90
NoNewPrivileges=yes
ProtectSystem=strict
ProtectHome=yes
PrivateTmp=yes
EOF
cat > /etc/systemd/system/xmzstatus-ping.timer <<'EOF'
[Unit]
Description=Monzero status check-in every 10 minutes
[Timer]
OnBootSec=3min
OnUnitActiveSec=10min
RandomizedDelaySec=60
[Install]
WantedBy=timers.target
EOF
systemctl daemon-reload
REMOTE
}

install_node monzero-eu eu
install_node monzero-us us

echo "== shared secret (created on monzero-eu, copied to monzero-us and the website)"
ssh -o BatchMode=yes monzero-eu 'sudo bash -c "umask 027; [ -s /etc/xmzstatus/secret ] || openssl rand -hex 32 > /etc/xmzstatus/secret; chown root:xmzstatus /etc/xmzstatus/secret; chmod 640 /etc/xmzstatus/secret"'
ssh -o BatchMode=yes monzero-eu sudo cat /etc/xmzstatus/secret \
  | ssh -o BatchMode=yes monzero-us 'sudo bash -c "umask 027; cat > /etc/xmzstatus/secret; chown root:xmzstatus /etc/xmzstatus/secret; chmod 640 /etc/xmzstatus/secret"'
{ ssh -o BatchMode=yes monzero-eu sudo cat /etc/xmzstatus/secret; printf '%s\n' "$ALERT_TO"; } | ssh -o BatchMode=yes "$WEB" \
  'umask 077; read -r s; read -r to; case "$s" in *[!0-9a-f]*|"") echo "!! bad secret"; exit 1;; esac; [ ${#s} -eq 64 ] || { echo "!! bad secret"; exit 1; }
   case "$to" in *[!A-Za-z0-9@._+-]*|"") echo "!! bad alert address"; exit 1;; esac
   printf "<?php return [\"secret\" => \"%s\", \"alert_to\" => \"%s\"];\n" "$s" "$to" > ~/monzero-private/status-config.php.tmp && mv ~/monzero-private/status-config.php.tmp ~/monzero-private/status-config.php && echo "   website config written (alerts to $to)"'

echo "== first check-ins and timers"
for host in monzero-eu monzero-us; do
  ssh -o BatchMode=yes "$host" 'sudo systemctl start xmzstatus-ping.service; sudo systemctl enable --now xmzstatus-ping.timer >/dev/null 2>&1; sudo journalctl -u xmzstatus-ping -n 1 --no-pager -o cat'
done
curl -s -m 20 https://monzero.org/api/status/ | python3 -c 'import sys,json;d=json.load(sys.stdin);print("overall:",d["overall"]);[print("  ",c["state"].ljust(4),c["label"],"-",c["detail"]) for c in d["checks"]]'
