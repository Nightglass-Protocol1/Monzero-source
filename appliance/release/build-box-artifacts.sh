#!/bin/bash
# Release artifacts for Monzero boxes, built next to the normal release (run before signing):
#   bash appliance/release/build-box-artifacts.sh <version> <out dir>
# Makes  monzero-v<version>-linux-x86_64-relay.tar.gz  and  ...-linux-aarch64-relay.tar.gz
# (the DEX relay from this checkout, portable builds) with .sha256 files, plus
#   artifacts-box.json   entries to add to v<version>.json "artifacts" before it is signed
#   latest.json          {"version": "<version>"}: upload to monzero.org/releases/ AFTER the
#                        signed release file and archives are up (boxes check it daily)
# The aarch64 node archive comes from appliance/pi/build-arm64-node.sh; add its entry too.
set -euo pipefail
V=${1:?usage: build-box-artifacts.sh <version> <out dir>}
OUT=${2:?out dir}
HERE=$(cd "$(dirname "$0")/.." && pwd); REPO=$(cd "$HERE/.." && pwd)
ROOT=${MONZERO_ROOT:-$HOME/projects/Monzero-App}
mkdir -p "$OUT"
. "$HERE/build/relay.sh"
X86=$(build_relay "$REPO" "$ROOT" | tail -1)
ARM=$(build_relay_arm64 "$REPO" "$ROOT" | tail -1)
[ -z "$(readelf -n "$X86" | grep 'ISA needed')" ] || { echo "!! x86-64 relay needs a newer CPU level"; exit 1; }
entries=()
for pair in "x86_64:$X86" "aarch64:$ARM"; do
  arch=${pair%%:*}; bin=${pair#*:}
  N=monzero-v$V-linux-$arch-relay
  W=$(mktemp -d); mkdir "$W/$N"
  install -m 755 "$bin" "$W/$N/monzero-dex-relay"
  printf 'Monzero DEX relay v%s for %s Linux (from commit %s). Monzero boxes install it through monzero-update.\n' "$V" "$arch" "$(git -C "$REPO" rev-parse --short HEAD)" > "$W/$N/README.txt"
  (cd "$W" && tar --owner=0 --group=0 --sort=name --mtime="2026-01-01 00:00Z" -czf "$OUT/$N.tar.gz" "$N")
  (cd "$OUT" && sha256sum "$N.tar.gz" > "$N.tar.gz.sha256")
  rm -rf "$W"
  entries+=("$(python3 -I -c 'import json,os,sys; p=sys.argv[1]; print(json.dumps({"filename": os.path.basename(p), "size_bytes": os.path.getsize(p), "sha256": open(p+".sha256").read().split()[0], "platform": "linux-"+sys.argv[2]+"-relay", "contains": "the Monzero DEX relay (offers board; no funds, no keys) for Monzero boxes"}))' "$OUT/$N.tar.gz" "$arch")")
done
printf '%s\n' "${entries[@]}" | python3 -I -c 'import json,sys; print(json.dumps([json.loads(l) for l in sys.stdin], indent=1))' > "$OUT/artifacts-box.json"
printf '{"version": "%s"}\n' "$V" > "$OUT/latest.json"
ls -la "$OUT"
