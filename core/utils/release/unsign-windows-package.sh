#!/usr/bin/env bash
# Rebuilds the unsigned Windows package from a code-signed one: removes each
# Authenticode signature, drops CODE-SIGNING.txt and re-creates the archive the
# way the packaging scripts do. The result must hash to the unsigned_package_sha256
# recorded in CODE-SIGNING.txt, which is what independent builders reproduce.
set -euo pipefail

[[ $# -eq 2 ]] || { echo "Usage: $0 <signed-windows.zip> <output.zip>" >&2; exit 2; }
signed=$(realpath "$1")
output=$(realpath -m "$2")
[[ -f "$signed" ]] || { echo "Archive not found: $signed" >&2; exit 2; }
[[ ! -e "$output" ]] || { echo "Refusing to overwrite: $output" >&2; exit 2; }
script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)
source "$script_dir/windows-package-zip.sh"
command -v osslsigncode >/dev/null || { echo "osslsigncode is required" >&2; exit 1; }

work_dir=$(mktemp -d "${TMPDIR:-/tmp}/monzero-windows-unsign.XXXXXX")
trap 'rm -rf -- "$work_dir"' EXIT
python3 "$script_dir/validate-binary-archive.py" zip "$signed"
mkdir "$work_dir/in"
TZ=UTC unzip -q "$signed" -d "$work_dir/in"
root=$(windows_package_root "$work_dir/in")
record="$root/CODE-SIGNING.txt"
[[ -f "$record" ]] || { echo "Package is not code-signed (no CODE-SIGNING.txt)" >&2; exit 1; }
epoch=$(windows_package_epoch "$root")

while read -r expected binary; do
  [[ $binary =~ ^[A-Za-z0-9._-]+\.exe$ && -f "$root/$binary" ]] || { echo "Invalid signing record entry: $binary" >&2; exit 1; }
  osslsigncode remove-signature -in "$root/$binary" -out "$root/$binary.unsigned" >/dev/null
  mv -- "$root/$binary.unsigned" "$root/$binary"
  chmod 0755 "$root/$binary"
  [[ $(sha256sum "$root/$binary" | awk '{print $1}') == "$expected" ]] || {
    echo "Removing the signature does not restore the unsigned executable: $binary" >&2; exit 1;
  }
done < <(sed -n 's/^unsigned_sha256=\([0-9a-f]\{64\}\) \(.*\)$/\1 \2/p' "$record")
for binary in "$root"/*.exe; do
  grep -Fq " $(basename "$binary")" <(sed -n 's/^unsigned_sha256=//p' "$record") || {
    echo "Executable missing from the signing record: $(basename "$binary")" >&2; exit 1;
  }
done

expected_package=$(sed -n 's/^unsigned_package_sha256=//p' "$record")
rm -- "$record"
zip_windows_package "$work_dir/in" "$(basename "$root")" "$output" "$epoch"
actual_package=$(sha256sum "$output" | awk '{print $1}')
[[ $actual_package == "$expected_package" ]] || {
  rm -f -- "$output"
  echo "Rebuilt unsigned package does not match the signing record ($actual_package)" >&2; exit 1;
}
echo "$output"
