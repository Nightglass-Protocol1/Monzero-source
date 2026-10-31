#!/usr/bin/env bash
# Authenticode-signs every executable in a finished, verified Windows package
# (CLI or GUI) and writes the signed archive, under the same name, to a separate
# output directory. Build manifests are left as built; CODE-SIGNING.txt records
# the signer and each executable's unsigned hash so the unsigned package can be
# rebuilt exactly (unsign-windows-package.sh) and compared with independent builds.
#
# Key, one of:
#   MONZERO_CODESIGN_PKCS11_MODULE  PKCS#11 library of the Certum card or SimplySign
#   MONZERO_CODESIGN_KEY            pkcs11: URI of the signing key
#   MONZERO_CODESIGN_CERTS          PEM file with the signing certificate and Certum
#                                   intermediates (or a pkcs11: certificate URI)
# or, for rehearsals with a throwaway certificate only:
#   MONZERO_CODESIGN_PKCS12         PKCS#12 file; its password is read from the terminal
# Optional:
#   MONZERO_CODESIGN_TIMESTAMP_URL  RFC 3161 server (default http://time.certum.pl)
#   MONZERO_CODESIGN_NO_TIMESTAMP=1 rehearsals only; strict verification rejects it
#   MONZERO_CODESIGN_CAFILE         trust anchors for the post-signing check
set -euo pipefail

