#!/usr/bin/env bash
set -euo pipefail

[[ $# -eq 6 ]] || {
  echo "Usage: $0 <gui-repository> <gui-commit> <core-repository> <core-commit> <output-dir> <version>" >&2
  exit 2
}

gui_repository=$(realpath "$1")
gui_commit=$2
core_repository=$(realpath "$3")
core_commit=$4
output_dir=$(realpath -m "$5")
version=$6

for commit in "$gui_commit" "$core_commit"; do
  [[ $commit =~ ^[0-9a-f]{40}$ ]] || { echo "Invalid source commit: $commit" >&2; exit 2; }
done
[[ $version =~ ^[0-9A-Za-z][0-9A-Za-z._-]*$ ]] || { echo "Invalid version label" >&2; exit 2; }
git -C "$gui_repository" cat-file -e "$gui_commit^{commit}" 2>/dev/null || {
  echo "GUI commit is unavailable: $gui_commit" >&2; exit 2;
}
git -C "$core_repository" cat-file -e "$core_commit^{commit}" 2>/dev/null || {
  echo "Core commit is unavailable: $core_commit" >&2; exit 2;
}
git -C "$core_repository" show "$core_commit:docs/RELEASE_NOTES.md" |
  grep -Fqx "Release label: $version" || {
    echo "Core RELEASE_NOTES.md at $core_commit does not match release label: $version" >&2; exit 1;
  }

gui_epoch=$(git -C "$gui_repository" show -s --format=%ct "$gui_commit")
core_epoch=$(git -C "$core_repository" show -s --format=%ct "$core_commit")
source_epoch=$((gui_epoch > core_epoch ? gui_epoch : core_epoch))
package_name="monzero-${version}-gui-source"
work_dir=$(mktemp -d "${TMPDIR:-/tmp}/monzero-gui-source.XXXXXX")
trap 'rm -rf -- "$work_dir"' EXIT
package_dir="$work_dir/$package_name"
submodule_manifest="$work_dir/submodules.txt"
mkdir -p "$package_dir" "$output_dir"
: > "$submodule_manifest"

git -C "$gui_repository" archive --format=tar --prefix="$package_name/" "$gui_commit" |
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

# Archive non-core GUI submodules at their committed gitlinks.
while IFS= read -r path; do
  [[ $path == monero ]] && continue
  expected=$(git -C "$gui_repository" ls-tree "$gui_commit" -- "$path" |
    awk '$1 == "160000" && $2 == "commit" {print $3}')
  [[ $expected =~ ^[0-9a-f]{40}$ ]] || { echo "Missing GUI submodule gitlink: $path" >&2; exit 1; }
  submodule_repository="$gui_repository/$path"
  git -C "$submodule_repository" cat-file -e "$expected^{commit}" 2>/dev/null || {
    echo "Pinned GUI submodule commit is unavailable: $path $expected" >&2; exit 1;
  }
  printf '%s\t%s\n' "$path" "$expected" >> "$submodule_manifest"
  git -C "$submodule_repository" archive --format=tar --prefix="$package_name/$path/" "$expected" |
    tar -xf - -C "$work_dir"
  archive_submodules "$submodule_repository" "$expected" "$path/"
done < <(
  git -C "$gui_repository" config --blob "$gui_commit:.gitmodules" \
    --get-regexp '^submodule\..*\.path$' 2>/dev/null | awk '{print $2}' | LC_ALL=C sort
)

# The GUI commit still points at pre11 core; release source must match the packaged binaries.
printf 'monero\t%s\n' "$core_commit" >> "$submodule_manifest"
git -C "$core_repository" archive --format=tar --prefix="$package_name/monero/" "$core_commit" |
  tar -xf - -C "$work_dir"
archive_submodules "$core_repository" "$core_commit" "monero/"

{
  printf 'package=%s\n' "$package_name"
  printf 'gui_source_commit=%s\n' "$gui_commit"
  printf 'core_source_commit=%s\n' "$core_commit"
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
tar --sort=name --mtime="@$source_epoch" --owner=0 --group=0 --numeric-owner \
  --pax-option=delete=atime,delete=ctime -C "$work_dir" -cf - "$package_name" |
  gzip -n -9 > "$archive"
(cd "$output_dir" && sha256sum "$(basename "$archive")" > "$(basename "$archive").sha256")
echo "$archive"
echo "$archive.sha256"
