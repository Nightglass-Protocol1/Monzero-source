#!/usr/bin/env bash
# Runs Info-ZIP from the packaging image so re-created Windows archives are byte
# identical to the packaged ones (other zip builds deflate differently). Use as
# MONZERO_ZIP=utils/release/container-zip.sh; reads the file list from stdin.
set -euo pipefail
image=${MONZERO_PACKAGE_IMAGE:-localhost/monzero-build-env:jammy-pkg}
exec podman run --rm -i --network=none --userns=keep-id -e TZ=UTC \
  -v "$PWD:$PWD" -w "$PWD" "$image" zip "$@"
