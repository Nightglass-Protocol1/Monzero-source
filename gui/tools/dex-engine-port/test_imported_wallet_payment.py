"""Payment and refreshed-backup acceptance for the owned imported-wallet fixture."""
import json
import subprocess
import uuid
from pathlib import Path
from test_light_wallet_chain import digest, sats


def verify(probe, wrapper, coin, node, wallet, anchor, root, settings, password, env, attach):
    """Use fresh Qt processes for each reviewed payment step and local backup."""
    # Keep Qt's public receive-request tokens inside the disposable fixture too.
    qt_settings = root / 'payment-qt-settings'; qt_settings.mkdir(mode=0o700)
    env = dict(env, XDG_CONFIG_HOME=str(qt_settings))
    def call(mode, extra=None, secret='', reject=False, launcher=None, full=False):
        result = subprocess.run([str(probe), str(launcher or wrapper), mode, '1000' if mode in ['wallet-reconcile', 'sync'] else '20',
                                 json.dumps(extra or {})], input=secret.encode(), capture_output=True,
                                env=dict(env, MONZERO_TEST_LIGHT_COIN=coin), timeout=180)
        assert password.encode() not in result.stdout + result.stderr
        value = json.loads(result.stdout) if result.stdout else {}
        if reject:
            assert result.returncode != 0 or value.get('state') == 'unknown', (mode, value)
            return
        assert result.returncode == 0 and value.get('state') != 'unknown', (coin, mode, value, result.stderr.decode())
        assert not value['executable'] and not value['busy']
        return value if full else value['wallet']

    historical = {p: digest(p) for p in Path(settings['setupDirectory']).glob('import-recovery-*/backup/*') if p.is_file()}
    assert historical
    credit, pay, fee, minimum = (10_000_000_000, 4_000_000_000, 10_000_000, 100_000_000) if coin == 'doge' else (1_000_000, 400_000, 2_000, 1_000)
    destination = node.rpc('getnewaddress', wallet=wallet)
    payment = dict(address=destination, amountAtomic=str(pay), feeAtomic=str(fee),
                   minimumChangeAtomic=str(minimum), minimumConfirmations=1)
    request = str(uuid.uuid4())
    options = dict(requestId=request, payment=payment)
    review = call('wallet-prepare', options)
    assert review['paymentAtomic'] == str(pay) and review['changeAtomic'] == str(credit-pay-fee)
    retained = dict(signingCommitment=review['signingCommitment'], changeAddress=review['changeAddress'])
    signing = dict(options, retainedReview=retained)
    call('wallet-sign', signing, 'incorrect disposable password', reject=True)
    assert not list(Path(settings['spendDirectory']).glob('ltc-signed-*.tx'))
    signed = call('wallet-sign', signing, password)
    raw = (Path(settings['spendDirectory']) / ('ltc-signed-'+signed['signingCommitment']+'.tx')).read_bytes()
    if coin != 'doge':
        acceptance = node.rpc('testmempoolaccept', [raw.hex()])[0]
        assert acceptance['allowed'], acceptance
    else:
        assert node.rpc('decoderawtransaction', raw.hex())['txid'] == signed['txid']
    assert signed['txid'] not in node.rpc('getrawmempool')
    sent = call('wallet-broadcast', dict(options, savedReview=review, approvedCommitment=signed['signingCommitment'],
                approvedTxid=signed['txid'], approvedEndpoint=settings['endpoint']))
    assert sent['broadcastStatus'] == 'server-acknowledged' and signed['txid'] in node.rpc('getrawmempool')
    block = node.mine(1, anchor)[0]
    reconciled = call('wallet-reconcile', dict(options, savedReview=review))
    assert reconciled['confirmationStatus'] == 'included' and reconciled['paymentObservation']['blockHash'] == block
    decoded = node.rpc('getrawtransaction', signed['txid'], 1)
    paid = [out for out in decoded['vout'] if out['scriptPubKey'].get('address') == destination
            or destination in out['scriptPubKey'].get('addresses', [])]
    assert len(paid) == 1 and sats(paid[0]['value']) == pay
    balance = call('wallet-refresh')
    assert balance['observedConfirmedAtomic'] == str(credit-pay-fee)
    backup = root / 'post-payment-backup'; backup.mkdir(mode=0o700)
    saved = call('wallet-backup-save', dict(directory=str(backup)), password)
    verified = call('wallet-backup-verify', dict(directory=str(backup)), password)
    assert saved['backupVerified'] and verified['backupVerified'] and saved['backupDigest'] == verified['backupDigest']
    assert [saved['receivingAllocated'], saved['changeAllocated']] == [5, 4], saved
    assert historical == {p: digest(p) for p in historical}
    reopened = attach()
    assert reopened['importActivated'] and reopened['walletReceiveAvailable'] and not reopened['importRecovery']
    restore_evidence = restore_refreshed(call, wrapper, root, settings, backup, saved, password, credit-pay-fee, node, coin, probe, env, anchor)
    return dict(restoredBackup=restore_evidence, paymentTxid=signed['txid'], paymentBlock=block, confirmedChangeAtomic=str(credit-pay-fee),
                refreshedBackupAllocations=[5, 4], historicalBackupUnchanged=True, wrongPasswordRejected=True,
                separateBroadcastVerified=True, testSha256=digest(Path(__file__)))


