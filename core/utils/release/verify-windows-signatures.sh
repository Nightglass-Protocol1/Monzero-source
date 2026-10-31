#!/usr/bin/env bash
# Checks the Authenticode signatures of an extracted Windows package. Without a
# CODE-SIGNING.txt the package is unsigned and every executable must be too.
# With one, every executable must verify, carry the recorded signer (and the
# MONZERO_EXPECTED_SIGNER subject when set), carry a timestamp when the record
# says so, and return to its recorded unsigned hash when the signature is removed.
# MONZERO_CODESIGN_CAFILE replaces the system trust anchors (rehearsal certificates).
set -euo pipefail

[[ $# -eq 1 && -d $1 ]] || { echo "Usage: $0 <extracted-package-root>" >&2; exit 2; }
root=$1
record="$root/CODE-SIGNING.txt"
command -v osslsigncode >/dev/null || { echo "osslsigncode is required to check code signatures" >&2; exit 1; }
work_dir=$(mktemp -d "${TMPDIR:-/tmp}/monzero-signature-verify.XXXXXX")
trap 'rm -rf -- "$work_dir"' EXIT

if [[ ! -f $record ]]; then
  for path in "$root"/*.exe; do
    osslsigncode extract-signature -in "$path" -out "$work_dir/signature" >/dev/null 2>&1 && {
      echo "Executable is signed but the package has no CODE-SIGNING.txt: $(basename "$path")" >&2; exit 1;
    }
  done
  echo "code_signed=false"
  exit 0
fi

field() { sed -n "s/^$1=//p" "$record"; }
[[ $(grep -c '^unsigned_package=' "$record") -eq 1 && $(field unsigned_package) == "$(basename "$root").zip" ]] || {
  echo "Signing record names a different package" >&2; exit 1;
}
[[ $(field unsigned_package_sha256) =~ ^[0-9a-f]{64}$ ]] || { echo "Invalid unsigned package hash in signing record" >&2; exit 1; }
timestamped=$(field timestamped)
[[ $timestamped == true || $timestamped == false ]] || { echo "Invalid timestamped field in signing record" >&2; exit 1; }
signer=$(field signer_subject)
[[ -n $signer && $(grep -c '^signer_subject=' "$record") -eq 1 ]] || { echo "Signing record has no signer" >&2; exit 1; }
if [[ -n ${MONZERO_EXPECTED_SIGNER:-} && $signer != "$MONZERO_EXPECTED_SIGNER" ]]; then
  echo "Package signer is not the expected certificate: $signer" >&2; exit 1
fi
verify_args=()
[[ -z ${MONZERO_CODESIGN_CAFILE:-} ]] || verify_args=(-CAfile "$MONZERO_CODESIGN_CAFILE")

declare -A expected=()
while read -r hash binary; do
  [[ $hash =~ ^[0-9a-f]{64}$ && $binary =~ ^[A-Za-z0-9._-]+\.exe$ && -z ${expected[$binary]:-} ]] || {
    echo "Invalid signing record entry: $binary" >&2; exit 1;
  }
  expected[$binary]=$hash
done < <(field unsigned_sha256)
count=0
for path in "$root"/*.exe; do
  binary=$(basename "$path")
  [[ -n ${expected[$binary]:-} ]] || { echo "Executable missing from the signing record: $binary" >&2; exit 1; }
  count=$((count + 1))
  report=$(osslsigncode verify "${verify_args[@]}" -in "$path" 2>&1) || {
    echo "$report" >&2; echo "Code signature does not verify: $binary" >&2; exit 1;
  }
  this_signer=$(awk '/^Signer.s certificate:/{s=1} s&&/Subject:/{sub(/^[[:space:]]*Subject: /,"");print;exit}' <<< "$report")
  [[ $this_signer == "$signer" ]] || { echo "Executable signer does not match the signing record: $binary" >&2; exit 1; }
  if [[ $timestamped == true ]] && grep -q 'Timestamp is not available' <<< "$report"; then
    echo "Signature has no timestamp: $binary" >&2; exit 1
  fi
  osslsigncode remove-signature -in "$path" -out "$work_dir/unsigned.exe" >/dev/null
  [[ $(sha256sum "$work_dir/unsigned.exe" | awk '{print $1}') == "${expected[$binary]}" ]] || {
    echo "Signed executable does not match its recorded unsigned build: $binary" >&2; exit 1;
  }
  rm -- "$work_dir/unsigned.exe"
done
[[ $count -eq ${#expected[@]} ]] || { echo "Signing record lists executables that are not in the package" >&2; exit 1; }
echo "code_signed=true"
echo "code_signer=$signer"
echo "code_signature_timestamped=$timestamped"
