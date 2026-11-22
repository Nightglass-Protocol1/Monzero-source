#!/bin/bash
# Automatic updates for the Docker edition (run daily by the systemd timer next to this file):
# if monzero.org names a newer release, rebuild the images for it (the build itself checks the
# signature and checksums and stops on any mismatch), then restart the node and relay. The
# wallet is only restarted when it is not running, so an open session is never cut off.
set -euo pipefail
cd "$(dirname "$0")"
CUR=$(cat .monzero-version 2>/dev/null || echo 0)
NEW=$(curl -fsS --max-time 30 https://monzero.org/releases/latest.json | python3 -I -c 'import json,re,sys; v=json.load(sys.stdin)["version"]; assert re.fullmatch(r"\d+(\.\d+){1,3}", v); print(v)')
newer=$(python3 -I -c 'import sys; k=lambda v: tuple(map(int, v.split("."))); print(1 if k(sys.argv[2]) > k(sys.argv[1]) else 0)' "$CUR" "$NEW")
[ "$newer" = 1 ] || { echo "up to date ($CUR)"; exit 0; }
bash ./build-images.sh "$NEW"
echo "$NEW" > .monzero-version
export MONZERO_VERSION=$NEW
docker compose up -d node relay
if [ -z "$(docker compose ps -q wallet 2>/dev/null)" ]; then
  echo "wallet image updated; it starts on the new version next time"
else
  echo "wallet is running: restart it when convenient (docker compose --profile wallet up -d wallet)"
fi
echo "updated to $NEW"
