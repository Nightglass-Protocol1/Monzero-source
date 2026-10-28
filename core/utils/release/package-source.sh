#!/usr/bin/env bash
set -euo pipefail

[[ $# -eq 3 ]] || {
  echo "Usage: $0 <40-character-source-commit> <output-dir> <version>" >&2
  exit 2
}

source_root=$(git rev-parse --show-toplevel)
source_commit=$1
output_dir=$(realpath -m "$2")
version=$3
[[ $source_commit =~ ^[0-9a-f]{40}$ ]] || { echo "Invalid source commit" >&2; exit 2; }
git -C "$source_root" cat-file -e "$source_commit^{commit}" 2>/dev/null || {
  echo "Source commit is not available locally: $source_commit" >&2; exit 2;
}
[[ $version =~ ^[0-9A-Za-z][0-9A-Za-z._-]*$ ]] || { echo "Invalid version label" >&2; exit 2; }
git -C "$source_root" show "$source_commit:docs/RELEASE_NOTES.md" |
  grep -Fqx "Release label: $version" || {
    echo "RELEASE_NOTES.md at $source_commit does not match release label: $version" >&2; exit 1;
  }

source_epoch=$(git -C "$source_root" show -s --format=%ct "$source_commit")
package_name="monzero-${version}-source"
work_dir=$(mktemp -d "${TMPDIR:-/tmp}/monzero-source-package.XXXXXX")
trap 'rm -rf -- "$work_dir"' EXIT
package_dir="$work_dir/$package_name"
submodule_manifest="$work_dir/submodules.txt"
mkdir -p "$package_dir" "$output_dir"
: > "$submodule_manifest"

git -C "$source_root" archive --format=tar --prefix="$package_name/" "$source_commit" |
  tar -xf - -C "$work_dir"

archive_submodules() {
  local repository=$1 treeish=$2 destination_prefix=$3
  local path expected submodule_repository
  local -a paths=()
  mapfile -t paths < <(
    git -C "$repository" config --blob "$treeish:.gitmodules" \
      --get-regexp '^submodule\..*\.path$' 2>/dev/null | awk '{print $2}' | LC_ALL=C sort
  )
  for path in "${paths[@]}"; do
    [[ $path != /* && $path != ../* && $path != */../* && $path != *$'\n'* && $path != *$'\t'* ]] || {
      echo "Unsafe submodule path: $destination_prefix$path" >&2; exit 1;
    }
    expected=$(git -C "$repository" ls-tree "$treeish" -- "$path" |
      awk '$1 == "160000" && $2 == "commit" {print $3}')
    [[ $expected =~ ^[0-9a-f]{40}$ ]] || {
      echo "Missing gitlink for submodule: $destination_prefix$path" >&2; exit 1;
    }
    submodule_repository="$repository/$path"
    git -C "$submodule_repository" cat-file -e "$expected^{commit}" 2>/dev/null || {
      echo "Pinned submodule commit is unavailable: $destination_prefix$path $expected" >&2; exit 1;
    }
    printf '%s\t%s\n' "$destination_prefix$path" "$expected" >> "$submodule_manifest"
    git -C "$submodule_repository" archive --format=tar \
      --prefix="$package_name/$destination_prefix$path/" "$expected" |
      tar -xf - -C "$work_dir"
    archive_submodules "$submodule_repository" "$expected" "$destination_prefix$path/"
  done
}

archive_submodules "$source_root" "$source_commit" ''

{
  printf 'package=%s\n' "$package_name"
  printf 'source_commit=%s\n' "$source_commit"
  printf 'source_date_epoch=%s\n' "$source_epoch"
  printf 'submodules:\n'
  LC_ALL=C sort "$submodule_manifest"
} > "$package_dir/SOURCE-MANIFEST.txt"

(
  cd "$package_dir"
  find . -type f ! -name SHA256SUMS -print0 | LC_ALL=C sort -z |
    xargs -0 -r sha256sum > SHA256SUMS
  sha256sum -c SHA256SUMS
)

archive="$output_dir/$package_name.tar.gz"
tar --sort=name \
  --mtime="@$source_epoch" \
  --owner=0 --group=0 --numeric-owner \
  --pax-option=delete=atime,delete=ctime \
  -C "$work_dir" -cf - "$package_name" |
  gzip -n -9 > "$archive"
(cd "$output_dir" && sha256sum "$(basename "$archive")" > "$(basename "$archive").sha256")
echo "$archive"
echo "$archive.sha256"
