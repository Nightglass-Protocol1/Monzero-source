"""Relocate the trusted helper for a funded, activated disposable wallet."""
import json
from pathlib import Path
import shutil
import subprocess
from test_light_wallet_chain import digest


def migrate(probe, launcher, installed, root, settings, coin, env, peer):
    replacement = root / 'updated-helper'
    shutil.copyfile(installed, replacement); replacement.chmod(0o500)
    original_launcher = launcher.read_bytes()
    original = {p: digest(p) for key in ['setupDirectory', 'vaultDirectory', 'addressBookDirectory',
                                       'backupDirectory', 'spendDirectory', 'cacheDirectory']
                for p in Path(settings[key]).rglob('*') if p.is_file()}
    requests = len(peer.requests)
    def call(mode, approval=None, reject=False):
        command = [str(probe), mode, coin, str(root), str(replacement), digest(replacement)]
        if approval is not None: command.append(approval)
        result = subprocess.run(command, env=env, capture_output=True, timeout=90)
        if reject:
            assert result.returncode != 0 and not result.stdout
            return
        assert result.returncode == 0, (coin, mode, result.stderr.decode())
        return json.loads(result.stdout)
    review = call('--review-helper')
    assert review['previousHelper'] == str(installed) and review['helper'] == str(replacement)
    call('--upgrade-helper', '0'*64, reject=True)
    assert not list(root.glob('launcher-helper-*.json'))
    # Old installations may already be gone. Do not execute or require them.
    installed.unlink()
    assert call('--review-helper') == review
    result = call('--upgrade-helper', review['commitment'])
    assert result['wallet']['importActivated'] and result['wallet']['walletReceiveAvailable']
    assert not result['wallet']['executable'] and not result['wallet']['busy']
    migrated = Path(result['launcher']); retained = migrated.read_bytes()
    assert migrated.stat().st_mode & 0o777 == 0o600
    assert call('--upgrade-helper', review['commitment']) == result
    assert migrated.read_bytes() == retained
    assert launcher.read_bytes() == original_launcher
    assert original == {p: digest(p) for p in original}
    assert len(peer.requests) == requests
    return migrated, replacement, dict(originalWalletRecordsPreserved=True, oldHelperAbsent=True,
        originalLauncherPreserved=True, repeatPreservedBytes=True, wrongApprovalRejected=True,
        migrationMadeNoNetworkRequests=True, helperSha256=digest(replacement),
        scope='Same reviewed helper binary relocated; release-version compatibility is not tested.',
        testSha256=digest(Path(__file__)))
