#!/bin/sh
# Tor onion service for the relay (this container shares the relay's network). The onion name
# goes into the shared volume, where the relay picks it up and announces it.
set -eu
T=/home/monzero/tor; mkdir -p "$T/hs"; chmod 700 "$T" "$T/hs"
cat > "$T/torrc" <<RC
DataDirectory $T/data
SocksPort 0
HiddenServiceDir $T/hs/
HiddenServicePort 80 127.0.0.1:8790
RC
( while [ ! -s "$T/hs/hostname" ]; do sleep 2; done
  cp "$T/hs/hostname" /share/public-url.tmp && mv /share/public-url.tmp /share/public-url
  echo "relay shared as http://$(cat "$T/hs/hostname")" ) &
exec tor -f "$T/torrc"
