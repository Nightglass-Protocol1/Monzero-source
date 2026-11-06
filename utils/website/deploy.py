#!/usr/bin/env python3
"""Deploy website/ to the webspace: upload only changed files, never delete, back up, verify.

  python3 utils/website/deploy.py               # show the plan only
  python3 utils/website/deploy.py --apply       # upload, then verify and record the manifest
  python3 utils/website/deploy.py --rollback    # restore the files replaced by the last deploy

Rules:
  * Files are compared by SHA-256. New and changed files are uploaded; nothing is ever deleted.
  * Paths owned by the live site are never uploaded: runtime data, release metadata, downloads,
    the whitepaper and anything the admin area edits (see LIVE_OWNED).
  * The manifest of the last deploy lives in ~/monzero-private/deploy-manifest.json. A live file
    that differs from both this checkout and that manifest was changed on the server by someone
    else; the deploy stops unless --force names it.
  * Every replaced file is saved to ~/Monzero-backups/deploy-<time>/replaced-files.tar first.
Standard library only. DEPLOY_REMOTE=local:/path runs the remote side locally (tests).
"""
import argparse, fnmatch, hashlib, io, json, os, shlex, subprocess, sys, tarfile, time
from pathlib import Path

WEBSITE = Path(__file__).resolve().parents[2] / "website"
HOST = os.environ.get("DEPLOY_HOST", "monzero-webspace")
LIVE_OWNED = [
    "downloads/*", "releases/*", "WHITEPAPER.md", "network/data/*.json", "network/data/.history-*", "news/data/*.json", "news/data/.news-*",
    # Edited from /admin; the live copy is authoritative once the admin area manages it.
    "exchanges/catalog.json", "ecosystem/catalog.json", "community/bounties/catalog.json",
    "mining/benchmarks/catalog.json", "community/announcements.json", "roadmap/updates.json", "fund/goals.json", "fund/spending.json",
]
SKIP = ["*.pyc", "*/__pycache__/*", ".DS_Store", "*~"]


def owned(path):
    return any(fnmatch.fnmatch(path, pattern) for pattern in LIVE_OWNED)


def local_files():
    files = {}
    for path in sorted(WEBSITE.rglob("*")):
        rel = path.relative_to(WEBSITE).as_posix()
        if path.is_file() and not path.is_symlink() and not owned(rel) and not any(fnmatch.fnmatch(rel, s) for s in SKIP):
            files[rel] = hashlib.sha256(path.read_bytes()).hexdigest()
    return files


def remote(script, data=None):
    """Run a bash script in the live document root (found next to admin/bootstrap.php)."""
    target = os.environ.get("DEPLOY_REMOTE", "")
    prologue = ("set -euo pipefail; f=$(ls ~/*/admin/bootstrap.php); [ $(echo \"$f\" | wc -l) = 1 ]; "
                "ROOT=$(dirname \"$(dirname \"$f\")\"); cd \"$ROOT\"; ")
    if target.startswith("local:"):
        command = ["bash", "-c", f"set -euo pipefail; export HOME={shlex.quote(target[6:])}; " + prologue + script]
    else:
        command = ["ssh", "-o", "BatchMode=yes", HOST, "bash -c " + shlex.quote(prologue + script)]
    result = subprocess.run(command, input=data, capture_output=True, timeout=600)
    if result.returncode != 0:
        sys.exit(f"remote step failed: {result.stderr.decode(errors='replace').strip()[-800:]}")
    return result.stdout


def live_state(paths):
    """Hashes of the given paths on the server, and the last deploy manifest."""
    listing = "\n".join(paths).encode() + b"\n"
    out = remote("while IFS= read -r p; do [ -f \"$p\" ] && sha256sum -- \"$p\"; done; "
                 "echo '--manifest--'; cat ~/monzero-private/deploy-manifest.json 2>/dev/null || true", listing).decode()
    hashes_part, _, manifest_part = out.partition("--manifest--\n")
    hashes = {line[66:]: line[:64] for line in hashes_part.splitlines() if len(line) > 66}
    manifest = json.loads(manifest_part) if manifest_part.strip() else {}
    return hashes, manifest.get("files", {}), manifest


def plan(force):
    local = local_files()
    live, recorded, manifest = live_state(local)
    upload, conflicts = [], []
    for path, digest in local.items():
        current = live.get(path)
        if current == digest:
            continue
        if current is not None and recorded and recorded.get(path) not in (None, current) and path not in force:
            conflicts.append(path)
        upload.append(path)
    return local, live, upload, conflicts, manifest


