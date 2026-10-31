#!/usr/bin/env python3
"""Offline real Qt/helper mnemonic creation, backup and private recovery checks."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import uuid
from test_light_wallet_qt_custody import digest, write


def check(probe, helper, coin):
    with tempfile.TemporaryDirectory(prefix='monzero-mnemonic-qt-') as temporary:
        root = Path(temporary).resolve()
        installed = root / 'helper'; shutil.copyfile(helper, installed); installed.chmod(0o500)
        settings = dict(schema=f'monzero-private-{coin}-setup-v1', network='regtest',
                        seedEncoding='bip39-english-v1', walletId=str(uuid.uuid4()), accountIndex=0, maximumHeight=1000)
        for key in ['setupDirectory', 'vaultDirectory', 'addressBookDirectory', 'backupDirectory', 'spendDirectory', 'cacheDirectory']:
            folder = root / key; folder.mkdir(mode=0o700); settings[key] = str(folder)
        setup = root / 'setup.json'; write(setup, settings)
        wrapper = root / 'launcher.json'
        launcher = dict(schema=f'monzero-private-{coin}-light-launcher-v1', helper=str(installed),
                        helperDigest=digest(installed), configFile=str(setup))
        write(wrapper, launcher)
        if os.environ.get('MONZERO_TEST_GUIDED_SETUP') == '1':
            guided = root / 'guided'; guided.mkdir(mode=0o700)
            command = ['bwrap', '--die-with-parent', '--unshare-net', '--ro-bind', '/', '/',
                       '--bind', str(root), str(root), '--dev', '/dev', '--proc', '/proc', '--',
                       str(probe), '--prepare-local', coin, 'regtest', str(guided), str(installed), digest(installed), '', '0']
            for invalid in [('coin', 'xmr'), ('digest', '0'*64), ('host', 'https://example.org')]:
                rejected = command.copy(); rejected[{'coin':-7,'digest':-3,'host':-2}[invalid[0]]] = invalid[1]
                result = subprocess.run(rejected, capture_output=True, timeout=90)
                assert result.returncode != 0 and not result.stdout
                assert not list(guided.iterdir())
            if coin in ['bch', 'doge']:
                rejected = command.copy(); rejected[-6] = 'main'
                assert subprocess.run(rejected, capture_output=True, timeout=90).returncode != 0
                assert not list(guided.iterdir())
            guided.chmod(0o777)
            assert subprocess.run(command, capture_output=True, timeout=90).returncode != 0
            guided.chmod(0o700)
            assert not list(guided.iterdir())
            result = subprocess.run(command, capture_output=True, timeout=90)
            assert result.returncode == 0, (coin, 'guided preparation failed')
            wrapper = Path(result.stdout.decode()); launcher = json.loads(wrapper.read_text())
            setup = Path(launcher['configFile']); settings = json.loads(setup.read_text())
            assert settings['seedEncoding'] == 'bip39-english-v1'
            assert wrapper.stat().st_mode & 0o777 == 0o600
            assert setup.parent.stat().st_mode & 0o777 == 0o700
            for child in setup.parent.rglob('*'):
                assert child.stat().st_mode & 0o777 == (0o700 if child.is_dir() else 0o600)
        password = 'disposable-password-' + uuid.uuid4().hex
        env = dict(os.environ, MONZERO_TEST_LIGHT_COIN=coin, XDG_CONFIG_HOME=str(root / 'preferences'))

        private_material = []

        def call(operation, extra=None, reject=False, credential=None):
            command = ['bwrap', '--die-with-parent', '--unshare-net', '--ro-bind', '/', '/', '--bind', str(root), str(root),
                       '--dev', '/dev', '--proc', '/proc', '--', str(probe), str(wrapper), operation, '10']
            if extra is not None: command.append(json.dumps(extra))
            result = subprocess.run(command, input=(password if credential is None else credential).encode(),
                                    capture_output=True, env=env, timeout=90)
            assert password.encode() not in result.stdout + result.stderr
            assert all(secret not in result.stdout + result.stderr for secret in private_material)
            if reject:
                assert result.returncode != 0 and not result.stdout, (coin, operation, 'unexpected acceptance')
                return
            assert result.returncode == 0, (coin, operation, result.returncode, result.stderr.decode())
            response = json.loads(result.stdout)
            assert response['state'] != 'unknown' and not response['busy'] and not response['executable']
            assert not {'seedHex', 'phraseHex', 'passphraseHex', 'recoverySeed', 'recoveryPassphrase'} & response.keys()
            return response

        assert call('wallet-create-open')['state'] == 'ready'
        received = call('wallet-created-receive')
        address = received['wallet']['address']
        if os.environ.get('MONZERO_TEST_GUIDED_SETUP') == '1':
            attach = ['bwrap', '--die-with-parent', '--unshare-net', '--ro-bind', '/', '/',
                      '--bind', str(root), str(root), '--dev', '/dev', '--proc', '/proc', '--',
                      str(probe), '--attach-local', coin, str(wrapper.parent), str(installed), digest(installed)]
            snapshot = {p: digest(p) for p in wrapper.parent.rglob('*') if p.is_file()}
            reopened = subprocess.run(attach, capture_output=True, timeout=90)
            assert reopened.returncode == 0, (coin, 'saved folder attach failed')
            state = json.loads(reopened.stdout)
            assert state['configured'] and state['setupConfigured'] and not state['busy']
            assert not state['recoveryVisible'] and not state['executable']
            assert all(digest(p) == value for p, value in snapshot.items())
            assert call('wallet-created-receive')['wallet']['address'] == address
            # A self-consistent alternate helper in a saved launcher must not
            # replace the installation pinned by the running desktop.
            alternate = root / 'alternate-helper'; shutil.copyfile(installed, alternate); alternate.chmod(0o500)
            write(wrapper, dict(launcher, helper=str(alternate), helperDigest=digest(alternate)))
            rejected = subprocess.run(attach, capture_output=True, timeout=90)
            assert rejected.returncode != 0 and not rejected.stdout
            write(wrapper, launcher)
            wrong_coin = attach.copy(); wrong_coin[-4] = 'btc' if coin != 'btc' else 'ltc'
            rejected = subprocess.run(wrong_coin, capture_output=True, timeout=90)
            assert rejected.returncode != 0 and not rejected.stdout
            alias = root / 'wallet-alias'; alias.symlink_to(wrapper.parent, target_is_directory=True)
            linked = attach.copy(); linked[-3] = str(alias)
            assert subprocess.run(linked, capture_output=True, timeout=90).returncode != 0
            alias.unlink()

        runtime = Path(settings['setupDirectory']) / 'litecoin-wallet.json'
        configuration = json.loads(runtime.read_text())
        assert configuration['wallet']['vault']['seedEncoding'] == 'bip39-english-v1'
        write(wrapper, dict(launcher, configFile=str(runtime)))
        assert call('wallet-backup-verify', {'directory': settings['backupDirectory']})['wallet']['backupVerified']
        call('wallet-recovery', {'approved': False}, reject=True)
        call('wallet-recovery', {'approvedConfigDigest': '0' * 64}, reject=True)
        call('wallet-recovery', credential='wrong password', reject=True)
        result = call('wallet-recovery-timeout' if coin == 'ltc' else 'wallet-recovery')
        assert result['recoveryEncoding'] == 'bip39-english-v1' and not result['recoveryVisible']
        assert call('wallet-current')['wallet']['address'] == address
        # Disposable recovery material stays in memory and travels over private stdin/stdout.
        request = dict(schema=f'monzero-{coin}-light-wallet-mnemonic-recovery-request-v1',
                       requestId=str(uuid.uuid4()), approvedConfigDigest=digest(runtime),
                       revealApproved=True, password=password)
        command = ['bwrap', '--die-with-parent', '--unshare-net', '--ro-bind', '/', '/',
                   '--bind', str(root), str(root), '--dev', '/dev', '--proc', '/proc', '--',
                   str(installed), '--wallet-mnemonic-recovery-stdio-v1', str(runtime)]
        private = subprocess.run(command, input=json.dumps(request).encode(), capture_output=True, timeout=90)
        assert private.returncode == 0, (coin, 'private fixture recovery failed')
        revealed = json.loads(private.stdout)
        phrase = bytes.fromhex(revealed['phraseHex']).decode()
        passphrase = bytes.fromhex(revealed['passphraseHex']).decode()
        assert len(phrase.split()) == 24 and not passphrase
        private_material.extend([phrase.encode(), revealed['phraseHex'].encode()])
        original = {p: digest(p) for p in root.rglob('*') if p.is_file() and p != wrapper}
        restored = root / 'imported-vault'; restored.mkdir(mode=0o700)
        configuration['wallet']['vault']['directory'] = str(restored)
        imported_config = root / 'import.json'; write(imported_config, configuration)
        write(wrapper, dict(launcher, configFile=str(imported_config)))
        credentials = dict(phrase=phrase, passphrase=passphrase, password=password, confirmation=password)
        invalid_cases = [({'approved': False}, credentials),
                         ({'approvedConfigDigest': '0'*64}, credentials),
                         ({}, dict(credentials, confirmation='mismatch')),
                         ({}, dict(credentials, phrase='abandon '*24)),
                         ({}, dict(credentials, passphrase='wrong BIP39 passphrase'))]
        vault_file = restored / 'litecoin-seed.vault'
        for extra, supplied in invalid_cases:
            call('wallet-mnemonic-restore', extra, reject=True, credential=json.dumps(supplied))
            assert not vault_file.exists()
        assert call('wallet-mnemonic-restore', credential=json.dumps(credentials))['state'] == 'wallet-key-restored'
        ciphertext = digest(vault_file)
        call('wallet-mnemonic-restore', credential=json.dumps(credentials))
        assert digest(vault_file) == ciphertext
        call('wallet-mnemonic-restore', reject=True, credential=json.dumps(
            dict(credentials, password='different-password', confirmation='different-password')))
        assert digest(vault_file) == ciphertext
        call('wallet-recovery')
        assert call('wallet-current')['wallet']['address'] == address
        assert all(digest(p) == previous for p, previous in original.items())
        for path in root.rglob('*'):
            if path.is_file():
                assert phrase.encode() not in path.read_bytes(), (coin, 'plaintext recovery material persisted')
        return dict(coin=coin, status='passed', createdOpenedAndReceived=True, initialBackupVerified=True,
                    privatePhraseRevealAndHide=True, wrongPasswordAndApprovalRejected=True, networkDisabled=True,
                    qtMnemonicImport=True, invalidImportsRejected=True, exactRetryPreservesVault=True,
                    guidedSetup=os.environ.get("MONZERO_TEST_GUIDED_SETUP") == "1",
                    savedFolderReopening=os.environ.get("MONZERO_TEST_GUIDED_SETUP") == "1")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('probe', type=Path); parser.add_argument('helper', type=Path); parser.add_argument('report', type=Path)
    args = parser.parse_args(); assert not args.report.exists()
    probe = args.probe.resolve(strict=True); helper = args.helper.resolve(strict=True)
    pins = dict(probe=digest(probe), helper=digest(helper))
    results = []
    for coin in ['ltc', 'btc', 'bch', 'doge']:
        results.append(check(probe, helper, coin)); print(f'PASS {coin}: offline mnemonic custody and recovery', flush=True)
    assert pins == dict(probe=digest(probe), helper=digest(helper))
    args.report.write_text(json.dumps(dict(schema='monzero-mnemonic-qt-custody-v1', binaries=pins,
        testSha256=digest(Path(__file__)), results=results,
        scope='Local custody only; no real funds, public chains or DEX swap acceptance.'), indent=2)+'\n')


if __name__ == '__main__':
    main()
