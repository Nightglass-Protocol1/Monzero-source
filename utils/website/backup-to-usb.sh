#!/bin/bash
# Encrypted backups of the website's private data (webspace) and the EU node's service data to the
# owner's USB drives. Run by hand or nightly (monzero-backup.timer). See utils/website/BACKUPS.md.
#   bash utils/website/backup-to-usb.sh            # back up, copy to every connected drive, verify
#   bash utils/website/backup-to-usb.sh --list     # show the backups on each drive
# Databases are copied with SQLite's online snapshot (VACUUM INTO / backup API), so the services keep
# running. Archives are encrypted to the backup key (gpg) before they touch a drive.
set -euo pipefail
KEY=${MONZERO_BACKUP_KEY:-29505BF7DC5B2A73136690E1AEB50246E151A3BC}
WEB=${WEB_HOST:-monzero-webspace} EU=${EU_HOST:-monzero-eu}
KEEP=${MONZERO_BACKUP_KEEP:-14}
# USB drives as name:uuid:<filesystem UUID>[:folder on the drive] (UUIDs survive replugging);
# a drive that is not connected is skipped. The SSD's top folder belongs to root, so it uses a subfolder.
TARGETS=${MONZERO_BACKUP_TARGETS:-"monzero-transfer-ssd:uuid:68aef467-63b2-42aa-9615-8db384081399:Monzero-App/Monzero-backups seagate-1tb:uuid:0BA2-2A03:Monzero-backups"}
log() { printf '%s %s\n' "$(date -u +%H:%M:%S)" "$*"; }