def apply(local, live, upload):
    stamp = time.strftime("%Y%m%dT%H%M%SZ", time.gmtime())
    buffer = io.BytesIO()
    with tarfile.open(fileobj=buffer, mode="w") as tar:
        for path in upload:
            info = tar.gettarinfo(str(WEBSITE / path), arcname=path)
            info.uid = info.gid = 0; info.uname = info.gname = ""
            info.mode = 0o644
            with open(WEBSITE / path, "rb") as f:
                tar.addfile(info, f)
    replaced = [p for p in upload if p in live]
    manifest = {"deployed_at": int(time.time()), "files": local}
    quoted = " ".join(shlex.quote(p) for p in replaced) or "''"
    script = (f"B=~/Monzero-backups/deploy-{stamp}; mkdir -p \"$B\"; "
              f"printf '%s\\n' {quoted} | grep -v '^$' > \"$B/replaced.txt\" || true; "
              "if [ -s \"$B/replaced.txt\" ]; then tar -cf \"$B/replaced-files.tar\" -T \"$B/replaced.txt\"; fi; "
              f"printf '%s\\n' {' '.join(shlex.quote(p) for p in upload)} > \"$B/uploaded.txt\"; "
              "tar -xf - --no-same-owner; "
              "while IFS= read -r p; do sha256sum -- \"$p\"; done < \"$B/uploaded.txt\" > \"$B/verify.txt\"; "
              "echo \"$B\"; cat \"$B/verify.txt\"")
    out = remote(script, buffer.getvalue()).decode().splitlines()
    backup, verify = out[0], {line[66:]: line[:64] for line in out[1:] if len(line) > 66}
    bad = [p for p in upload if verify.get(p) != local[p]]
    if bad:
        sys.exit(f"verification failed for {bad}; restore with --rollback (backup {backup})")
    remote(f"mkdir -p ~/monzero-private; umask 077; cat > ~/monzero-private/deploy-manifest.json.tmp && "
           f"mv ~/monzero-private/deploy-manifest.json.tmp ~/monzero-private/deploy-manifest.json; "
           f"echo {shlex.quote(backup)} > ~/monzero-private/deploy-last-backup", json.dumps(manifest).encode())
    return backup


def rollback():
    out = remote("B=$(cat ~/monzero-private/deploy-last-backup); [ -d \"$B\" ]; "
                 "if [ -f \"$B/replaced-files.tar\" ]; then tar -xf \"$B/replaced-files.tar\" --no-same-owner; fi; "
                 "comm -23 <(sort \"$B/uploaded.txt\") <(sort \"$B/replaced.txt\" 2>/dev/null || true) > \"$B/added.txt\"; "
                 "echo \"$B\"; cat \"$B/added.txt\"; rm -f ~/monzero-private/deploy-manifest.json ~/monzero-private/deploy-last-backup").decode().splitlines()
    print(f"restored the files replaced by {out[0]}")
    if out[1:]:
        print("files that deploy added are still present (nothing is deleted automatically):")
        for path in out[1:]:
            print("  ", path)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--apply", action="store_true")
    parser.add_argument("--rollback", action="store_true")
    parser.add_argument("--force", action="append", default=[], metavar="PATH", help="overwrite this live-changed path")
    args = parser.parse_args()
    if args.rollback:
        return rollback()
    local, live, upload, conflicts, manifest = plan(set(args.force))
    new = [p for p in upload if p not in live]
    print(f"{len(local)} files tracked, {len(upload)} to upload ({len(new)} new, {len(upload) - len(new)} changed)"
          + ("" if manifest else "; no previous deploy manifest on the server"))
    for path in upload:
        print(("   new      " if path in new else "   changed  ") + path)
    if conflicts:
        print("\nThese live files were changed on the server since the last deploy (not by this script):")
        for path in conflicts:
            print("   ", path)
        sys.exit("refusing to overwrite them; check the live copies, then re-run with --force PATH for each")
    if not args.apply:
        print("\nplan only; run again with --apply to upload" if upload else "\nlive site is up to date")
        return
    if not upload:
        print("nothing to upload")
        return
    backup = apply(local, live, upload)
    print(f"uploaded and verified {len(upload)} files; backup of replaced files: {backup}")
    print("undo with: python3 utils/website/deploy.py --rollback")


if __name__ == "__main__":
    main()
