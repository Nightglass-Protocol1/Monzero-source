#!/usr/bin/env bash
set -euo pipefail

[[ $# -eq 1 ]] || { echo "Usage: $0 <monzero-windows.zip>" >&2; exit 2; }
archive=$(realpath "$1")
[[ -f "$archive" ]] || { echo "Archive not found: $archive" >&2; exit 2; }
script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)
work_dir=$(mktemp -d "${TMPDIR:-/tmp}/monzero-windows-verify.XXXXXX")
trap 'rm -rf -- "$work_dir"' EXIT

python3 "$script_dir/validate-binary-archive.py" zip "$archive"
unzip -q "$archive" -d "$work_dir"
mapfile -t roots < <(find "$work_dir" -mindepth 1 -maxdepth 1 -type d)
[[ ${#roots[@]} -eq 1 ]] || { echo "Archive must contain exactly one root directory" >&2; exit 1; }
root=${roots[0]}

for required in monzerod.exe monzero-wallet-cli.exe monzero-wallet-rpc.exe BUILD-MANIFEST.txt SHA256SUMS README.md README-WINDOWS.txt UPGRADE.md RELEASE_NOTES.md RELEASE_STATUS.md RELEASE_CHECKLIST.md LICENSE start-node.bat start-wallet-cli.bat start-mining.bat stop-mining.bat; do
  [[ -f "$root/$required" ]] || { echo "Required package file is missing: $required" >&2; exit 1; }
done
grep -Fq -- '--rpc-bind-ip 127.0.0.1' "$root/start-node.bat" || {
  echo "Windows node launcher does not restrict HTTP RPC to loopback" >&2; exit 1;
}
grep -Fq -- '--zmq-rpc-bind-ip 127.0.0.1' "$root/start-node.bat" || {
  echo "Windows node launcher does not restrict ZMQ RPC to loopback" >&2; exit 1;
}
[[ $(sed -n 's/^package=//p' "$root/BUILD-MANIFEST.txt") == "$(basename "$root")" ]] || {
  echo "Build manifest package name does not match archive root" >&2; exit 1;
}
"$script_dir/verify-release-notes.sh" "$root/BUILD-MANIFEST.txt" -windows-x64 "$root/RELEASE_NOTES.md"
if find "$root" -type l -print -quit | grep -q .; then echo "Package contains symbolic links" >&2; exit 1; fi
(cd "$root" && sha256sum -c SHA256SUMS)
signature_status=$("$script_dir/verify-windows-signatures.sh" "$root")
echo "$signature_status"

if [[ -f "$root/monzero-miner-reporter.ps1" ]]; then
  grep -Fq 'mining_status' "$root/monzero-miner-reporter.ps1" || {
    echo "Windows miner reporter does not query daemon mining status" >&2; exit 1;
  }
  grep -Fq 'miner-stats-token.txt' "$root/monzero-miner-reporter.ps1" || {
    echo "Windows miner reporter does not use an external token file" >&2; exit 1;
  }
  grep -Fq 'blocks_found = Get-BlocksFound' "$root/monzero-miner-reporter.ps1" || {
    echo "Windows miner reporter does not collect its mined block count" >&2; exit 1;
  }
  grep -Fq 'monzero-miner-reporter.ps1' "$root/start-mining.bat" || {
    echo "Windows mining launcher does not start the optional website reporter" >&2; exit 1;
  }
fi

source_commit=$(sed -n 's/^source_commit=//p' "$root/BUILD-MANIFEST.txt")
[[ $source_commit =~ ^[0-9a-f]{40}$ ]] || { echo "Invalid source commit in manifest" >&2; exit 1; }
source_short=${source_commit:0:9}
for binary in monzerod.exe monzero-wallet-cli.exe monzero-wallet-rpc.exe; do
  report=$(file "$root/$binary")
  echo "$report"
  grep -Fq 'PE32+ executable' <<< "$report" || { echo "Not a Windows x64 PE binary: $binary" >&2; exit 1; }
  pe_headers=$(x86_64-w64-mingw32-objdump -p "$root/$binary")
  for mitigation in HIGH_ENTROPY_VA DYNAMIC_BASE NX_COMPAT; do
    grep -Fq "$mitigation" <<< "$pe_headers" || {
      echo "Required PE mitigation $mitigation is missing: $binary" >&2; exit 1;
    }
  done
  strings "$root/$binary" | grep -F "1.0.0-$source_short" >/dev/null || { echo "Version mismatch: $binary" >&2; exit 1; }
  while read -r library; do
    case ${library^^} in
      ADVAPI32.DLL|BCRYPT.DLL|CRYPT32.DLL|IPHLPAPI.DLL|KERNEL32.DLL|MSVCRT.DLL|MSWSOCK.DLL|SHELL32.DLL|USER32.DLL|WS2_32.DLL) ;;
      *) echo "Unexpected runtime dependency $library in $binary" >&2; exit 1 ;;
    esac
  done < <(x86_64-w64-mingw32-objdump -p "$root/$binary" | sed -n 's/^\s*DLL Name: //p')
done
grep -Fqx "binary_version=Monzero 'Genesis' (core v1.0.0-${source_short})" "$root/BUILD-MANIFEST.txt" || {
  echo "Binary version does not match manifest" >&2; exit 1;
}

if [[ ${RELEASE_STRICT:-0} == 1 ]]; then
  grep -qx 'source_tree_dirty=false' "$root/BUILD-MANIFEST.txt" || { echo "Strict verification rejects dirty source" >&2; exit 1; }
  grep -qx 'binaries_stripped=true' "$root/BUILD-MANIFEST.txt" || { echo "Strict verification requires stripped binaries" >&2; exit 1; }
  grep -qx 'binary_build_reproducibility=verified' "$root/BUILD-MANIFEST.txt" || {
    echo "Strict verification requires independently verified binary reproducibility" >&2; exit 1;
  }
  [[ $signature_status == *$'code_signed=true'* && $signature_status == *$'code_signature_timestamped=true'* ]] || {
    echo "Strict verification requires timestamped code signatures" >&2; exit 1;
  }
fi
echo "Package verification passed: $archive"
