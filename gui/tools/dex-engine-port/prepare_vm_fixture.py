#!/usr/bin/env python3
"""Prepare a standalone Qt DEX test source tree for a Debian lab VM.

This builds the existing dex_tests target only, not a release wallet. Source
membership comes from src/CMakeLists.txt so the VM uses the same production
DEX sources as the desktop regression target. No core/submodule build needed.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil


def prepare(output):
    root = Path(__file__).resolve().parents[2]
    source = (root / 'src/CMakeLists.txt').read_text()
    files = []
    for match in re.finditer(r'(?:add_executable|target_sources)\(dex_tests\s+(.*?)\)', source, re.S):
        files.extend(x for x in match[1].split() if x != 'PRIVATE')
    if not files or any(not x.startswith('dex/') or '..' in Path(x).parts for x in files):
        raise ValueError('Unexpected dex_tests source list; review the standalone target')
    output.mkdir(mode=0o700, parents=True, exist_ok=False)
    shutil.copytree(root / 'src/dex', output / 'src/dex')
    (output / 'CMakeLists.txt').write_text('''cmake_minimum_required(VERSION 3.16)
project(monzero_dex_vm LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_AUTOMOC ON)
find_package(Qt5 REQUIRED COMPONENTS Core Network Concurrent Test)
find_package(PkgConfig REQUIRED)
pkg_check_modules(DEX_SODIUM REQUIRED IMPORTED_TARGET libsodium)
add_executable(dex_tests ''' + ' '.join('src/' + x for x in files) + ''')
target_compile_definitions(dex_tests PRIVATE MONZERO_DEX_TESTING WITH_DEX_PROTOTYPE)
target_link_libraries(dex_tests Qt5::Core Qt5::Network Qt5::Concurrent Qt5::Test PkgConfig::DEX_SODIUM)
''')
    hashes = {str(p.relative_to(output)): hashlib.sha256(p.read_bytes()).hexdigest()
              for p in sorted(output.rglob('*')) if p.is_file()}
    (output / 'source-sha256.json').write_text(json.dumps(hashes, indent=2) + '\n')
    print(f'Prepared {len(hashes)} source files in {output}')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True)
    prepare(parser.parse_args().out)
