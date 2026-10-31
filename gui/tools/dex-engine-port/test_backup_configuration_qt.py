"""Exercise backup folder preparation, restore and guarded restart via Qt."""
import json
import os
from pathlib import Path
import shutil
import subprocess
from test_light_wallet_local_custody import digest, snapshot


def verify(probe, helper, root, backup, coin, password, encoding):
    installed=root/'qt-helper';shutil.copyfile(helper,installed);installed.chmod(0o500)
    preferences=root/'qt-preferences';preferences.mkdir(mode=0o700)
    env=dict(os.environ,MONZERO_TEST_LIGHT_COIN=coin,XDG_CONFIG_HOME=str(preferences))
    def invoke(arguments,secret='',reject=False,plain=False):
        run=subprocess.run(['bwrap','--die-with-parent','--unshare-net','--ro-bind','/','/',
            '--bind',str(root),str(root),'--dev','/dev','--proc','/proc','--',str(probe),*map(str,arguments)],
            input=secret.encode(),capture_output=True,env=env,timeout=180)
        assert password.encode() not in run.stdout+run.stderr
        if reject:
            assert run.returncode!=0 and not run.stdout,(coin,arguments,run.stdout.decode())
            return
        assert run.returncode==0,(coin,arguments,run.stderr.decode())
        return run.stdout.decode() if plain else json.loads(run.stdout)
    launcher=Path(invoke(['--prepare-backup-local',coin,'regtest',root,installed,digest(installed),backup,'',0],plain=True))
    folder=launcher.parent;settings=json.loads(Path(json.loads(launcher.read_text())['configFile']).read_text())
    setup=Path(settings['setupDirectory']);blank=snapshot(folder)
    invoke([launcher,'wallet-backup-preview',0], 'wrong password',reject=True)
    assert snapshot(folder)==blank
    review=invoke([launcher,'wallet-backup-preview',0],password)
    assert review['state']=='backup-import-reviewed' and not review['walletReceiveAvailable']
    assert review['backupImportReview']['review']['seedEncoding']==encoding and snapshot(folder)==blank
    for approval in [dict(approved=False),dict(approvedReview='0'*64)]:
        invoke([launcher,'wallet-backup-import',0,json.dumps(approval)],password,reject=True)
        assert snapshot(folder)==blank
    result=invoke([launcher,'wallet-backup-import',0],password)
    assert result['state']=='ready' and result['backupRestoration']['restored'] and result['walletReceiveAvailable']
    assert not result['backupSetupConfigured'] and not result['executable']
    def attach():return invoke(['--attach-local',coin,folder,installed,digest(installed)])
    assert attach()['walletReceiveAvailable']
    complete=setup/'litecoin-backup-restore-complete.json';held=root/'held-receipt';complete.rename(held)
    incomplete=attach();assert incomplete['state']=='backup-restore-incomplete' and not incomplete['walletReceiveAvailable']
    held.rename(complete)
    receipt=complete.read_bytes();altered=json.loads(receipt);altered['activeSwapRecovery']=True
    complete.write_text(json.dumps(altered,sort_keys=True,separators=(',',':')))
    invalid=attach();assert invalid['state']=='backup-restore-invalid' and not invalid['walletReceiveAvailable']
    complete.write_bytes(receipt)
    config=setup/'litecoin-wallet-restored.json';original=config.read_bytes();config.write_bytes(b'{}')
    invalid=attach();assert invalid['state']=='backup-restore-invalid' and not invalid['walletReceiveAvailable']
    config.write_bytes(original)
    # The original backup can be disconnected after a completed restore.
    offline=root/'disconnected-backup';backup.rename(offline)
    try:assert attach()['walletReceiveAvailable']
    finally:offline.rename(backup)
    received=invoke([launcher,'wallet-receive',0])
    assert received['wallet']['derivationPath'].endswith('/0/1')
    reopened=attach();assert reopened['walletReceiveAvailable'] and not reopened['walletInitAvailable']
    assert reopened['backupRestoration']['allocated']==[1,0] # historical, not current counts
    assert (setup/'litecoin-backup-restore-intent.json').stat().st_mode&0o777==0o600
