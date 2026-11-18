#!/usr/bin/env bash
set -euo pipefail

[[ $# -eq 1 ]] || { echo "Usage: $0 <monzero-gui-source.tar.gz>" >&2; exit 2; }
archive=$(realpath "$1")
[[ -f $archive ]] || { echo "GUI source archive not found: $archive" >&2; exit 2; }
script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)

python3 - "$archive" <<'PY'
import pathlib, posixpath, sys, tarfile

with tarfile.open(sys.argv[1], "r:gz") as source:
    members = source.getmembers()
    if not members:
        raise SystemExit("GUI source archive is empty")
    roots = set()
    for member in members:
        path = pathlib.PurePosixPath(member.name)
        if path.is_absolute() or ".." in path.parts or not path.parts:
            raise SystemExit(f"Unsafe GUI source archive path: {member.name}")
        roots.add(path.parts[0])
        if member.isdev() or member.isfifo():
            raise SystemExit(f"Special file is forbidden: {member.name}")
        if member.issym() or member.islnk():
            target = pathlib.PurePosixPath(posixpath.normpath(posixpath.join(*path.parts[:-1], member.linkname)))
            if target.is_absolute() or ".." in target.parts or not target.parts or target.parts[0] != path.parts[0]:
                raise SystemExit(f"Archive link escapes package root: {member.name} -> {member.linkname}")
    if len(roots) != 1:
        raise SystemExit("GUI source archive must contain exactly one root directory")
PY

work_dir=$(mktemp -d "${TMPDIR:-/tmp}/monzero-gui-source-verify.XXXXXX")
trap 'rm -rf -- "$work_dir"' EXIT
tar -xzf "$archive" -C "$work_dir"
mapfile -t roots < <(find "$work_dir" -mindepth 1 -maxdepth 1 -type d)
[[ ${#roots[@]} -eq 1 ]] || { echo "GUI source archive root is invalid" >&2; exit 1; }
root=${roots[0]}
[[ -f $root/SOURCE-MANIFEST.txt && -f $root/SHA256SUMS ]] || {
  echo "GUI source manifest or checksums are missing" >&2; exit 1;
}
[[ $(sed -n 's/^package=//p' "$root/SOURCE-MANIFEST.txt") == "$(basename "$root")" ]] || {
  echo "GUI source manifest package name does not match archive root" >&2; exit 1;
}
"$script_dir/verify-release-notes.sh" "$root/SOURCE-MANIFEST.txt" -gui-source "$root/monero/docs/RELEASE_NOTES.md"
gui_commit=$(sed -n 's/^gui_source_commit=//p' "$root/SOURCE-MANIFEST.txt")
core_commit=$(sed -n 's/^core_source_commit=//p' "$root/SOURCE-MANIFEST.txt")
source_epoch=$(sed -n 's/^source_date_epoch=//p' "$root/SOURCE-MANIFEST.txt")
[[ $gui_commit =~ ^[0-9a-f]{40}$ ]] || { echo "Invalid GUI commit in manifest" >&2; exit 1; }
[[ $core_commit =~ ^[0-9a-f]{40}$ ]] || { echo "Invalid core commit in manifest" >&2; exit 1; }
[[ $source_epoch =~ ^[0-9]+$ ]] || { echo "Invalid source epoch in manifest" >&2; exit 1; }
grep -Fqx $'monero\t'"$core_commit" "$root/SOURCE-MANIFEST.txt" || {
  echo "GUI source manifest does not pin the packaged core commit" >&2; exit 1;
}
awk '
  /^submodules:$/ { found=1; next }
  found && NF && ($1 !~ /^[A-Za-z0-9._\/-]+$/ || $2 !~ /^[0-9a-f]{40}$/ || NF != 2) { bad=1 }
  END { exit !(found && !bad) }
' "$root/SOURCE-MANIFEST.txt" || { echo "Invalid GUI submodule manifest" >&2; exit 1; }
if find "$root" -type d -name .git -print -quit | grep -q .; then
  echo "GUI source archive contains embedded Git metadata" >&2; exit 1
fi
(cd "$root" && sha256sum -c SHA256SUMS)
echo "GUI source package verification passed: $archive"
