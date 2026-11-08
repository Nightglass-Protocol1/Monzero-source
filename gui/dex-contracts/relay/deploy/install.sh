#!/bin/sh
# Installs or updates the relay on a server: sudo sh install.sh <relay1|relay2> <path-to-binary>
# Binary and config are replaced; the state folder (/var/lib/monzero-dex-relay) is kept.
set -eu
name="$1"; binary="$2"; here=$(dirname "$0")
id -u monzero-relay >/dev/null 2>&1 || useradd --system --home /nonexistent --shell /usr/sbin/nologin monzero-relay
install -d -m 0755 /opt/monzero-dex-relay /etc/monzero-dex-relay
install -m 0755 "$binary" /opt/monzero-dex-relay/monzero-dex-relay.new
mv /opt/monzero-dex-relay/monzero-dex-relay.new /opt/monzero-dex-relay/monzero-dex-relay
install -m 0644 "$here/config-$name.json" /etc/monzero-dex-relay/config.json
install -m 0644 "$here/monzero-dex-relay.service" /etc/systemd/system/monzero-dex-relay.service
systemctl daemon-reload
systemctl enable monzero-dex-relay >/dev/null 2>&1
systemctl restart monzero-dex-relay
sleep 2
systemctl is-active monzero-dex-relay
