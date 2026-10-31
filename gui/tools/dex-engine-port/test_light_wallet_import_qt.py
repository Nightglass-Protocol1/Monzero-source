#!/usr/bin/env python3
"""Real Qt/helper import review, approval and restart checks; isolated local keys."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import uuid
from test_light_wallet_import_helper import PHRASE, OTHER, digest, write


def check(probe, helper, coin):
    with tempfile.TemporaryDirectory(prefix='monzero-import-qt-') as temporary:
        root = Path(temporary).resolve()
        installed = root/'helper'; shutil.copyfile(helper, installed); installed.chmod(0o500)
        settings = dict(schema=f'monzero-private-{coin}-setup-v1', network='regtest', seedEncoding='bip39-english-v1',
                        walletId=str(uuid.uuid4()), accountIndex=3, maximumHeight=1000)
        for key in ['setupDirectory','vaultDirectory','addressBookDirectory','backupDirectory','spendDirectory','cacheDirectory']:
            path=root/key;path.mkdir(mode=0o700);settings[key]=str(path)
        config=root/'setup.json';write(config,settings)
        wrapper=root/'launcher.json';write(wrapper,dict(schema=f'monzero-private-{coin}-light-launcher-v1',
            helper=str(installed),helperDigest=digest(installed),configFile=str(config)))
        password='disposable-'+uuid.uuid4().hex
        credentials=dict(phrase=PHRASE,passphrase='PUBLIC additional passphrase',password=password,confirmation=password)
        env=dict(os.environ,MONZERO_TEST_LIGHT_COIN=coin,XDG_CONFIG_HOME=str(root/'preferences'))
        base=['bwrap','--die-with-parent','--unshare-net','--ro-bind','/','/','--bind',str(root),str(root),
              '--dev','/dev','--proc','/proc','--',str(probe)]
        def call(operation, supplied=None, extra=None, reject=False):
            command=base+[str(wrapper),operation,'10']
            if extra is not None:command.append(json.dumps(extra))
            result=subprocess.run(command,input=json.dumps(credentials if supplied is None else supplied).encode(),
                                  capture_output=True,env=env,timeout=90)
            for secret in [PHRASE,OTHER,password,credentials['passphrase']]:
                assert secret.encode() not in result.stdout+result.stderr
            if reject:
                assert result.returncode != 0 and not result.stdout,(coin,operation,'unexpected acceptance')
                return
            assert result.returncode==0,(coin,operation,result.returncode,result.stderr.decode())
            return json.loads(result.stdout)
        initial={p:digest(p) for p in root.rglob('*') if p.is_file()}
        preview=call('wallet-import-preview')
        assert preview['state']=='wallet-import-reviewed' and preview['importReview']['review']['account']['account']==3
        for supplied,extra in [(credentials,{'approved':False}),(credentials,{'approvedReview':'0'*64}),
                               (dict(credentials,confirmation='mismatch'),{}),(dict(credentials,importPhrase=OTHER),{})]:
            call('wallet-import-flow',supplied,extra,reject=True)
            assert initial=={p:digest(p) for p in root.rglob('*') if p.is_file()}
        imported=call('wallet-import-flow')
        assert imported['state']=='wallet-import-pending' and imported['walletImportPending']
        assert not imported['walletCreateAvailable'] and not imported['walletReceiveAvailable'] and not imported['walletSignAvailable']
        assert imported['wallet']['backupVerified'] and imported['wallet']['discoveryRequired']
        retained={p:digest(p) for p in root.rglob('*') if p.is_file()}
        restarted=subprocess.run(base+['--attach-local',coin,str(root),str(installed),digest(installed)],capture_output=True,env=env,timeout=90)
        assert restarted.returncode==0,(coin,'restart failed')
        state=json.loads(restarted.stdout)
        assert state['state']=='wallet-import-pending' and state['walletImportPending']
        assert not state['walletCreateAvailable'] and not state['walletReceiveAvailable']
        assert not state['wallet']['backupVerified']  # Saved receipt is not a fresh password-based backup check.
        assert call('wallet-import-flow')['wallet']==imported['wallet']
        assert retained=={p:digest(p) for p in root.rglob('*') if p.is_file()}
        return dict(coin=coin,passed=True,previewValidated=True,approvalMismatchRejected=True,
                    pendingImport=True,restartKeepsPaymentsDisabled=True,retryPreservesFiles=True)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('probe',type=Path);parser.add_argument('helper',type=Path);parser.add_argument('report',type=Path)
    args=parser.parse_args();assert not args.report.exists()
    probe=args.probe.resolve(strict=True);helper=args.helper.resolve(strict=True)
    pins=dict(probe=digest(probe),helper=digest(helper));results=[]
    for coin in ['ltc','btc','bch','doge']:
        results.append(check(probe,helper,coin));print('PASS '+coin+': Qt import and pending restart',flush=True)
    assert pins==dict(probe=digest(probe),helper=digest(helper))
    args.report.write_text(json.dumps(dict(schema='monzero-light-wallet-import-qt-v1',binaries=pins,results=results,
        testSha256=digest(Path(__file__)),networkDisabled=True,scope='Pending keys-only import; no discovery or DEX activation.'),indent=2)+'\n')


if __name__=='__main__':main()
