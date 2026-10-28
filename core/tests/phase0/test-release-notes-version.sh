#!/usr/bin/env bash
set -euo pipefail

project_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
verifier="$project_root/utils/release/verify-release-notes.sh"
work_dir=$(mktemp -d "${TMPDIR:-/tmp}/monzero-release-notes-test.XXXXXX")
trap 'rm -rf -- "$work_dir"' EXIT

printf 'package=monzero-genesis-pre13-linux-x86_64\n' > "$work_dir/manifest"
printf '# Test notes\n\nRelease label: genesis-pre13\n' > "$work_dir/notes"
"$verifier" "$work_dir/manifest" -linux-x86_64 "$work_dir/notes" >/dev/null

printf 'package=monzero-genesis-pre13-development-dirty-linux-x86_64\n' > "$work_dir/manifest"
"$verifier" "$work_dir/manifest" -linux-x86_64 "$work_dir/notes" >/dev/null

printf 'package=monzero-genesis-pre13-source\n' > "$work_dir/manifest"
"$verifier" "$work_dir/manifest" -source "$work_dir/notes" >/dev/null

printf 'package=monzero-genesis-pre13-gui-source\n' > "$work_dir/manifest"
"$verifier" "$work_dir/manifest" -gui-source "$work_dir/notes" >/dev/null

printf '# Stale notes\n\nRelease label: genesis-pre12\n' > "$work_dir/notes"
printf 'package=monzero-genesis-pre13-linux-x86_64\n' > "$work_dir/manifest"
if "$verifier" "$work_dir/manifest" -linux-x86_64 "$work_dir/notes" >/dev/null 2>&1; then
  echo "Stale release notes were incorrectly accepted" >&2
  exit 1
fi
if "$verifier" "$work_dir/manifest" -windows-x64 "$work_dir/notes" >/dev/null 2>&1; then
  echo "Incorrect package platform suffix was accepted" >&2
  exit 1
fi

printf 'package=monzero-genesis-pre13-linux-x86_64\npackage=monzero-duplicate-linux-x86_64\n' > "$work_dir/manifest"
if "$verifier" "$work_dir/manifest" -linux-x86_64 "$work_dir/notes" >/dev/null 2>&1; then
  echo "Duplicate package metadata was incorrectly accepted" >&2
  exit 1
fi

echo "Release-notes version binding tests passed"
