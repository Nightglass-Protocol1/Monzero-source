#!/usr/bin/env bash
set -euo pipefail

project_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
validator="$project_root/utils/release/validate-binary-archive.py"
work_dir=$(mktemp -d "${TMPDIR:-/tmp}/monzero-archive-safety.XXXXXX")
trap 'rm -rf -- "$work_dir"' EXIT

python3 - "$work_dir" <<'PY'
import io
import pathlib
import stat
import sys
import tarfile
import warnings
import zipfile

root = pathlib.Path(sys.argv[1])

with tarfile.open(root / "safe.tar.gz", "w:gz") as archive:
    directory = tarfile.TarInfo("package")
    directory.type = tarfile.DIRTYPE
    archive.addfile(directory)
    data = b"safe\n"
    regular = tarfile.TarInfo("package/README.md")
    regular.size = len(data)
    archive.addfile(regular, io.BytesIO(data))

with tarfile.open(root / "link.tar.gz", "w:gz") as archive:
    link = tarfile.TarInfo("package/link")
    link.type = tarfile.SYMTYPE
    link.linkname = "/tmp/monzero-archive-escape"
    archive.addfile(link)

with zipfile.ZipFile(root / "safe.zip", "w") as archive:
    archive.writestr("package/README.md", "safe\n")

with zipfile.ZipFile(root / "link.zip", "w") as archive:
    link = zipfile.ZipInfo("package/link")
    link.create_system = 3
    link.external_attr = (stat.S_IFLNK | 0o777) << 16
    archive.writestr(link, "/tmp/monzero-archive-escape")

with zipfile.ZipFile(root / "traversal.zip", "w") as archive:
    archive.writestr("package/../../escape", "unsafe\n")

with warnings.catch_warnings():
    warnings.simplefilter("ignore", UserWarning)
    with zipfile.ZipFile(root / "duplicate.zip", "w") as archive:
        archive.writestr("package/file", "first\n")
        archive.writestr("package/file", "second\n")

with zipfile.ZipFile(root / "too-many.zip", "w") as archive:
    for index in range(257):
        archive.writestr(f"package/{index}", b"")
PY

python3 "$validator" tar.gz "$work_dir/safe.tar.gz"
python3 "$validator" zip "$work_dir/safe.zip"

for case in "tar.gz:$work_dir/link.tar.gz" "zip:$work_dir/link.zip" \
  "zip:$work_dir/traversal.zip" "zip:$work_dir/duplicate.zip" \
  "zip:$work_dir/too-many.zip"; do
  format=${case%%:*}
  archive=${case#*:}
  if python3 "$validator" "$format" "$archive" >/dev/null 2>&1; then
    echo "Unsafe archive was accepted: $archive" >&2
    exit 1
  fi
done

# Smoke-test entry points must reject unsafe archives before extraction or
# execution, even when the caller supplies the correct archive checksum.
archive="$work_dir/link.tar.gz"
expected_hash=$(sha256sum "$archive" | awk '{print $1}')
for script in smoke-test-linux-package.sh smoke-test-linux-wallet.sh; do
  args=("$archive" "$expected_hash")
  if [[ $script == smoke-test-linux-wallet.sh ]]; then
    args+=(127.0.0.1:1)
  fi
  if bash "$project_root/utils/release/$script" "${args[@]}" >"$work_dir/$script.log" 2>&1; then
    echo "Smoke test accepted an unsafe archive: $script" >&2
    exit 1
  fi
  if ! grep -q 'Archive contains a link or special file:' "$work_dir/$script.log"; then
    echo "Smoke test did not reject the archive through the safety validator: $script" >&2
    cat "$work_dir/$script.log" >&2
    exit 1
  fi
done

echo "Release archive pre-extraction safety tests passed"