folder_for() { local rest=${1#*:*:*:}; [ "$rest" != "$1" ] && echo "$rest" || echo Monzero-backups; }

mountpoint_for() {  # "<name>:label|uuid:<value>[:folder]" -> mount point (mounting it read-write if needed), or nothing
  IFS=: read -r _ kind value _ <<< "$1"; local dev
  dev=$( [ "$kind" = label ] && echo "/dev/disk/by-label/$value" || echo "/dev/disk/by-uuid/$value" )
  [ -e "$dev" ] || return 0
  local mp; mp=$(findmnt -n -o TARGET --source "$(readlink -f "$dev")" | head -1 || true)
  [ -n "$mp" ] || { udisksctl mount -b "$dev" >/dev/null 2>&1 || true; mp=$(findmnt -n -o TARGET --source "$(readlink -f "$dev")" | head -1 || true); }
  if [ -n "$mp" ] && findmnt -n -o OPTIONS --target "$mp" | grep -qw rw; then echo "$mp"; fi
}

if [ "${1:-}" = --list ]; then
  for t in $TARGETS; do mp=$(mountpoint_for "$t"); DIR=$(folder_for "$t"); [ -n "$mp" ] && { echo "== $mp/$DIR"; ls -lh "$mp/$DIR" 2>/dev/null | tail -n +2 || echo "   (empty)"; }; done
  exit 0
fi

WORK=$(mktemp -d "${XDG_CACHE_HOME:-$HOME/.cache}/monzero-backup.XXXXXX"); chmod 700 "$WORK"; trap 'rm -rf "$WORK"' EXIT
STAMP=$(date -u +%Y%m%dT%H%M%SZ)

log "webspace: snapshot private data and admin-edited content"
ssh -o BatchMode=yes "$WEB" bash -s > "$WORK/webspace.tar" <<'REMOTE'
set -euo pipefail
f=$(ls ~/*/admin/bootstrap.php); ROOT=$(dirname "$(dirname "$f")")
T=$(mktemp -d ~/monzero-private/.backup-XXXXXX); trap 'rm -rf "$T"' EXIT
mkdir -p "$T/stage/home" "$T/stage/docroot"
tar -C ~ -cf - --exclude='monzero-private/.backup-*' --exclude='*.sqlite' --exclude='*.sqlite-wal' --exclude='*.sqlite-shm' --exclude='*.lock' monzero-private | tar -C "$T/stage/home" -xf -
find ~/monzero-private -name '*.sqlite' -not -path '*/.backup-*' | while read -r db; do
  rel=${db#"$HOME"/}; mkdir -p "$T/stage/home/$(dirname "$rel")"
  php -r '$s = new PDO("sqlite:" . $argv[1]); $s->setAttribute(PDO::ATTR_ERRMODE, PDO::ERRMODE_EXCEPTION); $s->exec("PRAGMA busy_timeout = 10000"); $s->exec("VACUUM INTO " . $s->quote($argv[2]));' "$db" "$T/stage/home/$rel"
done
cd "$ROOT"
for p in community/announcements.json roadmap/updates.json exchanges/catalog.json ecosystem/catalog.json community/bounties/catalog.json mining/benchmarks/catalog.json network/data/*.json news/data/*.json; do
  [ -f "$p" ] && { mkdir -p "$T/stage/docroot/$(dirname "$p")"; cp -p "$p" "$T/stage/docroot/$p"; }
done
if [ -f ~/.monzero-miner-stats.php ]; then
  cp -p ~/.monzero-miner-stats.php "$T/stage/home/"
  stats=$(php -r '$c = @include getenv("HOME") . "/.monzero-miner-stats.php"; echo is_array($c) ? ($c["MONZERO_STATS_FILE"] ?? "") : "";')
  for s in "$stats" "$stats.history.json"; do [ -n "$s" ] && [ -f "$s" ] && { mkdir -p "$T/stage/miner-stats"; cp -p "$s" "$T/stage/miner-stats/"; }; done
fi
tar -C "$T/stage" -cf - .
REMOTE

log "EU node: snapshot pool, faucet, history and service settings"
ssh -o BatchMode=yes "$EU" sudo bash -s > "$WORK/eu-node.tar" <<'REMOTE'
set -euo pipefail
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
mkdir -p "$T/stage"
python3 - "$T/stage" <<'PY'
import glob, os, sqlite3, sys
for db in glob.glob("/var/lib/xmz*/**/*.sqlite", recursive=True):
    out = os.path.join(sys.argv[1], db.lstrip("/"))
    os.makedirs(os.path.dirname(out), exist_ok=True)
    src = sqlite3.connect(f"file:{db}?mode=ro", uri=True, timeout=30)
    dst = sqlite3.connect(out)
    src.backup(dst); dst.close(); src.close()
PY
# Wallet files (the seed is the real backup), payout journals, settings and secrets. Not the
# country database or public exports, which are downloaded or regenerated.
tar -C / -cf - --ignore-failed-read --exclude='*.sqlite' --exclude='*.sqlite-wal' --exclude='*.sqlite-shm' --exclude='*.csv.gz' --exclude='var/lib/xmznethistory/public' \
  etc/xmzpool etc/xmzwallet etc/xmzfaucet etc/xmznodemap etc/xmznethistory etc/xmzstatus etc/xmznews etc/monzero \
  var/lib/xmzpool var/lib/xmzwallet var/lib/xmzfaucet 2>/dev/null | tar -C "$T/stage" -xf -
tar -C "$T/stage" -cf - .
REMOTE

for part in webspace eu-node; do
  tar -tf "$WORK/$part.tar" >/dev/null || { log "!! $part archive is damaged; nothing copied"; exit 1; }
  gpg --batch --yes --trust-model always -e -r "$KEY" -o "$WORK/$STAMP-$part.tar.gpg" "$WORK/$part.tar"
  rm -f "$WORK/$part.tar"
done
( cd "$WORK" && sha256sum "$STAMP"-*.tar.gpg > "$STAMP.sha256" )

copied=0
for t in $TARGETS; do
  mp=$(mountpoint_for "$t")
  if [ -z "$mp" ]; then log "skip ${t%%:*}: not connected or read-only"; continue; fi
  DIR=$(folder_for "$t")
  mkdir -p "$mp/$DIR"
  cp "$WORK/$STAMP"-*.tar.gpg "$WORK/$STAMP.sha256" "$mp/$DIR/"
  sync -f "$mp/$DIR/$STAMP.sha256" 2>/dev/null || sync
  ( cd "$mp/$DIR" && sha256sum -c --quiet "$STAMP.sha256" ) || { log "!! copy on $mp failed verification"; exit 1; }
  # Keep the newest $KEEP backups on this drive.
  ls -1 "$mp/$DIR"/*.sha256 2>/dev/null | sort | head -n -"$KEEP" | while read -r old; do
    base=${old%.sha256}; rm -f "$base"-*.tar.gpg "$old"; log "removed old backup $(basename "$base") from $mp"
  done
  log "copied to $mp/$DIR ($(du -ch "$mp/$DIR/$STAMP"-*.tar.gpg | tail -1 | cut -f1))"
  copied=$((copied + 1)); last="$mp/$DIR"
done
[ "$copied" -gt 0 ] || { log "!! no backup drive connected; nothing was saved"; exit 1; }

log "verify: decrypt the copy on $last and check every database"
CHECK="$WORK/check"; mkdir -p "$CHECK"
for part in webspace eu-node; do
  gpg --batch -q -d "$last/$STAMP-$part.tar.gpg" | tar -C "$CHECK" -xf -
done
bad=0; n=0
while read -r db; do
  n=$((n + 1)); r=$(sqlite3 "file:$db?mode=ro" 'PRAGMA integrity_check;' 2>&1 | head -1)
  [ "$r" = ok ] || { log "!! $(basename "$db"): $r"; bad=1; }
done < <(find "$CHECK" -name '*.sqlite')
[ "$bad" -eq 0 ] || exit 1
log "backup $STAMP done: $n databases OK, saved on $copied drive(s)"
