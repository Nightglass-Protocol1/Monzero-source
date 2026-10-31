#!/usr/bin/env python3
"""Owned-chain probe called by the Rust authenticated BCH helper fixture.
Usage: SCRIPT HELPER DEX_CONFIG NODE REPORT
No public peers or user wallets are used. Optional signing uses disposable local vaults.
"""
import copy
import hashlib
import json
import os
import signal
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import uuid
import test_light_wallet_chain as lab


def fingerprint(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def records(paths):
    return {str(p): fingerprint(p) for directory in paths for p in directory.iterdir() if p.is_file()}


def run():
    assert len(sys.argv) == 5 or (len(sys.argv) == 6 and sys.argv[5] in ('--observe', '--send', '--retry'))
    observe_mode = len(sys.argv) == 6 and sys.argv[5] == '--observe'
    send_mode = len(sys.argv) == 6 and sys.argv[5] in ('--send', '--retry')
    retry_mode = len(sys.argv) == 6 and sys.argv[5] == '--retry'
    kill_mode = retry_mode and os.environ.get('MONZERO_BCH_PROBE_KILL_SEND') == '1'
    helper, dex_path, node_path, report = map(lambda p: Path(p).resolve(), sys.argv[1:5])
    qt_controller = os.environ.get('MONZERO_BCH_QT_CONTROLLER') == '1'
    qt_engine = os.environ.get('MONZERO_BCH_QT_ENGINE') == '1'
    qt_probe = os.environ.get('MONZERO_BCH_QT_PROBE')
    if qt_probe:
        qt_probe = Path(qt_probe).resolve()
        assert fingerprint(qt_probe) == os.environ['MONZERO_BCH_QT_PROBE_SHA256']
    assert not report.exists()
    dex = json.loads(dex_path.read_bytes())
    wallet_path = Path(dex['walletConfigFile'])
    original = wallet_path.read_bytes()
    lab.CHAIN = 'bch'
    with tempfile.TemporaryDirectory(prefix='monzero-bch-dex-prepare-') as name:
        root = Path(name)
        tls = root / 'tls'
        lab.generate_tls(tls)
        for directory in ('node', 'cache', 'spends'):
            (root / directory).mkdir(mode=0o700)
        node = lab.Node(node_path, root / 'node')
        try:
            node.start()
            with lab.ElectrumPeer(node, tls, root) as peer:
                wallet = json.loads(original)
                wallet['cacheDirectory'] = str(root / 'cache')
                wallet['wallet']['spendDirectory'] = str(root / 'spends')
                wallet['endpoint'] = {'host': 'localhost', 'port': peer.port}
                lab.write_private(wallet_path, wallet)
                if qt_probe:
                    wallet_path.write_text(json.dumps(wallet, sort_keys=True, separators=(',', ':')))
                context = {'schema': 'monzero-bch-light-dex-context-v1', 'dexDigest': fingerprint(dex_path),
                           'walletDigest': fingerprint(wallet_path), 'quoteDigest': fingerprint(dex['quoteFile'])}
                digest = hashlib.sha256(json.dumps(context, sort_keys=True, separators=(',', ':')).encode()).hexdigest()
                env = os.environ.copy()
                env['SSL_CERT_FILE'] = str(tls / 'root.pem')
                launcher = root / 'launcher.json'
                if qt_probe:
                    lab.write_private(launcher, {'schema': 'monzero-private-bch-light-launcher-v1',
                        'helper': str(helper), 'helperDigest': fingerprint(helper), 'configFile': str(wallet_path)})
                    launcher.write_text(json.dumps(json.loads(launcher.read_bytes()), sort_keys=True, separators=(',', ':')))

                if qt_engine:
                    admission_helper = Path(os.environ['MONZERO_BCH_ADMISSION_HELPER']).resolve()
                    assert fingerprint(admission_helper) == os.environ['MONZERO_BCH_ADMISSION_HELPER_SHA256']
                    admission = dict(dex)
                    admission.pop('walletConfigFile')
                    admission.update(schema='monzero-private-bch-admission-v1', role='bch-funder',
                                     helper=str(admission_helper), helperDigest=fingerprint(admission_helper))
                    admission_path = root / 'admission.json'
                    lab.write_private(admission_path, admission)
                    admission_path.write_text(json.dumps(admission, sort_keys=True, separators=(',', ':')))
                    env['MONZERO_BCH_ADMISSION_CONFIG'] = str(admission_path)

                def call(mode, payment=None, retained=None, succeeds=True, approval=None, password=None, kill_after_accept=False, raw_helper=False):
                    request = {'schema': f'monzero-bch-light-dex-funding-{mode}-request-v1',
                               'requestId': str(uuid.uuid4()), 'approvedConfigDigest': digest}
                    if payment is not None:
                        request.update(payment=payment, maximumDownloadHeaders=1000)
                        if mode in ('observe', 'retry-review', 'retry', 'reconcile-ack'):
                            request['maximumHistoryEntries'] = 10
                        else:
                            request['walletScan'] = {'gap': 2, 'addressesPerBranch': 3, 'historyEntriesPerBranch': 50}
                    if retained is not None:
                        request['retainedReview'] = retained
                    if approval is not None:
                        request['approval'] = approval
                    if password is not None:
                        request['password'] = password
                    command = [str(helper), f'--bch-dex-funding-{mode}-stdio-v1', str(dex_path)]
                    if qt_probe and not raw_helper:
                        assert not kill_after_accept or qt_engine, 'Only the owned DexEngine fixture supports Qt process-group kill'
                        command = [str(qt_probe)] + (['--bch-light-engine-probe'] if qt_engine else []) + [str(dex_path), str(helper), fingerprint(helper), mode, str(launcher)]
                    if kill_after_accept:
                        peer.hold_next_broadcast_reply = True
                        child = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=env, start_new_session=qt_engine)
                        try:
                            child.stdin.write(json.dumps(request).encode())
                            child.stdin.close()
                            child.stdin = None
                            assert peer.broadcast_held.wait(30), 'helper did not reach owned broadcast barrier'
                            assert child.poll() is None
                            if qt_engine:
                                assert os.getpgid(child.pid) == child.pid and child.pid != os.getpgrp()
                                # This session contains only our disposable Qt process
                                # and its helper, never the node or TLS peer.
                                os.killpg(child.pid, signal.SIGKILL)
                            else:
                                child.kill()
                            stdout, stderr = child.communicate(timeout=20)
                            assert child.returncode == -9
                            result = subprocess.CompletedProcess(command, child.returncode, stdout, stderr)
                        finally:
                            if qt_engine and child.poll() is None:
                                try:
                                    os.killpg(child.pid, signal.SIGKILL)
                                except ProcessLookupError:
                                    pass
                            if child.poll() is None:
                                child.kill()
                                child.communicate(timeout=20)
                            peer.release_broadcast_reply.set()
                    else:
                        result = subprocess.run(command, input=json.dumps(request).encode(), env=env, capture_output=True, timeout=120)
                    assert (result.returncode == 0) == succeeds, (mode, succeeds, result.returncode, result.stderr)
                    if not succeeds:
                        assert not result.stdout
                        return None
                    assert b'disposable BCH signing fixture password' not in result.stdout
                    value = json.loads(result.stdout)
                    assert value['configurationDigest'] == digest
                    if qt_controller:
                        assert str(uuid.UUID(value['requestId'])) == value['requestId']
                    else:
                        assert value['requestId'] == request['requestId']
                    assert value['executable'] is False and value['schema'] == f'monzero-bch-light-dex-funding-{mode}-response-v1'
                    return value['result']

                terms = call('terms')
                payment = {'address': terms['contractAddress'], 'amountAtomic': terms['amountAtomic'],
                           'feeAtomic': '1000', 'minimumChangeAtomic': terms['minimumChangeAtomic'],
                           'minimumConfirmations': terms['lockConfirmations']}
                watched = [Path(wallet['wallet']['addressBookDirectory']), root / 'spends',
                           Path(dex['metadataDirectory']), Path(dex['snapshotDirectory'])]
                before = records(watched)
                call('prepare', payment, succeeds=False, raw_helper=True)
                assert records(watched) == before and not peer.requests
                conflict = Path(dex['metadataDirectory']) / f"{dex['tradeId']}.bch-funding-wallet-lock-intent"
                assert not conflict.exists()
                conflict.write_bytes(b'owned conflicting node fixture')
                conflict.chmod(0o600)
                call('source-select', succeeds=False)
                conflict.unlink()
                assert records(watched) == before
                selected = call('source-select')
                assert selected['walletSourceSelected'] is True and selected['broadcastAuthorized'] is False
                after_selection = records(watched)
                added = set(after_selection) - set(before)
                assert added == {str(Path(dex['metadataDirectory']) / f"{dex['tradeId']}.bch-funding-light-source")}
                call('source-select')
                assert records(watched) == after_selection and not peer.requests
                other_spends = root / 'other-spends'
                other_spends.mkdir(mode=0o700)
                saved_wallet = wallet_path.read_bytes()
                changed_wallet = copy.deepcopy(wallet)
                changed_wallet['wallet']['spendDirectory'] = str(other_spends)
                wallet_path.write_text(json.dumps(changed_wallet, sort_keys=True, separators=(',', ':')))
                saved_digest = digest
                changed_context = dict(context, walletDigest=fingerprint(wallet_path))
                digest = hashlib.sha256(json.dumps(changed_context, sort_keys=True, separators=(',', ':')).encode()).hexdigest()
                call('source-select', succeeds=False)
                call('prepare', payment, succeeds=False, raw_helper=True)
                wallet_path.write_bytes(saved_wallet)
                digest = saved_digest
                assert records(watched) == after_selection and not peer.requests and not list(other_spends.iterdir())
                before = after_selection
                bad = copy.deepcopy(payment)
                bad['minimumConfirmations'] = 1
                call('prepare', bad, succeeds=False)
                bad = copy.deepcopy(payment)
                bad['amountAtomic'] = str(int(payment['amountAtomic']) + 1)
                call('prepare', bad, succeeds=False)
                assert not peer.requests and records(watched) == before
                anchor = node.rpc('getnewaddress')
                node.mine(101, anchor)
                credit = node.rpc('sendtoaddress', terms['refundAddress'], '0.01')
                node.mine(1, anchor)
                call('prepare', payment, succeeds=False)
                if qt_probe:
                    after_shallow = records(watched)
                    added = set(after_shallow) - set(before)
                    assert len(added) == 1
                    intent = Path(added.pop())
                    assert intent.name == f'bch-light-dex-ui-{dex["tradeId"]}-payment.json'
                    assert json.loads(intent.read_bytes())['payment'] == payment
                    assert all(after_shallow[k] == v for k, v in before.items())
                    before = after_shallow
                assert records(watched) == before
                node.mine(1, anchor)
                prepared = call('prepare', payment)
                assert prepared['paymentAddress'] == terms['contractAddress']
                assert prepared['paymentAtomic'] == payment['amountAtomic'] and prepared['feeAtomic'] == '1000'
                assert prepared['changeAtomic'] == str(1_000_000 - int(payment['amountAtomic']) - 1000)
                assert prepared['fundingVerified'] is True and prepared['sourceTransactions'] == 1
                assert prepared['tradeId'] == dex['tradeId'] and prepared['unsignedTxid']
                assert 'txid' not in prepared and prepared['signed'] is False and prepared['broadcastAuthorized'] is False
                retained = {'signingCommitment': prepared['signingCommitment'], 'changeAddress': prepared['changeAddress']}
                after = records(watched)
                reviewed = call('review', payment, retained)
                repeated = call('prepare', payment)
                assert reviewed['signingCommitment'] == repeated['signingCommitment'] == prepared['signingCommitment']
                assert reviewed['unsignedTxid'] == repeated['unsignedTxid'] == prepared['unsignedTxid']
                bad = dict(retained, signingCommitment='00' * 32)
                call('review', payment, bad, succeeds=False)
                assert records(watched) == after
                original_answer = peer.answer
                raw_reads = [0]

                def corrupted_source(method, params):
                    if method == 'blockchain.transaction.get':
                        raw_reads[0] += 1
                        # First fetch supplies the verified scan. The second is
                        # the source export; substitute a different valid transaction.
                        if raw_reads[0] == 2:
                            other = node.rpc('getblock', node.rpc('getblockhash', 1))['tx'][0]
                            return node.rpc('getrawtransaction', other, False)
                    return original_answer(method, params)

                peer.answer = corrupted_source
                try:
                    call('review', payment, retained, succeeds=False)
                    assert raw_reads[0] == 2
                finally:
                    peer.answer = original_answer
                assert records(watched) == after
                assert call('review', payment, retained)['fundingVerified'] is True
                assert not peer.accepted_broadcast_digests and 'blockchain.transaction.broadcast' not in peer.requests
                assert not node.rpc('getrawmempool')
                assert not any('signed' in p.name or 'signing' in p.name for p in (root / 'spends').iterdir())
                signed_txid = None
                signing_format = None
                if wallet['wallet'].get('vault') is not None:
                    password = 'disposable BCH signing fixture password'
                    signing_format = wallet['wallet']['vault']['seedEncoding']
                    watched.append(Path(wallet['wallet']['vault']['directory']))
                    before_sign = records(watched)
                    call('sign', payment, retained, succeeds=False, approval='00' * 32, password=password)
                    call('sign', payment, retained, succeeds=False, approval=retained['signingCommitment'], password='wrong fixture password')
                    changed = copy.deepcopy(payment)
                    changed['feeAtomic'] = '1001'
                    call('sign', changed, retained, succeeds=False, approval=retained['signingCommitment'], password=password)
                    assert records(watched) == before_sign
                    signed = call('sign', payment, retained, approval=retained['signingCommitment'], password=password)
                    signed_txid = signed['txid']
                    assert signed['signed'] is True and signed['tradeFundingRetained'] is True
                    assert signed['broadcast'] is False and signed['broadcastAuthorized'] is False
                    assert signed['unsignedTxid'] == prepared['unsignedTxid'] and signed_txid != signed['unsignedTxid']
                    signed_files = list((root / 'spends').glob('ltc-signed-*.tx'))
                    assert len(signed_files) == 1
                    raw = signed_files[0].read_bytes()
                    decoded = node.rpc('decoderawtransaction', raw.hex())
                    assert decoded['txid'] == signed_txid
                    assert decoded['vout'][0]['scriptPubKey']['hex'] == terms['contractScript']
                    admission = node.rpc('testmempoolaccept', [raw.hex()])
                    assert len(admission) == 1 and admission[0]['allowed'] is True, admission
                    after_sign = records(watched)
                    again = call('sign', payment, retained, approval=retained['signingCommitment'], password=password)
                    assert again['txid'] == signed_txid and signed_files[0].read_bytes() == raw
                    assert records(watched) == after_sign
                    assert not node.rpc('getrawmempool')
                    assert not peer.accepted_broadcast_digests
                    assert 'blockchain.transaction.broadcast' not in peer.requests
                if send_mode:
                    assert signed_txid is not None
                    call('send-review', payment, retained, succeeds=False)
                    if retry_mode:
                        call('retry-review', payment, retained, succeeds=False)
                    call('send', payment, retained, succeeds=False, approval='00' * 32)
                    assert records(watched) == after_sign and not node.rpc('getrawmempool')
                    recovery = call('recovery-review')
                    call('recovery-retain', succeeds=False, approval='00' * 32)
                    assert records(watched) == after_sign
                    saved = call('recovery-retain', approval=recovery['review']['commitment'])
                    assert saved['privateRecoveryRetained'] is True
                    assert saved['broadcastAuthorized'] is False
                    after_recovery = records(watched)
                    review = call('send-review', payment, retained)
                    assert review['review']['details']['txid'] == signed_txid
                    call('send', payment, retained, succeeds=False, approval='00' * 32)
                    call('send', payment, retained, succeeds=False, approval=review['review']['commitment'], password=password)
                    assert records(watched) == after_recovery and not node.rpc('getrawmempool')
                    if retry_mode:
                        peer.drop_next_broadcast_reply = not kill_mode
                        call('send', payment, retained, succeeds=False, approval=review['review']['commitment'], kill_after_accept=kill_mode)
                        if not kill_mode:
                            assert peer.dropped_broadcast_replies == 1
                        assert node.rpc('getrawmempool') == [signed_txid]
                        uncertain = call('observe', payment, retained)
                        assert uncertain['observation']['position'] == 'reported-pending'
                        assert uncertain['broadcastStatus'] == 'outcome-unknown'
                        before_retry = records(watched)
                        call('reconcile-ack', payment, retained, succeeds=False)
                        call('send', payment, retained, succeeds=False, approval=review['review']['commitment'])
                        retry = call('retry-review', payment, retained)
                        assert retry['review']['details']['txid'] == signed_txid
                        call('retry', payment, retained, succeeds=False, approval=review['review']['commitment'])
                        call('retry', payment, retained, succeeds=False, approval=retry['review']['commitment'], password=password)
                        assert records(watched) == before_retry
                        sent = call('retry', payment, retained, approval=retry['review']['commitment'])
                        assert len(set(peer.accepted_broadcast_digests)) == 1
                    else:
                        sent = call('send', payment, retained, approval=review['review']['commitment'])
                    assert sent['txid'] == signed_txid and sent['broadcast'] is True
                    assert sent['broadcastStatus'] == 'server-acknowledged'
                    assert node.rpc('getrawmempool') == [signed_txid]
                    assert len(peer.accepted_broadcast_digests) == (2 if retry_mode else 1)
                    after_send = records(watched)
                    call('send', payment, retained, succeeds=False, approval=review['review']['commitment'])
                    assert records(watched) == after_send
                    pending = call('observe', payment, retained)
                    assert pending['observation']['position'] == 'reported-pending'
                    assert pending['broadcastStatus'] == 'server-acknowledged'
                    node.mine(1, anchor)
                    confirmed = call('observe', payment, retained)
                    assert confirmed['observation']['position'] == 'included'
                    assert confirmed['broadcastStatus'] == 'server-acknowledged'
                    assert records(watched) == after_send
                    if retry_mode:
                        call('retry-review', payment, retained, succeeds=False)
                        requests_before_ack = len(peer.requests)
                        ack = call('reconcile-ack', payment, retained)
                        assert len(peer.requests) == requests_before_ack
                        assert ack['tradeAcknowledgementRetained'] is True
                        assert ack['txid'] == signed_txid and ack['broadcast'] is False
                        assert records(watched) == after_send
                if observe_mode:
                    assert signed_txid is not None
                    unseen = call('observe', payment, retained)
                    assert unseen['observation']['position'] == 'not-reported'
                    call('observe', payment, retained, succeeds=False, password=password)
                    assert unseen['broadcastStatus'] == 'no-local-attempt'
                    assert records(watched) == after_sign
                    # The fixture publishes directly solely to exercise observation;
                    # helper relay/private swap recovery are NOT validated here.
                    assert node.rpc('sendrawtransaction', raw.hex()) == signed_txid
                    pending = call('observe', payment, retained)
                    assert pending['observation']['position'] == 'reported-pending'
                    assert pending['broadcastStatus'] == 'no-local-attempt'
                    node.mine(1, anchor)
                    included = call('observe', payment, retained)
                    assert included['observation']['position'] == 'included'
                    assert included['observation']['confirmations'] == 1
                    assert included['txid'] == signed_txid and included['broadcastAuthorized'] is False
                    assert included['broadcastStatus'] == 'no-local-attempt'
                    assert records(watched) == after_sign
                    deadline = time.monotonic() + 75
                    while time.time() <= int(terms['expiresAt']) + 1:
                        assert time.monotonic() < deadline, 'quote-expiry test wait exceeded bound'
                        time.sleep(1)
                    expired = call('observe', payment, retained)
                    assert expired['observation']['position'] == 'included' and expired['txid'] == signed_txid
                    call('review', payment, retained, succeeds=False)
                    call('sign', payment, retained, succeeds=False, approval=retained['signingCommitment'], password=password)
                    assert records(watched) == after_sign
                    assert not peer.accepted_broadcast_digests
                    assert 'blockchain.transaction.broadcast' not in peer.requests
                report.write_text(json.dumps({'status': 'passed', 'fixture': 'actual-bch-light-dex-send-v1' if send_mode else ('actual-bch-light-dex-observe-v1' if observe_mode else ('actual-bch-light-dex-sign-v1' if signed_txid else 'actual-bch-light-dex-prepare-v1')),
                    'helperSha256': fingerprint(helper), 'qtProbeSha256': fingerprint(qt_probe) if qt_probe else None, 'qtController': qt_controller, 'creditTxid': credit, 'preparedUnsignedTxid': prepared['unsignedTxid'],
                    'earlyWalletSourceSelected': True, 'changedWalletAndMixedNodeSourceRejected': True, 'exactReviewAndRetry': True, 'tradeMinimumConfirmationsEnforced': True,
                    'verifiedFundingSources': True, 'mismatchedSourceRejected': True,
                    'signed': signed_txid is not None, 'signedTxid': signed_txid, 'signingFormat': signing_format,
                    'signatureAcceptedByNode': signed_txid is not None, 'broadcast': observe_mode or send_mode,
                    'broadcastBy': 'helper' if send_mode else ('test-fixture' if observe_mode else None), 'helperBroadcast': send_mode, 'droppedReplyRecoveredWithExactBytes': retry_mode and not kill_mode, 'killedHelperRecoveredWithExactBytes': kill_mode, 'killedDesktopProcessGroupRecoveredWithExactBytes': kill_mode and qt_engine, 'desktopEngine': qt_engine,
                    'recoveryAndApprovalRequired': send_mode, 'coldAcknowledgementAndPeerMessage': send_mode,
                    'absentPendingIncludedAndExpiredObservation': observe_mode, 'fullSwapAcceptance': False}, indent=2) + '\n')
        finally:
            wallet_path.write_bytes(original)
            node.stop()
    print('PASS: BCH DEX helper verified exact funding and optional local signing on real confirmed wallet inputs')


if __name__ == '__main__':
    run()