[[ $# -eq 2 ]] || { echo "Usage: $0 <unsigned-windows.zip> <output-dir>" >&2; exit 2; }
input=$(realpath "$1")
output_dir=$(realpath -m "$2")
[[ -f "$input" ]] || { echo "Archive not found: $input" >&2; exit 2; }
archive="$output_dir/$(basename "$input")"
[[ $archive != "$input" ]] || { echo "Output directory must differ from the input's directory" >&2; exit 2; }
[[ ! -e "$archive" ]] || { echo "Refusing to overwrite: $archive" >&2; exit 2; }
script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)
source "$script_dir/windows-package-zip.sh"
command -v osslsigncode >/dev/null || { echo "osslsigncode is required" >&2; exit 1; }

key_args=()
if [[ -n ${MONZERO_CODESIGN_PKCS12:-} ]]; then
  [[ -z ${MONZERO_CODESIGN_KEY:-} ]] || { echo "Set either a PKCS#11 key or MONZERO_CODESIGN_PKCS12, not both" >&2; exit 2; }
  read -r -s -p "Password for $(basename "$MONZERO_CODESIGN_PKCS12"): " pkcs12_password; echo >&2
  key_args=(-pkcs12 "$(realpath "$MONZERO_CODESIGN_PKCS12")" -readpass /dev/stdin)
else
  : "${MONZERO_CODESIGN_PKCS11_MODULE:?set MONZERO_CODESIGN_PKCS11_MODULE (or MONZERO_CODESIGN_PKCS12 for a rehearsal)}"
  : "${MONZERO_CODESIGN_KEY:?set MONZERO_CODESIGN_KEY to the pkcs11: URI of the signing key}"
  : "${MONZERO_CODESIGN_CERTS:?set MONZERO_CODESIGN_CERTS to the certificate chain}"
  # The card's PIN is requested by the PKCS#11 module, never passed on the command line.
  key_args=(-pkcs11module "$MONZERO_CODESIGN_PKCS11_MODULE" -key "$MONZERO_CODESIGN_KEY" -certs "$MONZERO_CODESIGN_CERTS")
fi
timestamp_args=()
timestamped=true
timestamp_url=${MONZERO_CODESIGN_TIMESTAMP_URL:-http://time.certum.pl}
if [[ ${MONZERO_CODESIGN_NO_TIMESTAMP:-0} == 1 ]]; then
  timestamped=false
  timestamp_url=none
else
  timestamp_args=(-ts "$timestamp_url")
fi
verify_args=()
[[ -z ${MONZERO_CODESIGN_CAFILE:-} ]] || verify_args=(-CAfile "$(realpath "$MONZERO_CODESIGN_CAFILE")")

work_dir=$(mktemp -d "${TMPDIR:-/tmp}/monzero-windows-sign.XXXXXX")
trap 'rm -rf -- "$work_dir"' EXIT
python3 "$script_dir/validate-binary-archive.py" zip "$input"
TZ=UTC unzip -q "$input" -d "$work_dir"
root=$(windows_package_root "$work_dir")
package_name=$(basename "$root")
if [[ -f "$root/GUI_BUILD_MANIFEST.txt" ]]; then verifier=verify-windows-gui-package.sh; else verifier=verify-windows-package.sh; fi
[[ ! -f "$root/CODE-SIGNING.txt" ]] || { echo "Package is already code-signed" >&2; exit 1; }
"$script_dir/$verifier" "$input" >/dev/null
epoch=$(windows_package_epoch "$root")

record="$root/CODE-SIGNING.txt"
{
  echo "unsigned_package=$(basename "$input")"
  echo "unsigned_package_sha256=$(sha256sum "$input" | awk '{print $1}')"
  echo "timestamped=$timestamped"
  echo "timestamp_url=$timestamp_url"
} > "$work_dir/record-head"
: > "$work_dir/record-binaries"
signer=
for path in "$root"/*.exe; do
  binary=$(basename "$path")
  unsigned_hash=$(sha256sum "$path" | awk '{print $1}')
  echo "Signing $binary" >&2
  osslsigncode sign "${key_args[@]}" -h sha256 -n Monzero -i https://monzero.org/ \
    "${timestamp_args[@]}" -in "$path" -out "$path.signed" >/dev/null <<< "${pkcs12_password:-}"
  report=$(osslsigncode verify "${verify_args[@]}" -in "$path.signed" 2>&1) || {
    echo "$report" >&2; echo "Signed executable does not verify: $binary" >&2; exit 1;
  }
  this_signer=$(awk '/^Signer.s certificate:/{s=1} s&&/Subject:/{sub(/^[[:space:]]*Subject: /,"");print;exit}' <<< "$report")
  [[ -n $this_signer ]] || { echo "Could not read the signer of $binary" >&2; exit 1; }
  [[ -z $signer || $signer == "$this_signer" ]] || { echo "Executables were signed by different certificates" >&2; exit 1; }
  signer=$this_signer
  if [[ $timestamped == true ]] && grep -q 'Timestamp is not available' <<< "$report"; then
    echo "No timestamp was added to $binary" >&2; exit 1
  fi
  osslsigncode remove-signature -in "$path.signed" -out "$work_dir/roundtrip.exe" >/dev/null
  [[ $(sha256sum "$work_dir/roundtrip.exe" | awk '{print $1}') == "$unsigned_hash" ]] || {
    echo "Removing the signature would not restore $binary exactly" >&2; exit 1;
  }
  rm -- "$work_dir/roundtrip.exe"
  mv -- "$path.signed" "$path"
  chmod 0755 "$path"
  echo "unsigned_sha256=$unsigned_hash $binary" >> "$work_dir/record-binaries"
done
{
  cat "$work_dir/record-head"
  echo "signer_subject=$signer"
  cat "$work_dir/record-binaries"
} > "$record"
chmod 0644 "$record"

mkdir -p "$output_dir"
zip_windows_package "$work_dir" "$package_name" "$archive" "$epoch"
(cd "$output_dir" && sha256sum "$(basename "$archive")" > "$(basename "$archive").sha256")

# Prove the result: it verifies, and stripping it gives back the input byte for byte.
"$script_dir/$verifier" "$archive" >/dev/null
"$script_dir/unsign-windows-package.sh" "$archive" "$work_dir/unsigned-check.zip" >/dev/null
echo "Signed by: $signer" >&2
echo "$archive"
echo "$archive.sha256"
