#!/usr/bin/env bash
set -euo pipefail

[[ $# -eq 3 ]] || {
  echo "Usage: $0 <build-manifest> <package-suffix> <release-notes>" >&2
  exit 2
}

manifest=$1
package_suffix=$2
release_notes=$3

[[ -f $manifest ]] || { echo "Build manifest is missing: $manifest" >&2; exit 1; }
[[ -f $release_notes ]] || { echo "Release notes are missing: $release_notes" >&2; exit 1; }
[[ $package_suffix =~ ^-[a-z0-9_-]+$ ]] || { echo "Invalid package suffix" >&2; exit 2; }

mapfile -t packages < <(sed -n 's/^package=//p' "$manifest")
[[ ${#packages[@]} -eq 1 ]] || {
  echo "Build manifest must contain exactly one package field" >&2
  exit 1
}
package=${packages[0]}
[[ $package == monzero-*"$package_suffix" ]] || {
  echo "Build manifest package does not match expected suffix: $package_suffix" >&2
  exit 1
}

artifact_label=${package#monzero-}
artifact_label=${artifact_label%"$package_suffix"}
release_label=${artifact_label%-development-dirty}
[[ -n $release_label && $release_label =~ ^[0-9A-Za-z][0-9A-Za-z._-]*$ ]] || {
  echo "Invalid release label derived from package: $package" >&2
  exit 1
}
grep -Fqx "Release label: $release_label" "$release_notes" || {
  echo "Release notes do not match packaged release label: $release_label" >&2
  exit 1
}

echo "Release notes match packaged release label: $release_label"
