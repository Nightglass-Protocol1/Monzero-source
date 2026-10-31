# Shared by the Windows signing scripts: rebuilds SHA256SUMS and writes the
# archive exactly as package-windows.sh and package-windows-gui-v1.sh do, so a
# signed package can be turned back into the byte-identical unsigned one.
# Usage (after sourcing): zip_windows_package <work-dir> <package-name> <archive> <epoch>
# Only the packaging image's Info-ZIP reproduces the archives byte for byte; outside
# it set MONZERO_ZIP=utils/release/container-zip.sh. The callers detect a mismatch.

zip_windows_package() {
  local work_dir=$1 package_name=$2 archive=$3 epoch=$4
  (
    cd "$work_dir/$package_name"
    find . -maxdepth 1 -type f ! -name SHA256SUMS -printf '%P\n' | LC_ALL=C sort |
      xargs -r sha256sum > SHA256SUMS
    sha256sum -c SHA256SUMS >/dev/null
    TZ=UTC find . -type f -exec touch -d "@$epoch" {} +
  )
  local zip_tool=${MONZERO_ZIP:-zip}
  [[ $zip_tool != */* ]] || zip_tool=$(realpath "$zip_tool")
  # Written inside work_dir first so a containerised zip only needs that one mount.
  rm -f -- "$work_dir/archive.zip" "$archive"
  (cd "$work_dir" && TZ=UTC find "$package_name" -type f -printf '%p\n' | LC_ALL=C sort | "$zip_tool" -X -9 -q archive.zip -@)
  mv -- "$work_dir/archive.zip" "$archive"
}

# Prints the single root directory of an extracted package.
windows_package_root() {
  local roots
  mapfile -t roots < <(find "$1" -mindepth 1 -maxdepth 1 -type d)
  [[ ${#roots[@]} -eq 1 ]] || { echo "Archive must contain exactly one root directory" >&2; return 1; }
  echo "${roots[0]}"
}

# The packaging scripts stamp every file with the source epoch; read it back.
windows_package_epoch() {
  TZ=UTC stat -c %Y "$1/SHA256SUMS"
}
