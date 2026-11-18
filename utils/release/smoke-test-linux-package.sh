#!/usr/bin/env bash
set -euo pipefail

[[ $# -eq 2 ]] || {
  echo "Usage: $0 <monzero-linux.tar.gz> <expected-sha256>" >&2
  exit 2
}

archive=$(realpath "$1")
expected_hash=$2
[[ -f $archive ]] || { echo "Archive not found: $archive" >&2; exit 2; }
[[ $expected_hash =~ ^[0-9a-f]{64}$ ]] || { echo "Expected SHA-256 must be 64 lowercase hexadecimal characters" >&2; exit 2; }

actual_hash=$(sha256sum "$archive" | awk '{print $1}')
[[ $actual_hash == "$expected_hash" ]] || {
  echo "Archive SHA-256 mismatch: expected $expected_hash, got $actual_hash" >&2
  exit 1
}

script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
python3 "$script_dir/validate-binary-archive.py" tar.gz "$archive"
command -v docker >/dev/null || { echo "This smoke test requires Docker" >&2; exit 2; }

# Ubuntu 24.04 is a clean compatibility floor, not a build environment. Pin the
# root filesystem by digest so a later tag update cannot silently change the
# evidence. The container has no network, a read-only root, and disposable
# tmpfs storage. A daemon that survives until timeout has completed startup.
image=${MONZERO_LINUX_SMOKE_IMAGE:-ubuntu@sha256:33ceb71981b602c1a7443a53469e4dba065f7503eab3078a2d7a57a2ab987517}
docker image inspect "$image" >/dev/null 2>&1 || {
  echo "Pinned image is not installed: $image" >&2
  echo "Fetch the reviewed image explicitly before running this test." >&2
  exit 2
}

docker run --rm --interactive --network none --read-only \
  --tmpfs /tmp:rw,nosuid,nodev,noexec,size=64m \
  --tmpfs /work:rw,nosuid,nodev,exec,size=256m \
  --volume "$archive:/input/package.tar.gz:ro" \
  "$image" bash -seu -- "$expected_hash" <<'CONTAINER'
expected_hash=$1
cd /work
printf '%s  %s\n' "$expected_hash" /input/package.tar.gz | sha256sum -c -
tar -xzf /input/package.tar.gz
mapfile -t roots < <(find /work -mindepth 1 -maxdepth 1 -type d)
[[ ${#roots[@]} -eq 1 ]] || { echo "Archive must contain exactly one root directory" >&2; exit 1; }
cd "${roots[0]}"
sha256sum -c SHA256SUMS
./monzerod --version
./monzero-wallet-cli --version
./monzero-wallet-rpc --version

set +e
timeout --signal=TERM --kill-after=5 8 ./monzerod \
  --regtest --offline --no-igd --non-interactive \
  --data-dir /work/data --log-file /work/daemon.log
daemon_status=$?
set -e
cat /work/daemon.log
[[ $daemon_status -eq 124 || $daemon_status -eq 143 ]] || {
  echo "Daemon did not remain running through the smoke interval (status $daemon_status)" >&2
  exit 1
}
CONTAINER

echo "Clean Linux package smoke test passed: $archive"
