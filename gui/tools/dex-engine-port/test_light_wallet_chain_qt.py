#!/usr/bin/env python3
"""Run the actual-chain payment fixture through the production Qt light job.

Usage: CHAIN QT_PROBE HELPER NODE REPORT. Regtest only; disposable local wallets.
The reused fixture owns and stops its node. The Qt worker enforces config pins,
review binding, separate signing/broadcast approvals and response validation.
"""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import test_light_wallet_chain as fixture


def write(path, value):
    path.write_text(json.dumps(value, sort_keys=True, separators=(',', ':')))
    path.chmod(0o600)


class QtHelper:
    def __init__(self, program, tls):
        self.program, self.fixture = program, tls
        self.reviews = {}

    def call(self, mode, config, extra=None, scan=True):
        # The canonical JSON form is required by the app's private config reader.
        settings = json.loads(config.read_text())
        if mode == 'wallet-create' and os.environ.get('MONZERO_TEST_LIGHT_SEED_ENCODING') == 'bip39-english-v1':
            settings['seedEncoding'] = 'bip39-english-v1'
        write(config, settings)
        wrapper = config.parent / 'qt-launcher.json'
        write(wrapper, {'schema': f'monzero-private-{fixture.CHAIN}-light-launcher-v1',
                        'helper': str(self.program), 'helperDigest': fixture.digest(self.program),
                        'configFile': str(config)})
        extra = dict(extra or {})
        request = extra.get('requestId')
        password = fixture.PASSWORD if mode in ['wallet-create', 'wallet-sign'] else ''
        budget = extra.get('maximumDownloadHeaders', 3)
        if mode == 'wallet-create':
            extra = {'approvedConfigDigest': fixture.digest(config)}
        elif mode in ['wallet-broadcast', 'wallet-reconcile']:
            extra['savedReview'] = self.reviews[request]
            if mode == 'wallet-broadcast':
                approval = extra['broadcastApproval']
                extra['approvedTxid'] = approval['approvedTxid']
                extra['approvedCommitment'] = approval['approvedCommitment']
                extra['approvedEndpoint'] = {'host': approval['host'], 'port': approval['port']}
        result = subprocess.run([str(PROBE), str(wrapper), mode, str(budget), json.dumps(extra)],
                                input=password.encode(), capture_output=True, timeout=120,
                                env=dict(os.environ, MONZERO_TEST_LIGHT_COIN=fixture.CHAIN,
                                         SSL_CERT_FILE=str(self.fixture / 'root.pem')))
        assert result.returncode == 0, (fixture.CHAIN, mode, result.returncode, result.stderr.decode())
        value = json.loads(result.stdout)
        assert value['state'] != 'unknown', (fixture.CHAIN, mode, value)
        assert value['coin'] == fixture.CHAIN and not value['executable'] and not value['busy']
        if mode == 'wallet-prepare': self.reviews[request] = value['wallet']
        return value


if __name__ == '__main__':
    if len(sys.argv) != 6: raise SystemExit(__doc__)
    chain, probe, helper, node, report = sys.argv[1:]
    PROBE = Path(probe).resolve(strict=True)
    source = Path(helper).resolve(strict=True)
    probe_hash, helper_hash = fixture.digest(PROBE), fixture.digest(source)
    with tempfile.TemporaryDirectory(prefix='monzero-qt-helper-') as temp:
        installed = Path(temp).resolve() / 'native-light'
        shutil.copyfile(source, installed); installed.chmod(0o500)
        assert fixture.digest(installed) == helper_hash
        fixture.Helper = QtHelper
        sys.argv = [sys.argv[0], chain, str(installed), node, report]
        fixture.run()
    assert fixture.digest(PROBE) == probe_hash and fixture.digest(source) == helper_hash
    path = Path(report)
    result = json.loads(path.read_text())
    result.update(seedEncoding=os.environ.get("MONZERO_TEST_LIGHT_SEED_ENCODING", "raw-bip32-seed-hex-v1"), qtWorkerVerified=True, qtProbeSha256=probe_hash,
                  qtTestSha256=fixture.digest(Path(__file__)), liveTrading=False)
    write(path, result)
