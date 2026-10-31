"""Fake-helper tests for light_wallet_provision.py. Standard library only."""
import hashlib
import json
import os
import stat
import subprocess
import sys
import tempfile
import time
import unittest
from pathlib import Path
from unittest import mock

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import light_wallet_provision as lwp  # noqa: E402

SEED = 'abandon ability able about above absent absorb abstract absurd abuse access accident'
XPUB = 'tpubFAKEPUBLICACCOUNTDESCRIPTOR'
ADDRESS = 'rltc1qfakeaddress00000000000000000000000'

FAKE_HELPER = r'''
import hashlib, json, os, sys, time
SEED = %(seed)r
mode = sys.argv[1][2:-len('-stdio-v1')]
config = sys.argv[2]
request = json.loads(sys.stdin.read())
behavior = os.environ.get('FAKE_BEHAVIOR', '') if os.environ.get('FAKE_TARGET') == mode else ''
with open(os.environ['FAKE_LOG'], 'a') as log:
    log.write(json.dumps(dict(mode=mode, config=config, cert=os.environ.get('SSL_CERT_FILE'),
                              fields=sorted(request))) + '\n')
assert request['schema'] == 'monzero-ltc-light-%%s-request-v1' %% mode and request['requestId']
assert os.path.isfile(os.environ['SSL_CERT_FILE'])
password = request.get('createApproval', {}).get('password', '')
sys.stderr.write('debug seed=%%s\n' %% SEED)
if behavior == 'echo-password':
    sys.stderr.write('debug password=%%s\n' %% password)
if behavior == 'fail':
    sys.stderr.write('panic: ' + SEED)
    sys.stdout.write('partial ' + SEED)
    sys.exit(3)
if behavior == 'hang':
    time.sleep(60)
if behavior == 'flood':
    sys.stdout.write('x' * (4 << 20))
    sys.exit(0)
if behavior == 'garbage':
    sys.stdout.write('not json ' + SEED)
    sys.exit(0)
wallet = {}
if mode == 'wallet-create':
    with open(config, 'rb') as source:
        raw = source.read()
    assert request['createApproval']['approvedConfigDigest'] == hashlib.sha256(raw).hexdigest()
    setup = json.loads(raw)
    target = os.path.join(setup['setupDirectory'], 'litecoin-wallet.json')
    if behavior == 'escape':
        target = os.environ['FAKE_OUTSIDE']
    elif behavior == 'symlink':
        os.symlink(os.environ['FAKE_OUTSIDE'], target)
    else:
        fd = os.open(target, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
        os.write(fd, json.dumps(dict(schema='monzero-private-ltc-light-v1', network=setup['network'],
            endpoint=setup['endpoint'], account=%(xpub)r)).encode())
        os.close(fd)
    wallet = dict(created=True, configurationFile=target, mnemonic=SEED, walletId=setup['walletId'])
    if behavior == 'echo-password':
        wallet['password'] = password
elif mode == 'wallet-receive':
    assert os.path.basename(config) == 'litecoin-wallet.json'
    wallet = dict(address=%(address)r, derivationPath="m/84'/1'/0'/0/0", seed=SEED, account=%(xpub)r)
elif mode == 'sync':
    assert request['maximumDownloadHeaders'] >= 1
elif mode == 'wallet-refresh':
    assert request['walletScan'] == dict(gap=2, addressesPerBranch=3, historyEntriesPerBranch=50)
    wallet = dict(observedConfirmedAtomic='200000000', pendingReceivedAtomic='5000', immatureAtomic='0',
                  confirmedCoinCount=1, account=%(xpub)r, seed=SEED)
response = dict(schema='monzero-ltc-light-%%s-response-v1' %% mode, requestId=request['requestId'],
                executable=False, wallet=wallet, chain=dict(height=101))
if behavior == 'schema':
    response['schema'] = 'monzero-ltc-light-other-response-v1'
elif behavior == 'request-id':
    response['requestId'] = '00000000-0000-4000-8000-000000000000'
elif behavior == 'executable':
    response['executable'] = True
elif behavior == 'malformed-address':
    wallet['address'] = 'bad address; ' + SEED
elif behavior == 'malformed-balance':
    wallet['observedConfirmedAtomic'] = 200000000
sys.stdout.write(json.dumps(response))
''' % dict(seed=SEED, xpub=XPUB, address=ADDRESS)


class ProvisionTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name).resolve()
        self.helper = self.root / 'native-ltc-light'
        self.helper.write_text(f'#!{sys.executable} -I\n' + FAKE_HELPER)
        self.helper.chmod(0o700)
        self.tls = self.root / 'tls'
        self.tls.mkdir()
        (self.tls / 'root.pem').write_text('-----BEGIN CERTIFICATE-----\n')
        self.log = self.root / 'helper.log'
        self.outside = self.root / 'outside.json'
        self.outside.write_text('{}')
        self.outside.chmod(0o600)
        self.wallet = self.root / 'wallet'
        self.env = mock.patch.dict(os.environ, FAKE_LOG=str(self.log), FAKE_OUTSIDE=str(self.outside))
        self.env.start()
        self.addCleanup(self.env.stop)
        self.addCleanup(self.temporary.cleanup)

    def behave(self, target, behavior):
        patch = mock.patch.dict(os.environ, FAKE_TARGET=target, FAKE_BEHAVIOR=behavior)
        patch.start()
        self.addCleanup(patch.stop)

    def calls(self):
        if not self.log.exists():
            return []
        return [json.loads(line) for line in self.log.read_text().splitlines()]

    def create(self, **overrides):
        values = dict(helper=self.helper, directory=self.wallet, network='regtest',
                      maximum_height=10000, port=50002, tls_root=self.tls)
        values.update(overrides)
        return lwp.provision(**values)

    def password(self):
        return (self.wallet / 'light-password').read_text()

    def assert_clean(self, text):
        self.assertNotIn(SEED, text)
        self.assertNotIn('abandon', text)
        self.assertNotIn(XPUB, text)
        if (self.wallet / 'light-password').exists():
            self.assertNotIn(self.password(), text)

    def assert_refused(self, message, call, *args, **kwargs):
        with self.assertRaises(lwp.ProvisionError) as caught:
            call(*args, **kwargs)
        self.assertIn(message, str(caught.exception))
        self.assert_clean(str(caught.exception))
        self.assertIsNone(caught.exception.__cause__)
        return caught.exception

    def mode(self, path):
        return stat.S_IMODE(os.lstat(path).st_mode)

    # creation

    def test_create_returns_only_public_fields(self):
        result = self.create()
        digest = hashlib.sha256(self.helper.read_bytes()).hexdigest()
        config = str(self.wallet / 'setupDirectory' / 'litecoin-wallet.json')
        self.assertEqual(result, dict(address=ADDRESS, helperSha256=digest, configurationFile=config,
                                      launcher=str(self.wallet / 'launcher.json')))
        self.assert_clean(json.dumps(result))
        self.assertEqual([call['mode'] for call in self.calls()], ['wallet-create', 'wallet-receive'])
        self.assertTrue(all(call['cert'] == str(self.tls / 'root.pem') for call in self.calls()))
        self.assertEqual(self.calls()[0]['fields'], ['createApproval', 'requestId', 'schema'])

    def test_create_files_are_private_and_match_fixture_schema(self):
        result = self.create(network='test', maximum_height=1234, port=443)
        self.assertEqual(self.mode(self.wallet), 0o700)
        for key in lwp.STORAGE_KEYS:
            self.assertEqual(self.mode(self.wallet / key), 0o700)
        for name in ('light-password', 'setup.json', 'launcher.json'):
            self.assertEqual(self.mode(self.wallet / name), 0o600)
        self.assertTrue(self.password().startswith('disposable-'))
        self.assertGreaterEqual(len(self.password()), 40)
        setup = json.loads((self.wallet / 'setup.json').read_text())
        self.assertEqual(setup['schema'], 'monzero-private-ltc-setup-v1')
        self.assertEqual((setup['network'], setup['maximumHeight'], setup['accountIndex']), ('test', 1234, 0))
        self.assertEqual(setup['seedEncoding'], 'bip39-english-v1')
        self.assertEqual(setup['endpoint'], dict(host='localhost', port=443))
        self.assertEqual({key: setup[key] for key in lwp.STORAGE_KEYS},
                         {key: str(self.wallet / key) for key in lwp.STORAGE_KEYS})
        self.assertNotIn(self.password(), json.dumps(setup))
        raw = (self.wallet / 'launcher.json').read_text()
        launcher = json.loads(raw)
        self.assertEqual(list(launcher), ['configFile', 'helper', 'helperDigest', 'schema'])
        self.assertEqual(launcher, dict(schema='monzero-private-ltc-light-launcher-v1',
            configFile=result['configurationFile'], helper=str(self.helper), helperDigest=result['helperSha256']))
        self.assertNotIn(self.password(), raw)

    def test_create_rejects_mainnet_and_bad_parameters_before_helper(self):
        for network in ('main', 'mainnet', 'Regtest', '', None):
            self.assert_refused('not a test network', self.create, network=network)
        # Each coin accepts only its own test network names.
        for coin, network in (('btc', 'test'), ('btc', 'main'), ('ltc', 'signet'), ('ltc', 'testnet4')):
            self.assert_refused('not a test network', self.create, network=network, coin=coin)
        self.assert_refused('Unsupported coin', self.create, coin='doge')
        for height in (0, -1, 10_000_001, True, '10'):
            self.assert_refused('maximum height', self.create, maximum_height=height)
        for port in (0, 65536, True, '50002'):
            self.assert_refused('port', self.create, port=port)
        self.assertFalse(self.wallet.exists())
        self.assertEqual(self.calls(), [])

    def test_create_refuses_existing_directory_and_missing_inputs(self):
        self.wallet.mkdir()
        self.assert_refused('already exists', self.create)
        self.wallet.rmdir()
        self.assert_refused('not found', self.create, helper=self.root / 'missing')
        self.assert_refused('TLS root', self.create, tls_root=self.root / 'missing')
        self.helper.chmod(0o600)
        self.assert_refused('not an executable', self.create)
        self.helper.chmod(0o777)
        self.assert_refused('writable', self.create)
        self.assertEqual(self.calls(), [])

    def test_create_refuses_response_context_mismatches(self):
        cases = [('schema', 'context mismatch'), ('request-id', 'context mismatch'),
                 ('executable', 'context mismatch'), ('garbage', 'malformed output'),
                 ('fail', 'Helper failed: wallet-create'), ('echo-password', 'Secret in helper output')]
        for target in ('wallet-create', 'wallet-receive'):
            for behavior, message in cases:
                if target == 'wallet-receive' and behavior == 'echo-password':
                    continue
                with self.subTest(target=target, behavior=behavior):
                    self.behave(target, behavior)
                    self.assert_refused(message.replace('wallet-create', target), self.create)
                    self.assertFalse((self.wallet / 'launcher.json').exists())
                    os.chmod(self.wallet, 0o700)
                    subprocess.run(['rm', '-rf', str(self.wallet)], check=True)

    def test_create_refuses_malformed_address(self):
        self.behave('wallet-receive', 'malformed-address')
        self.assert_refused('malformed address', self.create)
        self.assertFalse((self.wallet / 'launcher.json').exists())

    def test_create_refuses_configuration_outside_directory(self):
        for behavior in ('escape', 'symlink'):
            with self.subTest(behavior=behavior):
                self.behave('wallet-create', behavior)
                self.assert_refused('outside wallet directory', self.create)
                self.assertEqual([call['mode'] for call in self.calls()], ['wallet-create'])
                self.log.unlink()
                subprocess.run(['rm', '-rf', str(self.wallet)], check=True)

    def test_output_limit_and_timeout(self):
        self.behave('wallet-create', 'flood')
        self.assert_refused('exceeded limit', self.create, limit=1 << 16)
        subprocess.run(['rm', '-rf', str(self.wallet)], check=True)
        self.behave('wallet-create', 'hang')
        started = time.monotonic()
        self.assert_refused('timed out', self.create, timeout=1)
        self.assertLess(time.monotonic() - started, 10)

    # refresh

    def test_refresh_syncs_then_returns_only_balances(self):
        launcher = self.create()['launcher']
        self.log.unlink()
        result = lwp.refresh(self.helper, launcher, self.tls / 'root.pem', maximum_download_headers=500)
        self.assertEqual(result, dict(observedConfirmedAtomic='200000000', pendingReceivedAtomic='5000'))
        calls = self.calls()
        self.assertEqual([call['mode'] for call in calls], ['sync', 'wallet-refresh'])
        config = str(self.wallet / 'setupDirectory' / 'litecoin-wallet.json')
        self.assertTrue(all(call['config'] == config for call in calls))
        self.assertEqual(calls[0]['fields'], ['maximumDownloadHeaders', 'requestId', 'schema'])
        self.assertEqual(calls[1]['fields'], ['requestId', 'schema', 'walletScan'])

    def test_refresh_refuses_changed_helper(self):
        launcher = self.create()['launcher']
        self.log.unlink()
        self.helper.write_text(self.helper.read_text() + '\n# changed\n')
        self.assert_refused('digest does not match', lwp.refresh, self.helper, launcher, self.tls)
        self.assertEqual(self.calls(), [])

    def test_refresh_refuses_tampered_or_exposed_launcher(self):
        launcher = Path(self.create()['launcher'])
        self.log.unlink()
        original = json.loads(launcher.read_text())
        variants = [dict(original, configFile=str(self.outside)),
                    dict(original, configFile='setupDirectory/litecoin-wallet.json'),
                    dict(original, schema='monzero-private-ltc-light-launcher-v2'),
                    dict(original, extra=True), dict(original, helperDigest='zz')]
        for variant in variants:
            launcher.write_text(json.dumps(variant))
            with self.assertRaises(lwp.ProvisionError):
                lwp.refresh(self.helper, launcher, self.tls)
        launcher.write_text(json.dumps(original))
        launcher.chmod(0o644)
        self.assert_refused('private regular file', lwp.refresh, self.helper, launcher, self.tls)
        self.assertEqual(self.calls(), [])

    def test_refresh_refuses_response_mismatches(self):
        launcher = self.create()['launcher']
        for target in ('sync', 'wallet-refresh'):
            for behavior, message in (('schema', 'context mismatch'), ('request-id', 'context mismatch'),
                                      ('executable', 'context mismatch'), ('fail', 'Helper failed')):
                with self.subTest(target=target, behavior=behavior):
                    self.behave(target, behavior)
                    self.log.unlink(missing_ok=True)
                    self.assert_refused(message, lwp.refresh, self.helper, launcher, self.tls)
                    modes = [call['mode'] for call in self.calls()]
                    self.assertEqual(modes, ['sync'] if target == 'sync' else ['sync', 'wallet-refresh'])
        self.behave('wallet-refresh', 'malformed-balance')
        self.assert_refused('balance', lwp.refresh, self.helper, launcher, self.tls)

    # command line

    def cli(self, *args):
        return subprocess.run([sys.executable, '-I', str(HERE / 'light_wallet_provision.py'), *map(str, args)],
                              capture_output=True, text=True, timeout=60, env=dict(os.environ))

    def test_cli_create_and_refresh_print_filtered_json(self):
        created = self.cli('create', '--helper', self.helper, '--directory', self.wallet, '--network', 'regtest',
                           '--maximum-height', 10000, '--port', 50002, '--tls-root', self.tls)
        self.assertEqual(created.returncode, 0)
        self.assertEqual(created.stderr, '')
        result = json.loads(created.stdout)
        self.assertEqual(sorted(result), ['address', 'configurationFile', 'helperSha256', 'launcher'])
        self.assert_clean(created.stdout)
        refreshed = self.cli('refresh', '--helper', self.helper, '--launcher', result['launcher'],
                             '--tls-root', self.tls)
        self.assertEqual(refreshed.returncode, 0)
        self.assertEqual(json.loads(refreshed.stdout),
                         dict(observedConfirmedAtomic='200000000', pendingReceivedAtomic='5000'))
        self.assertEqual(refreshed.stderr, '')
        self.assert_clean(refreshed.stdout)

    def test_cli_redacts_helper_failure(self):
        os.environ.update(FAKE_TARGET='wallet-create', FAKE_BEHAVIOR='fail')
        failed = self.cli('create', '--helper', self.helper, '--directory', self.wallet, '--network', 'test',
                          '--maximum-height', 10000, '--port', 50002, '--tls-root', self.tls)
        self.assertEqual(failed.returncode, 1)
        self.assertEqual(failed.stdout, '')
        self.assertEqual(failed.stderr.strip(), 'create failed: Helper failed: wallet-create')
        self.assert_clean(failed.stderr)
        self.assertNotIn('panic', failed.stderr)

    def test_cli_rejects_mainnet(self):
        failed = self.cli('create', '--helper', self.helper, '--directory', self.wallet, '--network', 'main',
                          '--maximum-height', 10000, '--port', 50002, '--tls-root', self.tls)
        self.assertNotEqual(failed.returncode, 0)
        self.assertEqual(failed.stdout, '')
        self.assertFalse(self.wallet.exists())
        self.assertEqual(self.calls(), [])


if __name__ == '__main__':
    unittest.main()
