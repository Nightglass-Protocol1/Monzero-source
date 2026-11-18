#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd -P)
parser="$repo_root/utils/release/validsig-primary-fingerprint.awk"
work_dir=$(mktemp -d "${TMPDIR:-/tmp}/monzero-signing-subkey-test.XXXXXX")
trap 'find "$work_dir" -type f -delete; find "$work_dir" -depth -type d -empty -delete' EXIT
chmod 700 "$work_dir"
export GNUPGHOME="$work_dir/gnupg"
mkdir -m 700 "$GNUPGHOME"

gpg --batch --passphrase '' --quick-generate-key \
  'Monzero throwaway verifier test <test@invalid.example>' ed25519 cert 1d \
  >/dev/null 2>&1
primary=$(gpg --with-colons --list-keys |
  awk -F: '$1 == "fpr" { print $10; exit }')
gpg --batch --passphrase '' --quick-add-key "$primary" ed25519 sign 1d \
  >/dev/null 2>&1
signing=$(gpg --with-colons --list-secret-keys |
  awk -F: '$1 == "ssb" { subkey = 1 } subkey && $1 == "fpr" { print $10; exit }')

printf 'throwaway release metadata\n' > "$work_dir/payload"
gpg --batch --armor --detach-sign "$work_dir/payload"
status=$(gpg --status-fd 1 --verify "$work_dir/payload.asc" \
  "$work_dir/payload" 2>/dev/null)
parsed=$(awk -f "$parser" <<< "$status")

[[ $signing != "$primary" ]] || {
  echo 'Test key unexpectedly signed with its primary key' >&2
  exit 1
}
[[ $parsed == "$primary" ]] || {
  echo "Expected primary fingerprint $primary, got $parsed" >&2
  exit 1
}

if awk -f "$parser" </dev/null >/dev/null 2>&1; then
  echo 'Empty GnuPG status was incorrectly accepted' >&2
  exit 1
fi
if awk -f "$parser" > /dev/null 2>&1 <<EOF
[GNUPG:] VALIDSIG $signing 2026-08-23 0 0 4 0 22 10 00 $primary
[GNUPG:] VALIDSIG $signing 2026-08-23 0 0 4 0 22 10 00 $primary
EOF
then
  echo 'Multiple VALIDSIG records were incorrectly accepted' >&2
  exit 1
fi

echo "Signing-subkey verification passed for primary fingerprint $primary"
