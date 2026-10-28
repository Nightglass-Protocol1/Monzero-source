#!/usr/bin/env bash
set -euo pipefail

[[ $# -eq 1 ]] || { echo "Usage: $0 <monzero-linux.tar.gz>" >&2; exit 2; }
archive=$(realpath "$1")
[[ -f "$archive" ]] || { echo "Archive not found: $archive" >&2; exit 2; }
script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)

work_dir=$(mktemp -d "${TMPDIR:-/tmp}/monzero-verify.XXXXXX")
trap 'rm -rf -- "$work_dir"' EXIT

# Reject links and special files before extraction. A post-extraction symlink
# check is too late if a later entry follows a link outside the work directory.
python3 "$script_dir/validate-binary-archive.py" tar.gz "$archive"
tar --no-same-owner --no-same-permissions -xzf "$archive" -C "$work_dir"

mapfile -t roots < <(find "$work_dir" -mindepth 1 -maxdepth 1 -type d)
[[ ${#roots[@]} -eq 1 ]] || { echo "Archive must contain exactly one root directory" >&2; exit 1; }
root=${roots[0]}

[[ -x "$root/monzerod" && -x "$root/monzero-wallet-cli" ]] || {
  echo "Required Monzero executables are missing" >&2
  exit 1
}
[[ -f "$root/BUILD-MANIFEST.txt" && -f "$root/SHA256SUMS" ]] || {
  echo "Build or checksum manifest is missing" >&2
  exit 1
}
[[ -x "$root/start-monzerod.sh" ]] || {
  echo "Required node launcher is missing or not executable" >&2
  exit 1
}
grep -Fq -- '--rpc-bind-ip 127.0.0.1' "$root/start-monzerod.sh" || {
  echo "Linux node launcher does not restrict HTTP RPC to loopback" >&2; exit 1;
}
grep -Fq -- '--zmq-rpc-bind-ip 127.0.0.1' "$root/start-monzerod.sh" || {
  echo "Linux node launcher does not restrict ZMQ RPC to loopback" >&2; exit 1;
}
for required_document in README.md UPGRADE.md RELEASE_NOTES.md RELEASE_STATUS.md RELEASE_CHECKLIST.md LICENSE; do
  [[ -f "$root/$required_document" ]] || {
    echo "Required release document is missing: $required_document" >&2
    exit 1
  }
done

manifest_package=$(sed -n 's/^package=//p' "$root/BUILD-MANIFEST.txt")
[[ $manifest_package == "$(basename "$root")" ]] || {
  echo "Build manifest package name does not match archive root" >&2
  exit 1
}
"$script_dir/verify-release-notes.sh" "$root/BUILD-MANIFEST.txt" -linux-x86_64 "$root/RELEASE_NOTES.md"

if find "$root" -type l -print -quit | grep -q .; then
  echo "Package contains symbolic links" >&2
  exit 1
fi

(
  cd "$root"
  sha256sum -c SHA256SUMS
)

release_binaries=("$root/monzerod" "$root/monzero-wallet-cli")
[[ ! -x "$root/monzero-wallet-rpc" ]] || release_binaries+=("$root/monzero-wallet-rpc")
binary_report=$(file "${release_binaries[@]}")
echo "$binary_report"
daemon_version=$("$root/monzerod" --version | head -n 1)
wallet_version=$("$root/monzero-wallet-cli" --version | head -n 1)
[[ -n $daemon_version && $daemon_version == "$wallet_version" ]] || {
  echo "Packaged executables report inconsistent versions" >&2
  exit 1
}
if [[ -x "$root/monzero-wallet-rpc" ]]; then
  rpc_version=$("$root/monzero-wallet-rpc" --version | head -n 1)
  [[ $rpc_version == "$daemon_version" ]] || {
    echo "Packaged wallet RPC reports an inconsistent version" >&2
    exit 1
  }
fi
grep -Fqx "binary_version=$daemon_version" "$root/BUILD-MANIFEST.txt" || {
  echo "Packaged executable version does not match the build manifest" >&2
  exit 1
}
if [[ ${RELEASE_STRICT:-0} == 1 ]]; then
  if grep -Eq 'not stripped|with debug_info' <<< "$binary_report"; then
    echo "Strict release verification rejects unstripped or debug binaries" >&2
    exit 1
  fi
  grep -qx 'binaries_stripped=true' "$root/BUILD-MANIFEST.txt" || {
    echo "Strict release verification requires a stripped-binary manifest" >&2
    exit 1
  }
  command -v readelf >/dev/null || {
    echo "Strict release verification requires readelf" >&2
    exit 1
  }
  max_glibc=${MONZERO_MAX_GLIBC:-2.35}
  [[ $max_glibc =~ ^[0-9]+\.[0-9]+$ ]] || {
    echo "MONZERO_MAX_GLIBC must be a numeric major.minor version" >&2
    exit 2
  }
  for binary in "${release_binaries[@]}"; do
    mapfile -t needed < <(readelf -d "$binary" 2>/dev/null |
      sed -n 's/.*Shared library: \[\([^]]*\)\].*/\1/p')
    for library in "${needed[@]}"; do
      case "$library" in
        libc.so.6|libm.so.6|libpthread.so.0|libdl.so.2|librt.so.1|libresolv.so.2|ld-linux-x86-64.so.2) ;;
        *)
          echo "Strict release verification rejects runtime dependency $library in $(basename "$binary")" >&2
          exit 1
          ;;
      esac
    done
    required_glibc=$(readelf --version-info "$binary" 2>/dev/null |
      grep -oE 'GLIBC_[0-9]+\.[0-9]+' | sed 's/^GLIBC_//' | sort -Vu | tail -n 1)
    if [[ -n $required_glibc && $(printf '%s\n%s\n' "$max_glibc" "$required_glibc" | sort -V | tail -n 1) != "$max_glibc" ]]; then
      echo "Strict release verification rejects GLIBC_$required_glibc requirement in $(basename "$binary"); maximum supported is GLIBC_$max_glibc" >&2
      exit 1
    fi
  done
  grep -qx 'source_tree_dirty=false' "$root/BUILD-MANIFEST.txt" || {
    echo "Strict release verification rejects a dirty source manifest" >&2
    exit 1
  }
  grep -qx 'binary_build_reproducibility=verified' "$root/BUILD-MANIFEST.txt" || {
    echo "Strict release verification requires independently verified binary reproducibility" >&2
    exit 1
  }
fi
echo "$daemon_version"
echo "$wallet_version"
echo "Package verification passed: $archive"
