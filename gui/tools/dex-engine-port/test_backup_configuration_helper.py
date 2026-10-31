#!/usr/bin/env python3
"""Offline backup-to-new-configuration acceptance; only disposable private files."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import uuid
from test_light_wallet_local_custody import digest, snapshot


def write(path, value):
    path.write_text(json.dumps(value, sort_keys=True, separators=(',', ':')))
    path.chmod(0o600)


def exercise(helper, root, coin, encoding, probe=None):
    password = 'disposable-backup-' + uuid.uuid4().hex
    def call(mode, config, extra, reject=False):
        identity = str(uuid.uuid4())
        request = dict(schema=f'monzero-{coin}-light-{mode}-request-v1', requestId=identity, **extra)
        result = subprocess.run(['bwrap', '--die-with-parent', '--unshare-net', '--ro-bind', '/', '/',
            '--bind', str(root), str(root), '--dev', '/dev', '--proc', '/proc', '--', str(helper),
            '--'+mode+'-stdio-v1', str(config)], input=json.dumps(request).encode(), capture_output=True, timeout=180)
        assert password.encode() not in result.stdout+result.stderr
        if reject:
            assert result.returncode != 0 and not result.stdout, (coin, mode, 'accepted invalid request')
            return
        assert result.returncode == 0 and not result.stderr, (coin, mode, result.stderr.decode())
        response = json.loads(result.stdout)
        assert response['requestId'] == identity and not response['executable']
        assert response['schema'] == f'monzero-{coin}-light-{mode}-response-v1'
        return response
    source = root / 'original'; source.mkdir(mode=0o700)
    fields = ['setupDirectory','vaultDirectory','addressBookDirectory','backupDirectory','cacheDirectory','spendDirectory']
    settings = dict(schema=f'monzero-private-{coin}-setup-v1',network='regtest',walletId=str(uuid.uuid4()),
                    accountIndex=3,maximumHeight=1000,seedEncoding=encoding)
    for field in fields:
        folder=source/field;folder.mkdir(mode=0o700);settings[field]=str(folder)
    setup=source/'setup.json';write(setup,settings)
    created=call('wallet-create',setup,dict(createApproval=dict(approvedConfigDigest=digest(setup),password=password)))['wallet']
    original=Path(created['configurationFile'])
    # Keep one receive allocation in the exported backup.
    received=call('wallet-receive',original,{})['wallet']
    backup=source/'updated-backup';backup.mkdir(mode=0o700)
    call('wallet-backup-save',original,dict(backupApproval=dict(directory=str(backup),password=password)))
    before=snapshot(source)
    destination=root/'replacement';destination.mkdir(mode=0o700)
    options=dict(schema=f'monzero-private-{coin}-backup-setup-v1',network='regtest',backupDirectory=str(backup),maximumHeight=1000)
    for field in fields:
        if field=='backupDirectory':continue
        folder=destination/field;folder.mkdir(mode=0o700);options[field]=str(folder)
    configuration=destination/'restore.json';write(configuration,options)
    approval=dict(approvedConfigDigest=digest(configuration),password=password)
    blank=snapshot(destination)
    for invalid in [dict(approval,password='wrong password'),dict(approval,approvedConfigDigest='0'*64),dict(approval,restoreApproved=True)]:
        call('wallet-backup-preview',configuration,invalid,True)
        assert snapshot(destination)==blank
    preview=call('wallet-backup-preview',configuration,approval)
    assert preview['review']['walletId']==settings['walletId'] and preview['review']['allocated']==[1,0]
    assert preview['review']['seedEncoding']==encoding and not preview['review']['activeSwapRecovery']
    assert preview['configurationDigest']==digest(configuration) and snapshot(destination)==blank
    restore=dict(approval,approvedReview=preview['reviewDigest'],restoreApproved=True)
    for invalid in [dict(restore,restoreApproved=False),dict(restore,approvedReview='0'*64),dict(restore,password='wrong password')]:
        call('wallet-backup-import',configuration,invalid,True);assert snapshot(destination)==blank
    # The separately approved review must not adopt changed backup/settings bytes.
    encrypted_path=backup/'litecoin-wallet.backup';encrypted=encrypted_path.read_bytes()
    altered=bytearray(encrypted);altered[-1]^=1;encrypted_path.write_bytes(altered)
    call('wallet-backup-import',configuration,restore,True);assert snapshot(destination)==blank
    encrypted_path.write_bytes(encrypted)
    changed=dict(options,maximumHeight=999);write(configuration,changed)
    call('wallet-backup-import',configuration,dict(restore,approvedConfigDigest=digest(configuration)),True)
    write(configuration,options);assert snapshot(destination)==blank
    restored=call('wallet-backup-import',configuration,restore)['restoration']
    runtime=Path(restored['configurationFile'])
    assert digest(runtime)==restored['configurationDigest'] and restored['allocated']==[1,0]
    assert not restored['tradingEnabled'] and not restored['activeSwapRecovery']
    retained=snapshot(destination)
    assert call('wallet-backup-import',configuration,restore)['restoration']==restored
    assert snapshot(destination)==retained
    for field,name in [('vaultDirectory','litecoin-seed.vault'),('addressBookDirectory','litecoin-address-book.json')]:
        assert (Path(options[field])/name).read_bytes()==(Path(settings[field])/name).read_bytes()
    next_address=call('wallet-receive',runtime,{})['wallet']
    assert next_address['derivationPath'].endswith('/0/1') and next_address['address']!=received['address']
    advanced=snapshot(destination)
    call('wallet-backup-import',configuration,restore,True);assert snapshot(destination)==advanced
    assert snapshot(source)==before
    for path in root.rglob('*'):
        if path.is_file():assert password.encode() not in path.read_bytes()
    if probe:
        from test_backup_configuration_qt import verify
        verify(probe,helper,root,backup,coin,password,encoding)
    return dict(qtVerified=bool(probe),coin=coin,seedEncoding=encoding,passed=True,networkDisabled=True,previewWritesNothing=True,
                exactRetry=True,originalUnchanged=True,newAddressIndex=1,staleRestoreRejected=True,activeSwapRecovery=False)


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--probe',type=Path);parser.add_argument('helper',type=Path);parser.add_argument('report',type=Path)
    args=parser.parse_args();assert shutil.which('bwrap') and not args.report.exists()
    helper=args.helper.resolve(strict=True);pin=digest(helper);results=[]
    for coin in ['ltc','btc','bch','doge']:
        for encoding in ['raw-bip32-seed-hex-v1','bip39-english-v1']:
            with tempfile.TemporaryDirectory(prefix='monzero-backup-config-') as folder:
                results.append(exercise(helper,Path(folder).resolve(),coin,encoding,args.probe.resolve(strict=True) if args.probe else None))
    assert digest(helper)==pin
    write(args.report,dict(schema='monzero-backup-configuration-helper-test-v1',helperSha256=pin,
        testSha256=digest(Path(__file__)),probeSha256=digest(args.probe) if args.probe else None,
        qtTestSha256=digest(Path(__file__).with_name("test_backup_configuration_qt.py")) if args.probe else None,results=results,scope='Offline helper configuration recovery; no GUI or live DEX acceptance.'))
    print('PASS: eight coin/format backup previews and approved configuration restores')


if __name__=='__main__':main()