def restore_refreshed(call, wrapper, root, settings, backup, saved, password, balance, node, coin, probe, env, anchor):
    """Use guided recovery settings, then prove actual spending from restored keys."""
    launch=json.loads(wrapper.read_text());endpoint=settings['endpoint']
    prepared=subprocess.run([str(probe),'--prepare-backup-local',coin,'regtest',str(root),launch['helper'],
        launch['helperDigest'],str(backup),endpoint['host'],str(endpoint['port'])],capture_output=True,env=env,timeout=90)
    assert prepared.returncode==0,(coin,'guided backup setup',prepared.stderr.decode())
    launcher=Path(prepared.stdout.decode());assert launcher.is_relative_to(root)
    restore_settings=json.loads(Path(json.loads(launcher.read_text())['configFile']).read_text())
    folders={name:Path(restore_settings[field]) for name,field in [('vault','vaultDirectory'),
        ('addresses','addressBookDirectory'),('cache','cacheDirectory'),('spends','spendDirectory')]}
    def restored_call(mode, extra=None, secret='', reject=False, full=False):
        return call(mode, extra, secret, reject, launcher=launcher, full=full)
    def reopen():
        opened=subprocess.run([str(probe),'--attach-local',coin,str(launcher.parent),launch['helper'],launch['helperDigest']],
            capture_output=True,env=env,timeout=90)
        assert opened.returncode==0,(coin,'guided restored reopen',opened.stderr.decode())
        state=json.loads(opened.stdout);assert state['walletReceiveAvailable'] and state['backupRestoration']['restored']
        assert not state['walletInitAvailable'] and not state['executable']
        return state
    originals = {p: digest(p) for key in ['setupDirectory', 'vaultDirectory', 'addressBookDirectory', 'spendDirectory']
                 for p in Path(settings[key]).rglob('*') if p.is_file()}
    backup_before = {p: digest(p) for p in backup.iterdir() if p.is_file()}
    approval = dict(directory=str(backup))
    for operation, options, secret in [('wallet-backup-preview',{},'incorrect disposable password'),
            ('wallet-backup-import',dict(approved=False),password),
            ('wallet-backup-import',dict(approvedReview='0'*64),password)]:
        restored_call(operation,options,secret,reject=True,full=True)
        assert not list(folders['vault'].iterdir()) and not list(folders['addresses'].iterdir())
    preview=restored_call('wallet-backup-preview',{},password,full=True)
    assert preview['backupImportReview']['review']['backupDigest']==saved['backupDigest']
    restored=restored_call('wallet-backup-import',{},password,full=True)['backupRestoration']
    assert restored['restored'] and not restored['activeSwapRecovery'] and restored['allocated']==[5,4]
    assert restored['backupDigest']==saved['backupDigest'];reopen()
    for name, source, record in [('vault', 'vaultDirectory', 'litecoin-seed.vault'),
                                ('addresses', 'addressBookDirectory', 'litecoin-address-book.json')]:
        assert (folders[name] / record).read_bytes() == (Path(settings[source]) / record).read_bytes()
    retained = {p: digest(p) for name in ['vault', 'addresses'] for p in folders[name].iterdir() if p.is_file()}
    restored_call('wallet-restore', approval, password)
    assert retained == {p: digest(p) for p in retained}
    restored_call('sync')
    observed = restored_call('wallet-refresh')
    assert observed['observedConfirmedAtomic'] == str(balance)
    # The next receive request must not reuse any address from the backup.
    received = restored_call('wallet-current')
    assert received['derivationPath'].endswith('/0/5') and received['receivingAllocated'] == 6
    # A later allocation must never be erased by restoring the earlier backup.
    advanced = {p: digest(p) for p in folders['addresses'].iterdir() if p.is_file()}
    restored_call('wallet-restore', approval, password, reject=True)
    assert advanced == {p: digest(p) for p in advanced}
    fee = 10_000_000 if coin == 'doge' else 2_000
    amount = 100_000_000 if coin == 'doge' else 100_000
    payment = dict(address=node.rpc('getnewaddress'), amountAtomic=str(amount), feeAtomic=str(fee),
                   minimumChangeAtomic=str(amount), minimumConfirmations=1)
    options = dict(requestId=str(uuid.uuid4()), payment=payment)
    review = restored_call('wallet-prepare', options)
    retained_review = dict(signingCommitment=review['signingCommitment'], changeAddress=review['changeAddress'])
    signed = restored_call('wallet-sign', dict(options, retainedReview=retained_review), password)
    raw = (folders['spends'] / ('ltc-signed-'+signed['signingCommitment']+'.tx')).read_bytes()
    if coin != 'doge':
        result = node.rpc('testmempoolaccept', [raw.hex()])[0]
        assert result['allowed'], result
    else:
        assert node.rpc('decoderawtransaction', raw.hex())['txid'] == signed['txid']
    assert signed['txid'] not in node.rpc('getrawmempool')
    sent=restored_call('wallet-broadcast',dict(options,savedReview=review,approvedCommitment=signed['signingCommitment'],
        approvedTxid=signed['txid'],approvedEndpoint=endpoint))
    assert sent['broadcastStatus']=='server-acknowledged' and signed['txid'] in node.rpc('getrawmempool')
    block=node.mine(1,anchor)[0]
    reconciled=restored_call('wallet-reconcile',dict(options,savedReview=review))
    assert reconciled['confirmationStatus']=='included' and reconciled['paymentObservation']['blockHash']==block
    transaction=node.rpc('getrawtransaction',signed['txid'],1)
    paid=[out for out in transaction['vout'] if out['scriptPubKey'].get('address')==payment['address']
        or payment['address'] in out['scriptPubKey'].get('addresses',[])]
    assert len(paid)==1 and sats(paid[0]['value'])==amount and transaction['confirmations']==1
    remaining=restored_call('wallet-refresh')
    assert remaining['observedConfirmedAtomic']==str(balance-amount-fee)
    reopen()
    assert originals == {p: digest(p) for p in originals}
    assert backup_before == {p: digest(p) for p in backup_before}
    return dict(guidedRestore=True,paymentTxid=signed["txid"],paymentBlock=block,remainingAtomic=str(balance-amount-fee),
                exactVaultAndAddressRecords=True, wrongPasswordAndDigestRejected=True,
                repeatRestorePreservedBytes=True, staleRestoreRejected=True, balanceAtomic=str(balance),
                nextReceiveIndex=5, restoredKeysSigned=True, signingDidNotBroadcast=True, separatelyApprovedPaymentConfirmed=True,
                originalWalletAndBackupUnchanged=True, activeSwapRecovery=False)
