#!/usr/bin/env python3
"""Offline disposable mnemonic preview/import, approval and pending-schema checks."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import uuid

PHRASE = 'abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon about'
OTHER = 'legal winner thank year wave sausage worth useful legal winner thank yellow'


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write(path, value):
    path.write_text(json.dumps(value, sort_keys=True, separators=(',', ':')))
    path.chmod(0o600)


def check(helper, coin):
    with tempfile.TemporaryDirectory(prefix='monzero-import-helper-') as temporary:
        root = Path(temporary).resolve()
        installed = root/'helper'; shutil.copyfile(helper, installed); installed.chmod(0o500)
        settings = dict(schema=f'monzero-private-{coin}-setup-v1', network='regtest', seedEncoding='bip39-english-v1',
                        walletId=str(uuid.uuid4()), accountIndex=3, maximumHeight=1000)
        for key in ['setupDirectory','vaultDirectory','addressBookDirectory','backupDirectory','spendDirectory','cacheDirectory']:
            path = root/key; path.mkdir(mode=0o700); settings[key] = str(path)
        config = root/'setup.json'; write(config, settings)
        password = 'disposable-'+uuid.uuid4().hex
        passphrase = 'PUBLIC fixture passphrase café' if coin in ['ltc','bch'] else ''
        preview_request = dict(schema=f'monzero-{coin}-light-wallet-import-preview-request-v1', requestId=str(uuid.uuid4()),
            approvedConfigDigest=digest(config), seedEncoding='bip39-english-v1', phrase=PHRASE, recoveryPassphrase=passphrase)

        def call(operation, request, reject=False, configuration=config):
            command = ['bwrap', '--die-with-parent', '--unshare-net', '--ro-bind', '/', '/', '--bind', str(root), str(root),
                       '--dev', '/dev', '--proc', '/proc', '--', str(installed), '--'+operation+'-stdio-v1', str(configuration)]
            result = subprocess.run(command, input=json.dumps(request).encode(), capture_output=True, timeout=90)
            for secret in [PHRASE, OTHER, password, passphrase]:
                if secret: assert secret.encode() not in result.stdout+result.stderr
            if reject:
                assert result.returncode != 0 and not result.stdout, (coin, operation, 'unexpected acceptance')
                return None
            assert result.returncode == 0, (coin, operation, result.returncode, result.stderr.decode())
            return json.loads(result.stdout)

        initial = {p: digest(p) for p in root.rglob('*') if p.is_file()}
        preview = call('wallet-import-preview', preview_request)
        assert initial == {p: digest(p) for p in root.rglob('*') if p.is_file()}
        review = preview['review']; commitment = preview['reviewDigest']
        assert review['coin'] == coin and review['account']['account'] == 3
        assert review['wordCount'] == 12 and review['additionalPassphraseUsed'] == bool(passphrase)
        assert review['discoveryRequired'] and not review['activeSwapRecovery']
        assert review['receivePath'].endswith("/3'/0/0")
        call('wallet-import-preview', dict(preview_request, password=password), reject=True)
        call('wallet-import-preview', dict(preview_request, phrase='abandon '*12), reject=True)
        imported_request = dict(preview_request, schema=f'monzero-{coin}-light-wallet-import-request-v1',
                                password=password, approvedReview=commitment, importApproved=True)
        for request in [dict(imported_request, importApproved=False), dict(imported_request, approvedReview='0'*64),
                        dict(imported_request, approvedConfigDigest='0'*64), dict(imported_request, phrase=OTHER),
                        dict(imported_request, recoveryPassphrase=passphrase+'different'),
                        dict(imported_request, requestId=str(uuid.UUID(int=0)))]:
            call('wallet-import', request, reject=True)
            assert initial == {p: digest(p) for p in root.rglob('*') if p.is_file()}
        imported = call('wallet-import', imported_request)
        wallet = imported['wallet']
        assert wallet['imported'] and not wallet['spendable'] and wallet['discoveryRequired']
        pending = Path(wallet['configurationFile'])
        pending_config = json.loads(pending.read_text())
        assert pending_config['schema'] == f'monzero-private-{coin}-light-recovery-pending-v1'
        assert pending_config['wallet']['account'] == review['account']
        retained = {p: digest(p) for p in root.rglob('*') if p.is_file()}
        assert call('wallet-import', imported_request)['wallet'] == wallet
        call('wallet-import', dict(imported_request, password='wrong password'), reject=True)
        call('wallet-init', dict(schema=f'monzero-{coin}-light-wallet-init-request-v1', requestId=str(uuid.uuid4())),
             reject=True, configuration=pending)
        call('wallet-create', dict(schema=f'monzero-{coin}-light-wallet-create-request-v1', requestId=str(uuid.uuid4()),
             createApproval=dict(approvedConfigDigest=digest(config), password=password)), reject=True)
        assert retained == {p: digest(p) for p in root.rglob('*') if p.is_file()}
        for path in retained:
            for secret in [PHRASE, password, passphrase]:
                if secret: assert secret.encode() not in path.read_bytes()
        return dict(coin=coin, passed=True, previewWritesNothing=True, approvalMismatchRejected=True,
                    encryptedImport=True, exactRetryPreserved=True, normalWalletActivationRejected=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('helper', type=Path); parser.add_argument('report', type=Path)
    args = parser.parse_args(); assert not args.report.exists()
    helper = args.helper.resolve(strict=True); pin = digest(helper)
    results = []
    for coin in ['ltc','btc','bch','doge']:
        results.append(check(helper, coin)); print('PASS '+coin+': private import preview and pending wallet', flush=True)
    assert pin == digest(helper)
    args.report.write_text(json.dumps(dict(schema='monzero-light-wallet-import-helper-test-v1', helperSha256=pin,
        testSha256=digest(Path(__file__)), results=results, networkDisabled=True,
        scope='Keys-only pending imports; no address discovery, public chain or active-trade recovery.'), indent=2)+'\n')


if __name__ == '__main__':
    main()
