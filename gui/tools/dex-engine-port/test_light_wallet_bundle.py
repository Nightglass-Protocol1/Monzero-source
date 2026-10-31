#!/usr/bin/env python3
"""Verify helper packaging and isolated full-GUI bundle trust boundaries."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('gui', type=Path)
    parser.add_argument('helper', type=Path)
    parser.add_argument('report', type=Path)
    args = parser.parse_args()
    assert not args.report.exists()
    gui = args.gui.resolve(strict=True); helper = args.helper.resolve(strict=True)
    module = Path(__file__).resolve().parents[2] / 'cmake' / 'LightWalletHelper.cmake'
    pins = dict(gui=digest(gui), helper=digest(helper))
    with tempfile.TemporaryDirectory(prefix='monzero-bundle-test-') as temporary:
        root = Path(temporary)
        project = root / 'project'; project.mkdir()
        (project / 'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.12)\nproject(BundleTest NONE)\ninclude("'+str(module)+'")\n')
        for name, options, accepted in [
            ('valid', ['-DWITH_DEX_PROTOTYPE=ON', '-DMONZERO_LIGHT_HELPER='+str(helper), '-DMONZERO_LIGHT_HELPER_SHA256='+pins['helper']], True),
            ('unbundled', [], True),
            ('bad-hash', ['-DWITH_DEX_PROTOTYPE=ON', '-DMONZERO_LIGHT_HELPER='+str(helper), '-DMONZERO_LIGHT_HELPER_SHA256='+'0'*64], False),
            ('missing-hash', ['-DWITH_DEX_PROTOTYPE=ON', '-DMONZERO_LIGHT_HELPER='+str(helper)], False),
            ('disabled', ['-DWITH_DEX_PROTOTYPE=OFF', '-DMONZERO_LIGHT_HELPER='+str(helper), '-DMONZERO_LIGHT_HELPER_SHA256='+pins['helper']], False),
        ]:
            result = subprocess.run(['cmake', '-S', str(project), '-B', str(root/name)]+options, capture_output=True, timeout=60)
            assert (result.returncode == 0) == accepted, (name, result.stdout.decode(), result.stderr.decode())
        staged = root/'valid/bin/monzero-light/native-ltc-light'
        assert digest(staged) == pins['helper'] and staged.stat().st_nlink == 1
        result = subprocess.run(['cmake', '--install', str(root/'valid'), '--prefix', str(root/'installed'), '--component', 'LightWalletHelper'], capture_output=True, timeout=60)
        assert result.returncode == 0
        installed = root/'installed/bin/monzero-light/native-ltc-light'
        assert digest(installed) == pins['helper'] and installed.stat().st_mode & 0o777 == 0o555
        damaged = root/'damaged-helper'; shutil.copyfile(helper, damaged)
        with damaged.open('ab') as output: output.write(b'changed')
        damaged.chmod(0o500)
        base = ['bwrap', '--die-with-parent', '--unshare-all', '--new-session',
                '--ro-bind', '/usr', '/usr', '--ro-bind', '/lib', '/lib', '--ro-bind', '/lib64', '/lib64',
                '--proc', '/proc', '--dev', '/dev', '--tmpfs', '/tmp', '--dir', '/home/test', '--dir', '/etc',
                '--ro-bind', '/etc/passwd', '/etc/passwd', '--ro-bind', '/etc/fonts', '/etc/fonts',
                '--ro-bind', str(gui), '/app/gui', '--dir', '/app/monzero-light',
                '--clearenv', '--setenv', 'PATH', '/usr/bin', '--setenv', 'HOME', '/home/test',
                '--setenv', 'QT_QPA_PLATFORM', 'offscreen', '--setenv', 'QT_QUICK_BACKEND', 'software',
                '--setenv', 'XDG_CONFIG_HOME', '/tmp/config', '--setenv', 'XDG_DATA_HOME', '/tmp/data',
                '--setenv', 'XDG_CACHE_HOME', '/tmp/cache']
        results = []
        for name, binary, enabled, accepted in [('opt-out', None, False, True),
                ('valid', installed, True, True), ('missing', None, True, False), ('modified', damaged, True, False)]:
            command = base.copy()
            if binary: command += ['--ro-bind', str(binary), '/app/monzero-light/native-ltc-light']
            if enabled: command += ['--setenv', 'MONZERO_ENABLE_PRIVATE_DEX', 'I_UNDERSTAND_THIS_IS_A_LOCAL_PROTOTYPE']
            command += ['/app/gui', '--disable-check-updates', '--startup-smoke-test']
            result = subprocess.run(command, capture_output=True, timeout=60)
            assert (result.returncode == 0) == accepted, (name, result.returncode, result.stderr.decode()[-2500:])
            if not accepted: assert b'Bundled light-wallet helper verification failed' in result.stdout+result.stderr
            results.append(dict(case=name, passed=True))
        assert pins == dict(gui=digest(gui), helper=digest(helper))
        args.report.write_text(json.dumps(dict(schema='monzero-light-wallet-bundle-test-v1', binaries=pins,
            cases=results, cmakeTrustChecks=True, installMode='0555', networkDisabled=True,
            scope='Packaging and startup only; no user wallets, public chains or DEX settlement.'), indent=2)+'\n')
        print('PASS: packaging and isolated GUI bundle checks')


if __name__ == '__main__':
    main()
