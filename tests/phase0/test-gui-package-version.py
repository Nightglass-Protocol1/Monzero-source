#!/usr/bin/env python3
"""Exercise version binding using a real, otherwise valid Windows GUI archive."""
import hashlib
import copy
from pathlib import Path
import subprocess
import sys
import tempfile
import zipfile


def main():
    if len(sys.argv) != 2:
        raise SystemExit(f"Usage: {sys.argv[0]} <windows-gui.zip>")
    archive = Path(sys.argv[1]).resolve()
    verifier = Path(__file__).resolve().parents[2] / "utils/release/verify-windows-gui-package.sh"

    def verify(path, expected=None):
        result = subprocess.run([str(verifier), str(path)], capture_output=True, text=True)
        if expected is None:
            assert result.returncode == 0, result.stdout + result.stderr
        else:
            assert result.returncode != 0, "Mismatched package was accepted"
            assert expected in result.stderr, result.stdout + result.stderr

    verify(archive)
    with zipfile.ZipFile(archive) as original:
        manifest_name = next(n for n in original.namelist() if n.endswith('/GUI_BUILD_MANIFEST.txt'))
        root = manifest_name.rsplit('/', 1)[0]
        manifest = original.read(manifest_name).decode()
        sums_name = root + '/SHA256SUMS'
        sums = original.read(sums_name).decode()
        cases = (
            ({'core_version': '1.0.0-000000000'},
             'GUI manifest core version does not match its source commit'),
            ({'core_source_commit': '0' * 40, 'core_version': '1.0.0-000000000'},
             'Embedded core version does not match GUI manifest'),
        )
        with tempfile.TemporaryDirectory(prefix='monzero-gui-version-') as directory:
            for index, (changes, error) in enumerate(cases):
                changed = ''.join(
                    f'{line.split("=", 1)[0]}={changes[line.split("=", 1)[0]]}\n'
                    if line.split('=', 1)[0] in changes else line + '\n'
                    for line in manifest.splitlines()
                ).encode()
                digest = hashlib.sha256(changed).hexdigest()
                checksums = ''.join(
                    f'{digest}  {line.split("  ", 1)[1]}\n'
                    if line.endswith('GUI_BUILD_MANIFEST.txt') else line + '\n'
                    for line in sums.splitlines()
                ).encode()
                target = Path(directory) / f'mismatch-{index}.zip'
                with zipfile.ZipFile(target, 'w') as modified:
                    for entry in original.infolist():
                        data = (changed if entry.filename == manifest_name else
                                checksums if entry.filename == sums_name else
                                original.read(entry.filename))
                        modified.writestr(copy.copy(entry), data)
                verify(target, error)
    print('GUI package version binding: valid archive accepted; both mismatches rejected')


if __name__ == '__main__':
    main()
