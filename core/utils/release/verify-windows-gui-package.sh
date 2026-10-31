#!/usr/bin/env bash
set -euo pipefail

[[ $# -eq 1 ]] || { echo "Usage: $0 <monzero-windows-gui.zip>" >&2; exit 2; }
archive=$(realpath "$1")
[[ -f "$archive" ]] || { echo "Archive not found: $archive" >&2; exit 2; }
script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)
work_dir=$(mktemp -d "${TMPDIR:-/tmp}/monzero-windows-gui-verify.XXXXXX")
trap 'rm -rf -- "$work_dir"' EXIT

python3 "$script_dir/validate-binary-archive.py" zip "$archive"
unzip -q "$archive" -d "$work_dir"
mapfile -t roots < <(find "$work_dir" -mindepth 1 -maxdepth 1 -type d)
[[ ${#roots[@]} -eq 1 ]] || { echo "Archive must contain exactly one root directory" >&2; exit 1; }
root=${roots[0]}

for required in monzero-wallet-gui.exe monzerod.exe monzero-wallet-cli.exe monzero-wallet-rpc.exe GUI_BUILD_MANIFEST.txt SHA256SUMS README-GUI.txt RELEASE_NOTES.md LICENSE start-node.bat start-wallet-gui.bat; do
  [[ -f "$root/$required" ]] || { echo "Required GUI package file is missing: $required" >&2; exit 1; }
done
grep -Fq -- '--rpc-bind-ip 127.0.0.1' "$root/start-node.bat" || {
  echo "Windows GUI node launcher does not restrict HTTP RPC to loopback" >&2; exit 1;
}
grep -Fq -- '--zmq-rpc-bind-ip 127.0.0.1' "$root/start-node.bat" || {
  echo "Windows GUI node launcher does not restrict ZMQ RPC to loopback" >&2; exit 1;
}
[[ $(sed -n 's/^package=//p' "$root/GUI_BUILD_MANIFEST.txt") == "$(basename "$root")" ]] || {
  echo "GUI build manifest package name does not match archive root" >&2; exit 1;
}
"$script_dir/verify-release-notes.sh" "$root/GUI_BUILD_MANIFEST.txt" -windows-gui-x64 "$root/RELEASE_NOTES.md"
(cd "$root" && sha256sum -c SHA256SUMS)
signature_status=$("$script_dir/verify-windows-signatures.sh" "$root")
echo "$signature_status"

gui_commit=$(sed -n 's/^gui_source_commit=//p' "$root/GUI_BUILD_MANIFEST.txt")
core_commit=$(sed -n 's/^core_source_commit=//p' "$root/GUI_BUILD_MANIFEST.txt")
[[ $gui_commit =~ ^[0-9a-f]{40}$ ]] || { echo "Invalid GUI source commit" >&2; exit 1; }
[[ $core_commit =~ ^[0-9a-f]{40}$ ]] || { echo "Invalid core source commit" >&2; exit 1; }
expected_core_version="1.0.0-${core_commit:0:9}"
[[ $(sed -n 's/^core_version=//p' "$root/GUI_BUILD_MANIFEST.txt") == "$expected_core_version" ]] || {
  echo "GUI manifest core version does not match its source commit" >&2; exit 1;
}
expected_gui_hash=$(sed -n 's/^gui_sha256=//p' "$root/GUI_BUILD_MANIFEST.txt")
# The manifest describes the build; a signed package records that same unsigned hash.
if [[ -f "$root/CODE-SIGNING.txt" ]]; then
  actual_gui_hash=$(sed -n 's/^unsigned_sha256=\([0-9a-f]\{64\}\) monzero-wallet-gui\.exe$/\1/p' "$root/CODE-SIGNING.txt")
else
  actual_gui_hash=$(sha256sum "$root/monzero-wallet-gui.exe" | awk '{print $1}')
fi
[[ $expected_gui_hash == "$actual_gui_hash" ]] || { echo "GUI executable hash does not match manifest" >&2; exit 1; }

for binary in monzero-wallet-gui.exe monzerod.exe monzero-wallet-cli.exe monzero-wallet-rpc.exe; do
  # Consume all strings output: grep -q can cause SIGPIPE under pipefail.
  strings "$root/$binary" | grep -Fx "$expected_core_version" >/dev/null || {
    echo "Embedded core version does not match GUI manifest: $binary" >&2; exit 1;
  }
  report=$(file "$root/$binary")
  echo "$report"
  grep -Fq 'PE32+ executable' <<< "$report" || { echo "Not a Windows x64 PE binary: $binary" >&2; exit 1; }
  pe_headers=$(x86_64-w64-mingw32-objdump -p "$root/$binary")
  for mitigation in HIGH_ENTROPY_VA DYNAMIC_BASE NX_COMPAT; do
    grep -Fq "$mitigation" <<< "$pe_headers" || {
      echo "Required PE mitigation $mitigation is missing: $binary" >&2; exit 1;
    }
  done
done
x86_64-w64-mingw32-objdump -p "$root/monzero-wallet-gui.exe" |
  grep -E '^Subsystem[[:space:]].*\(Windows GUI\)$' >/dev/null || { echo "GUI executable does not use the Windows GUI subsystem" >&2; exit 1; }

if [[ ${RELEASE_STRICT:-0} == 1 ]]; then
  grep -qx 'static=true' "$root/GUI_BUILD_MANIFEST.txt" || { echo "Strict verification requires a static GUI build" >&2; exit 1; }
  grep -qx 'dev_mode=false' "$root/GUI_BUILD_MANIFEST.txt" || { echo "Strict verification rejects GUI development mode" >&2; exit 1; }
  grep -qx 'native_windows_tested=true' "$root/GUI_BUILD_MANIFEST.txt" || { echo "Strict verification requires native Windows GUI testing" >&2; exit 1; }
  grep -qx 'independently_reproduced=true' "$root/GUI_BUILD_MANIFEST.txt" || { echo "Strict verification requires independent GUI reproduction" >&2; exit 1; }
  grep -qx 'security_audited=true' "$root/GUI_BUILD_MANIFEST.txt" || { echo "Strict verification requires completed GUI security review" >&2; exit 1; }
  grep -qx 'release_tests_run=true' "$root/GUI_BUILD_MANIFEST.txt" || { echo "Strict verification requires GUI release tests" >&2; exit 1; }
  [[ $signature_status == *$'code_signed=true'* && $signature_status == *$'code_signature_timestamped=true'* ]] || {
    echo "Strict verification requires timestamped code signatures" >&2; exit 1;
  }
fi

echo "GUI package verification passed: $archive"
