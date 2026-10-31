#!/usr/bin/env python3
"""Real Qt job + pinned helper local lifecycle for all four light-wallet coins.

Every subprocess has networking disabled and only the disposable test root is
writable. No existing wallets, live funds, nodes or mining are involved.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import shutil
import tempfile
import uuid


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write(path, value):
    path.write_text(json.dumps(value, sort_keys=True, separators=(',', ':')))
    path.chmod(0o600)


def check(probe, helper, coin):
    with tempfile.TemporaryDirectory(prefix=f'monzero-qt-{coin}-') as temp:
        root = Path(temp).resolve()
        installed = root / 'native-light-helper'
        shutil.copyfile(helper, installed); installed.chmod(0o500)
        assert digest(installed) == digest(helper)
        password = 'disposable-qt-custody-' + uuid.uuid4().hex
        directories = {}
        for field in ['setupDirectory', 'vaultDirectory', 'addressBookDirectory',
                      'backupDirectory', 'spendDirectory', 'cacheDirectory']:
            folder = root / field; folder.mkdir(mode=0o700); directories[field] = str(folder)
        config = root / 'setup.json'
        setup = dict(directories, schema=f'monzero-private-{coin}-setup-v1',
                     network='regtest', walletId=str(uuid.uuid4()), accountIndex=0, maximumHeight=1000)
        write(config, setup)
        wrapper = root / 'launcher.json'
        launch = dict(schema=f'monzero-private-{coin}-light-launcher-v1',
                      helper=str(installed), helperDigest=digest(installed), configFile=str(config))
        write(wrapper, launch)
        env = dict(os.environ, MONZERO_TEST_LIGHT_COIN=coin, XDG_CONFIG_HOME=str(root / 'preferences'))

        def call(operation, extra=None, selected=None, reject=False, password_value=None):
            command = ['bwrap', '--die-with-parent', '--unshare-net', '--ro-bind', '/', '/',
                       '--bind', str(root), str(root), '--dev', '/dev', '--proc', '/proc',
                       '--', str(probe), str(wrapper), operation, '10']
            if extra is not None: command.append(json.dumps(extra))
            result = subprocess.run(command, input=(password if password_value is None else password_value).encode(), capture_output=True,
                                    env=dict(env, MONZERO_TEST_LIGHT_COIN=selected or coin), timeout=90)
            assert password.encode() not in result.stdout + result.stderr
            if reject:
                assert result.returncode != 0 and not result.stdout, (coin, operation, 'unexpected acceptance')
                return
            assert result.returncode == 0, (coin, operation, result.returncode, result.stderr.decode())
            result = json.loads(result.stdout)
            assert result['coin'] == coin and result['executable'] is False and result['busy'] is False
            assert result['state'] != 'unknown', (coin, operation, result)
            return result

        call('status', selected='unknown', reject=True)
        call('status', selected='btc' if coin != 'btc' else 'ltc', reject=True)
        if coin in ['bch', 'doge']:
            for network in ['main', 'test']:
                write(config, dict(setup, network=network)); call('status', reject=True)
            write(config, setup)
        created = call('wallet-create-open')
        assert created['state'] == 'ready'
        # Fresh process independently reads and opens the retained wallet.
        received = call('wallet-created-receive')
        assert received['state'] == 'wallet-address'
        address = received['wallet']['address']
        assert call('wallet-created-receive')['wallet']['address'] == address
        runtime = Path(directories['setupDirectory']) / 'litecoin-wallet.json'
        launch['configFile'] = str(runtime); write(wrapper, launch)
        backup = call('wallet-backup-verify', {'directory': directories['backupDirectory']})
        assert backup['state'] == 'wallet-backup-verified' and not backup['wallet']['activeSwapRecovery']
        call('wallet-recovery', {'approved': False}, reject=True)
        call('wallet-recovery', {'approvedConfigDigest': '0'*64}, reject=True)
        call('wallet-recovery', reject=True, password_value='wrong disposable password')
        revealed = call('wallet-recovery-timeout' if coin == 'ltc' else 'wallet-recovery')
        assert revealed['recoveryVisible'] is False
        assert 'seedHex' not in revealed and 'recoverySeed' not in revealed

        # Cross-chain account schema substitution must fail during configuration.
        # Raw recovery is helper-only until a separate desktop import review is wired.
        # All key material here belongs to disposable wallets and stays in memory.
        def private_helper(mode, config_path, request, reject=False):
            command = ['bwrap', '--die-with-parent', '--unshare-net', '--ro-bind', '/', '/',
                       '--bind', str(root), str(root), '--dev', '/dev', '--proc', '/proc',
                       '--', str(installed), '--' + mode + '-stdio-v1', str(config_path)]
            result = subprocess.run(command, input=json.dumps(request).encode(), capture_output=True, timeout=90)
            assert password.encode() not in result.stdout + result.stderr
            if reject:
                assert result.returncode != 0 and not result.stdout, (coin, mode, 'unexpected acceptance')
                assert request.get('seedHex', 'never-a-secret').encode() not in result.stderr
                return None
            assert result.returncode == 0, (coin, mode, 'helper failed')
            return json.loads(result.stdout)

        reveal_request = dict(schema=f'monzero-{coin}-light-wallet-recovery-request-v1',
                              requestId=str(uuid.uuid4()), approvedConfigDigest=digest(runtime),
                              revealApproved=True, password=password)
        private = private_helper('wallet-recovery', runtime, reveal_request)
        source_records = {str(p): digest(p) for p in root.rglob('*') if p.is_file() and p != installed}
        restored_directory = root / 'raw-restored-vault'; restored_directory.mkdir(mode=0o700)
        restored_config = json.loads(runtime.read_text())
        restored_config['wallet']['vault']['directory'] = str(restored_directory)
        restore_config = root / 'raw-restore.json'; write(restore_config, restored_config)
        restore_request = dict(schema=f'monzero-{coin}-light-wallet-key-restore-request-v1',
                               requestId=str(uuid.uuid4()), approvedConfigDigest=digest(restore_config),
                               restoreApproved=True, password=password, seedEncoding=private['seedEncoding'],
                               seedHex=private['seedHex'])
        for invalid in [dict(restore_request, restoreApproved=False),
                        dict(restore_request, approvedConfigDigest='0'*64),
                        dict(restore_request, seedEncoding='bip39'),
                        dict(restore_request, seedHex='08'*32)]:
            private_helper('wallet-key-restore', restore_config, invalid, reject=True)
            assert not (restored_directory / 'litecoin-seed.vault').exists()
        restored = private_helper('wallet-key-restore', restore_config, restore_request)
        assert restored['restored'] is True and restored['addressRecordsRestored'] is False
        assert restored['activeSwapRecovery'] is False and 'seedHex' not in restored
        ciphertext = (restored_directory / 'litecoin-seed.vault').read_bytes()
        private_helper('wallet-key-restore', restore_config, restore_request)
        assert (restored_directory / 'litecoin-seed.vault').read_bytes() == ciphertext
        checked = private_helper('wallet-recovery', restore_config,
                                 dict(reveal_request, approvedConfigDigest=digest(restore_config)))
        assert checked['seedHex'] == private['seedHex']
        private_helper('wallet-key-restore', restore_config,
                       dict(restore_request, password='wrong disposable password'), reject=True)
        assert (restored_directory / 'litecoin-seed.vault').read_bytes() == ciphertext
        for file in root.rglob('*'):
            if file.is_file() and file != installed:
                assert private['seedHex'].encode() not in file.read_bytes()
        assert all(digest(Path(p)) == checksum for p, checksum in source_records.items())
        qt_directory = root / 'qt-restored-vault'; qt_directory.mkdir(mode=0o700)
        restored_config['wallet']['vault']['directory'] = str(qt_directory)
        qt_config = root / 'qt-restore.json'; write(qt_config, restored_config)
        write(wrapper, dict(launch, configFile=str(qt_config)))
        credentials = dict(seed=private['seedHex'], password=password, confirmation=password)
        for extra, supplied in [({'approved': False}, credentials),
                                ({'approvedConfigDigest': '0'*64}, credentials),
                                ({}, dict(credentials, confirmation='mismatch')),
                                ({}, dict(credentials, seed='08'*32))]:
            call('wallet-key-restore', extra, reject=True, password_value=json.dumps(supplied))
            assert not (qt_directory / 'litecoin-seed.vault').exists()
        assert call('wallet-key-restore', password_value=json.dumps(credentials))['state'] == 'wallet-key-restored'
        assert call('wallet-recovery')['recoveryVisible'] is False
        assert call('wallet-current')['wallet']['address'] == address
        write(wrapper, launch)
        del credentials, private, checked, restore_request
        # Explicit format validation must not offer raw-seed controls on mnemonic configs.
        format_config = json.loads(runtime.read_text())
        format_file = root / 'format.json'
        format_config['wallet']['vault']['seedEncoding'] = 'unknown'
        write(format_file, format_config); write(wrapper, dict(launch, configFile=str(format_file)))
        call('status', reject=True)
        format_config['wallet']['vault']['seedEncoding'] = 'bip39-english-v1'
        write(format_file, format_config)
        format_status = call('status')
        assert format_status['recoveryEncoding'] == 'bip39-english-v1' and format_status['recoveryAvailable']
        call('wallet-recovery', reject=True)
        format_config['wallet']['vault']['seedEncoding'] = 'raw-bip32-seed-hex-v1'
        write(format_file, format_config)
        assert call('wallet-current')['wallet']['address'] == address
        write(wrapper, launch)
        changed = json.loads(runtime.read_text())
        changed['wallet']['account']['schema'] = 'monzero-eth-public-account-v1'
        altered = root / 'altered.json'; write(altered, changed)
        write(wrapper, dict(launch, configFile=str(altered)))
        call('status', reject=True)
        return {'coin': coin, 'status': 'passed', 'createdAndOpened': True,
                'restartRetainsAddress': True, 'localBackupVerified': True, 'privateRecoveryRevealAndHide': True, 'rawSeedVaultRestore': True, 'qtRawSeedRestore': True,
                'coinBindingRejectedMismatches': True, 'networkDisabled': True}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('probe', type=Path); parser.add_argument('helper', type=Path)
    parser.add_argument('report', type=Path); args = parser.parse_args()
    assert not args.report.exists()
    probe, helper = args.probe.resolve(strict=True), args.helper.resolve(strict=True)
    pins = {'probe': digest(probe), 'helper': digest(helper)}
    results = []
    for coin in ['ltc', 'btc', 'bch', 'doge']:
        results.append(check(probe, helper, coin)); print(f'PASS {coin}: Qt local lifecycle and chain binding', flush=True)
    assert pins == {'probe': digest(probe), 'helper': digest(helper)}
    args.report.write_text(json.dumps({'schema': 'monzero-qt-multicoin-custody-v1',
        'binaries': pins, 'testSha256': digest(Path(__file__)), 'results': results,
        'scope': 'Qt worker lifecycle only; no live funds, mainnet or full desktop/swap acceptance.'}, indent=2)+'\n')


if __name__ == '__main__':
    main()
