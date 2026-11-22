#!/bin/sh
# The wallet on a virtual screen, shown in the browser at https://<host>:6080/vnc.html
# The VNC password: $MONZERO_VNC_PASSWORD, or one made on the first start and kept in the
# volume (printed in the container log once: docker compose logs wallet).
set -eu
S=$HOME/.monzero-screen; mkdir -p "$S"; chmod 700 "$S"
if [ -n "${MONZERO_VNC_PASSWORD:-}" ]; then
  x11vnc -storepasswd "$MONZERO_VNC_PASSWORD" "$S/passwd" >/dev/null
elif [ ! -f "$S/passwd" ]; then
  PW=$(python3 -I -c 'import secrets; print(secrets.token_urlsafe(6)[:8])' 2>/dev/null || openssl rand -base64 6)
  x11vnc -storepasswd "$PW" "$S/passwd" >/dev/null
  echo "Wallet screen password (first start, kept in the volume): $PW"
fi
if [ ! -f "$S/tls.crt" ]; then
  openssl req -x509 -newkey ec -pkeyopt ec_paramgen_curve:P-256 -nodes -days 3650 -subj "/CN=monzero-wallet" \
    -keyout "$S/tls.key" -out "$S/tls.crt" 2>/dev/null
  echo "Screen certificate SHA-256: $(openssl x509 -in "$S/tls.crt" -noout -fingerprint -sha256 | cut -d= -f2)"
fi
Xvfb :0 -screen 0 ${MONZERO_SCREEN:-1600x1000}x24 -nolisten tcp &
export DISPLAY=:0
sleep 1
CONF="$HOME/.config/monzero-project/monzero-core.conf"
[ -f "$CONF" ] || { mkdir -p "$(dirname "$CONF")"; printf '[General]\naskDesktopShortcut=false\n' > "$CONF"; }
openbox --config-file /usr/local/lib/monzero-wallet/openbox-rc.xml &
( while true; do /opt/monzero/monzero-wallet-gui; sleep 2; done ) &
x11vnc -display :0 -localhost -rfbport 5900 -rfbauth "$S/passwd" -forever -shared -quiet &
exec websockify --cert="$S/tls.crt" --key="$S/tls.key" --ssl-only --web=/usr/share/novnc 6080 127.0.0.1:5900
