#!/usr/bin/env python3
"""Verify local light-wallet custody with the helper's network disabled.

Disposable regtest wallets only. The filesystem is read-only except for the
owned temporary directory. No node, server, mining, or existing wallet is used.
This verifies offline lifecycle behavior, not privacy of online chain queries.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import stat
import subprocess
import tempfile
import uuid


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def private_json(path, value):
    with path.open('x') as stream:
        os.chmod(path, 0o600)
        json.dump(value, stream)


def snapshot(root):
    return {str(p.relative_to(root)): digest(p) for p in root.rglob('*') if p.is_file()}


def check_chain(program, root, chain):
    password = 'disposable-custody-' + uuid.uuid4().hex
    directories = {}
    for field, name in [('setupDirectory', 'setup'), ('vaultDirectory', 'vault'),
                        ('addressBookDirectory', 'addresses'), ('backupDirectory', 'backup'),
                        ('spendDirectory', 'spends'), ('cacheDirectory', 'cache')]:
        directory = root / name
        directory.mkdir(mode=0o700)
        directories[field] = str(directory)
    setup = root / 'setup.json'
    private_json(setup, dict(directories, schema=f'monzero-private-{chain}-setup-v1',
                            network='regtest', walletId=str(uuid.uuid4()),
                            accountIndex=0, maximumHeight=1000))

    def call(mode, config, extra=None, rejected=False, tag=None):
        tag = tag or chain
        request_id = str(uuid.uuid4())
        request = dict(schema=f'monzero-{tag}-light-{mode}-request-v1', requestId=request_id)
        request.update(extra or {})
        result = subprocess.run([
            'bwrap', '--die-with-parent', '--unshare-net', '--ro-bind', '/', '/',
            '--bind', str(root), str(root), '--dev', '/dev', '--proc', '/proc',
            '--', str(program), f'--{mode}-stdio-v1', str(config)
        ], input=json.dumps(request).encode(), capture_output=True, timeout=180)
        assert password.encode() not in result.stdout + result.stderr, 'password leaked to output'
        if rejected:
            assert result.returncode != 0 and not result.stdout, 'request unexpectedly succeeded'
            return None
        assert result.returncode == 0, f'{chain} {mode} failed: {result.stderr.decode()}'
        assert not result.stderr, 'unexpected diagnostics'
        response = json.loads(result.stdout)
        assert response['schema'] == f'monzero-{tag}-light-{mode}-response-v1'
        assert response['requestId'] == request_id and response['executable'] is False
        return response['wallet']

    approval = {'createApproval': {'approvedConfigDigest': digest(setup), 'password': password}}
    created = call('wallet-create', setup, approval)
    config = Path(created['configurationFile'])
    assert config.is_relative_to(root) and digest(config) == created['configurationDigest']
    runtime = json.loads(config.read_text())
    assert runtime['schema'] == f'monzero-private-{chain}-light-v1'
    assert runtime['wallet']['account']['schema'] == f'monzero-{chain}-public-account-v1'
    assert not runtime.get('endpoint'), 'offline wallet unexpectedly contains an endpoint'
    original = snapshot(root)
    reopened = call('wallet-create', setup, approval)
    assert reopened['configurationDigest'] == created['configurationDigest']
    assert reopened['walletId'] == created['walletId']
    assert snapshot(root) == original, 'repeat creation changed existing wallet'
    backup = {'backupApproval': {'directory': directories['backupDirectory'], 'password': password}}
    verified = call('wallet-backup-verify', config, backup)
    assert verified['backupVerified'] and verified['activeSwapRecovery'] is False
    original = snapshot(root)
    wrong = {'backupApproval': dict(backup['backupApproval'], password='incorrect disposable password')}
    call('wallet-backup-verify', config, wrong, rejected=True)
    assert snapshot(root) == original, 'incorrect password changed local records'
    other_chain = 'btc' if chain != 'btc' else 'bch'
    call('wallet-backup-verify', config, backup, rejected=True, tag=other_chain)
    assert snapshot(root) == original, 'wrong-chain request changed local records'

    # Restore into fresh local directories; the original vault is never replaced.
    restored = json.loads(config.read_text())
    for field, name in [('addressBookDirectory', 'restored-addresses')]:
        directory = root / name; directory.mkdir(mode=0o700)
        restored['wallet'][field] = str(directory)
    vault = root / 'restored-vault'; vault.mkdir(mode=0o700)
    restored['wallet']['vault']['directory'] = str(vault)
    restore_config = root / 'restore.json'; private_json(restore_config, restored)
    restored_wallet = call('wallet-restore', restore_config, {'restoreApproval': {
        'directory': directories['backupDirectory'], 'password': password,
        'approvedDigest': verified['backupDigest']}})
    assert restored_wallet['restored'] and restored_wallet['activeSwapRecovery'] is False
    original_address = call('wallet-receive', config)['address']
    restored_address = call('wallet-receive', restore_config)['address']
    assert original_address == restored_address, 'restore derived a different first address'
    for path in root.rglob('*'):
        assert not path.is_symlink(), 'unexpected symlink in local wallet'
        assert path.stat().st_uid == os.getuid(), 'wallet file not owned by test user'
        expected = 0o700 if path.is_dir() else 0o600
        assert stat.S_IMODE(path.stat().st_mode) == expected, f'unsafe permissions: {path.name}'
        if path.is_file():
            assert password.encode() not in path.read_bytes(), 'plaintext password persisted'
    return {'chain': chain, 'status': 'passed', 'networkDisabled': True,
            'localCreateBackupRestore': True, 'sameRestoredAddress': True,
            'wrongPasswordAndChainRejected': True, 'repeatCreationPreservedWallet': True,
            'ownerOnlyPermissions': True, 'activeSwapRecovery': False}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('helper', type=Path)
    parser.add_argument('report', type=Path)
    args = parser.parse_args()
    assert shutil.which('bwrap'), 'bubblewrap is required; no unsandboxed fallback'
    assert not args.report.exists(), 'refusing to replace an existing report'
    program = args.helper.resolve(strict=True)
    before = digest(program)
    results = []
    for chain in ['ltc', 'btc', 'bch', 'doge']:
        with tempfile.TemporaryDirectory(prefix=f'monzero-{chain}-custody-') as temp:
            results.append(check_chain(program, Path(temp).resolve(), chain))
        print(f'PASS {chain}: local create/backup/restore with networking disabled', flush=True)
    assert digest(program) == before, 'helper changed during acceptance'
    args.report.write_text(json.dumps({'schema': 'monzero-light-wallet-local-custody-v1',
        'helperSha256': before, 'testSha256': digest(Path(__file__)), 'results': results,
        'limits': ['Disposable regtest wallets; not mainnet acceptance.',
                   'Does not prove online query privacy, memory erasure or full DEX recovery.',
                   'No desktop integration or live trading enabled.']}, indent=2) + '\n')


if __name__ == '__main__':
    main()
