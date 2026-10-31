"""Tests for remote_light_recovery: VM scripts run here against a private temporary
workspace and real disposable processes; the resident protocol is mocked.

Staged copy: the worktree tests unchanged, plus funding-helper scan/kill tests on
real disposable process groups and mocked-protocol tests for
interrupt_bob_after_acceptance. Run with: python3 -B test_remote_light_recovery.py -v
"""
import copy
import json
import os
import pathlib
import shutil
import signal
import stat
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from unittest import mock

sys.dont_write_bytecode = True
PORT = os.environ.get('MONZERO_DEX_ENGINE_PORT',
    str(pathlib.Path(__file__).resolve().parent))
sys.path.insert(0, PORT)
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import remote_light_recovery as rlr  # noqa: E402
from litecoin_app_fixture import SplitLitecoinApp  # noqa: E402
from remote_trade_workspace import RemoteTradeWorkspace  # noqa: E402

TRADE = '0c7d3a52-2f0e-4c35-9a77-3c2f3d7c9b10'
TXID = 'a' * 64
SLEEP = shutil.which('sleep')
# Leader of an owned group, like the remote supervisor shell: it owns its process
# group, starts the "app" (sleep under the fixture's command name) and records both.
LEADER = r'''
import os,subprocess,sys
d,binary=sys.argv[1:]
p=subprocess.Popen(["actualLitecoinAppSetup","300"],executable=binary)
with open(os.path.join(d,"app.pid.tmp"),"w") as f:
    f.write("%d %d\n"%(os.getpid(),p.pid))
os.rename(os.path.join(d,"app.pid.tmp"),os.path.join(d,"app.pid"))
p.wait()
'''


class LocalWorkspace:
    """Runs the VM-side scripts on this machine, failing like RemoteTradeWorkspace.run."""
    def __init__(self, root):
        self.root, self.calls = pathlib.Path(root), []

    path = RemoteTradeWorkspace.path

    def run(self, argv, data=None, timeout=60):
        self.calls.append(argv)
        argv = [sys.executable if argv[0] == 'python3' else argv[0]] + list(argv[1:])
        result = subprocess.run(argv, input=data, capture_output=True, timeout=timeout)
        if result.returncode:
            raise RuntimeError('VM command failed: ' + result.stderr.decode(errors='replace')[-2000:])
        return result.stdout


def private_dir(path):
    path.mkdir(mode=0o700)
    return path


def write(path, data, mode=0o600):
    path.parent.mkdir(mode=0o700, parents=True, exist_ok=True)
    with os.fdopen(os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, mode), 'wb') as out:
        out.write(data if isinstance(data, bytes) else json.dumps(data).encode())
    return path


def alive(pid):
    try:
        fields = pathlib.Path(f'/proc/{pid}/stat').read_text().rsplit(')', 1)[1].split()
    except FileNotFoundError:
        return False
    return fields[0] != 'Z'


@unittest.skipUnless(sys.platform.startswith('linux') and SLEEP, 'Needs Linux /proc and sleep')
class OwnedKillTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = pathlib.Path(self.temp.name)
        self.workspace = LocalWorkspace(self.root)
        self.binary = self.root / 'app-binary'
        self.binary.symlink_to(SLEEP)  # /proc/<pid>/exe resolves to the same file
        self.fixture = self.root / 'session-bob.json'
        self.groups, self.leaders = [], []

    def tearDown(self):
        for group in self.groups:
            try:
                os.killpg(group, signal.SIGKILL)
            except ProcessLookupError:
                pass
        for leader in self.leaders:
            leader.wait(timeout=10)
        self.temp.cleanup()

    def start_group(self, name, fixture=None):
        directory = private_dir(self.root / name)
        environment = dict(os.environ, MONZERO_LTC_APP_FIXTURE=str(fixture or self.fixture))
        leader = subprocess.Popen([sys.executable, '-I', '-c', LEADER, str(directory), SLEEP],
            env=environment, start_new_session=True)
        self.groups.append(leader.pid)
        self.leaders.append(leader)
        deadline = time.monotonic() + 10
        while not (directory / 'app.pid').exists():
            self.assertLess(time.monotonic(), deadline, 'fake resident did not start')
            time.sleep(0.02)
        group, app = map(int, (directory / 'app.pid').read_text().split())
        return directory, leader, group, app

    def part(self, directory, binary=None, fixture=None):
        return mock.Mock(directory=directory, binary=binary or self.binary, fixture=fixture or self.fixture)

    def test_kills_only_the_verified_owned_group(self):
        directory, leader, group, app = self.start_group('resident')
        _, decoy, decoy_group, decoy_app = self.start_group('other')
        rlr.kill_owned(self.workspace, self.part(directory))
        self.assertEqual(leader.wait(timeout=10), -signal.SIGKILL)
        self.assertFalse(alive(app))
        self.assertTrue(alive(decoy_group) and alive(decoy_app) and decoy.poll() is None)

    def assertRefused(self, directory, message, **part):
        with self.assertRaisesRegex(RuntimeError, message):
            rlr.kill_owned(self.workspace, self.part(directory, **part))

    def test_refuses_a_different_fixture_environment(self):
        directory, leader, group, app = self.start_group('resident', fixture=self.root / 'other-session.json')
        self.assertRefused(directory, 'fixture environment differs')
        self.assertTrue(alive(app) and leader.poll() is None)

    def test_refuses_a_different_executable(self):
        directory, leader, group, app = self.start_group('resident')
        # Not another coreutils name: a multicall build would resolve to the same file.
        self.assertRefused(directory, 'executable differs', binary=pathlib.Path(sys.executable))
        self.assertTrue(alive(app) and leader.poll() is None)

    def test_refuses_a_stale_group(self):
        directory, leader, group, app = self.start_group('resident')
        os.killpg(group, signal.SIGKILL)
        leader.wait(timeout=10)
        deadline = time.monotonic() + 10
        while alive(app):
            self.assertLess(time.monotonic(), deadline)
            time.sleep(0.02)
        self.assertRefused(directory, 'stale process group|outside the recorded owned group')

    def test_refuses_an_arbitrary_group_named_in_app_pid(self):
        # This test runner's own group: wrong executable, never signalled.
        directory = private_dir(self.root / 'resident')
        write(directory / 'app.pid', f'{os.getpgid(0)} {os.getpid()}\n'.encode())
        self.assertRefused(directory, 'Refusing to signal')
        # A real owned sleep whose recorded group is not its own.
        _, decoy, decoy_group, decoy_app = self.start_group('other')
        mixed = private_dir(self.root / 'mixed')
        write(mixed / 'app.pid', f'{os.getpgid(0)} {decoy_app}\n'.encode())
        self.assertRefused(mixed, 'outside the recorded owned group')
        self.assertTrue(alive(decoy_app) and decoy.poll() is None)

    def test_refuses_malformed_linked_or_missing_records(self):
        directory, leader, group, app = self.start_group('resident')
        for name, setup in (('missing', None), ('malformed', b'12 abc\n'), ('same', f'{group} {group}\n'.encode()),
                ('linked', 'link')):
            other = private_dir(self.root / name)
            if setup == 'link':
                (other / 'app.pid').symlink_to(directory / 'app.pid')
            elif setup is not None:
                write(other / 'app.pid', setup)
            self.assertRefused(other, 'Refusing to signal')
        self.assertTrue(alive(app) and leader.poll() is None)


# The resident "app" for helper tests: python under the fixture command name. It
# starts the funding helpers the mode asks for, records their pids, then waits.
# A helper is python too (it must run arbitrary argv), so the expected helper
# executable is sys.executable here and the flag is an exact argv element.
HELPER_APP = r'''
import json,os,subprocess,sys,time
d,mode=sys.argv[2:4]
flag="--dex-funding-"+"send-stdio-v1"
argv=[sys.executable,"-I","-c","import time; time.sleep(300)",flag,os.path.join(d,"helper-config.json")]
count={"none":0,"one":1,"two":2,"detached":1}[mode]
helpers=[subprocess.Popen(argv,start_new_session=mode=="detached") for _ in range(count)]
with open(os.path.join(d,"helpers.tmp"),"w") as f:
    json.dump([p.pid for p in helpers],f)
os.rename(os.path.join(d,"helpers.tmp"),os.path.join(d,"helpers.json"))
time.sleep(300)
'''
HELPER_LEADER = r'''
import os,subprocess,sys
d,code,mode=sys.argv[1:]
p=subprocess.Popen([sys.executable,"-I","-c",code,"actualLitecoinAppSetup",d,mode])
with open(os.path.join(d,"app.pid.tmp"),"w") as f:
    f.write("%d %d\n"%(os.getpid(),p.pid))
os.rename(os.path.join(d,"app.pid.tmp"),os.path.join(d,"app.pid"))
p.wait()
'''


@unittest.skipUnless(sys.platform.startswith('linux') and SLEEP, 'Needs Linux /proc and sleep')
class FundingHelperTests(unittest.TestCase):
    """find_helper and kill_owned(helper=...) against real disposable process groups."""
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = pathlib.Path(self.temp.name)
        self.workspace = LocalWorkspace(self.root)
        self.binary = pathlib.Path(sys.executable)
        self.fixture = self.root / 'session-bob.json'
        self.groups, self.leaders, self.loose, self.pids = [], [], [], []

    def tearDown(self):
        for group in self.groups:
            try:
                os.killpg(group, signal.SIGKILL)
            except ProcessLookupError:
                pass
        for pid in self.loose:
            try:
                os.kill(pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
        for leader in self.leaders:
            leader.wait(timeout=10)
        self.wait_gone(self.pids)
        self.temp.cleanup()

    def wait_gone(self, pids):
        """A dying helper must not be counted by the next test's scan."""
        deadline = time.monotonic() + 10
        while any(alive(pid) for pid in pids):
            self.assertLess(time.monotonic(), deadline, 'fake resident processes survived SIGKILL')
            time.sleep(0.02)

    def start(self, mode, name='resident'):
        directory = private_dir(self.root / name)
        environment = dict(os.environ, MONZERO_LTC_APP_FIXTURE=str(self.fixture))
        leader = subprocess.Popen([sys.executable, '-I', '-c', HELPER_LEADER, str(directory), HELPER_APP, mode],
            env=environment, start_new_session=True)
        self.groups.append(leader.pid)
        self.leaders.append(leader)
        deadline = time.monotonic() + 15
        while not ((directory / 'app.pid').exists() and (directory / 'helpers.json').exists()):
            self.assertLess(time.monotonic(), deadline, 'fake resident did not start its helpers')
            time.sleep(0.02)
        group, app = map(int, (directory / 'app.pid').read_text().split())
        helpers = json.loads((directory / 'helpers.json').read_text())
        self.pids += [app] + helpers
        if mode == 'detached':
            self.loose += helpers
        return directory, leader, group, app, helpers

    def part(self, directory):
        return mock.Mock(directory=directory, binary=self.binary, fixture=self.fixture)

    def test_finds_the_one_helper_child_in_the_owned_group(self):
        directory, leader, group, app, helpers = self.start('one')
        found = rlr.find_helper(self.workspace, self.part(directory), self.binary)
        self.assertEqual((found['pid'], found['group'], found['app']), (helpers[0], group, app))
        self.assertIsInstance(found['start'], int)
        self.assertTrue(alive(app) and alive(helpers[0]) and leader.poll() is None)

    def test_refuses_no_helper_or_two_helpers(self):
        for mode in ('none', 'two'):
            with self.subTest(mode=mode):
                directory, leader, group, app, helpers = self.start(mode, name='resident-' + mode)
                with self.assertRaisesRegex(RuntimeError, 'expected exactly one owned funding helper, found %d'
                        % len(helpers)):
                    rlr.find_helper(self.workspace, self.part(directory), self.binary)
                os.killpg(group, signal.SIGKILL)
                leader.wait(timeout=10)
                self.wait_gone([app] + helpers)

    def test_refuses_a_helper_outside_the_owned_group(self):
        directory, leader, group, app, helpers = self.start('detached')
        with self.assertRaisesRegex(RuntimeError, 'not a child of the resident app in its owned group'):
            rlr.find_helper(self.workspace, self.part(directory), self.binary)
        self.assertTrue(alive(helpers[0]) and alive(app))

    def test_refuses_a_different_helper_executable(self):
        directory, leader, group, app, helpers = self.start('one')
        with self.assertRaisesRegex(RuntimeError, 'found 0'):
            rlr.find_helper(self.workspace, self.part(directory), SLEEP)

    def test_kills_app_and_verified_helper_only(self):
        directory, leader, group, app, helpers = self.start('one')
        decoy_dir = private_dir(self.root / 'decoy')
        decoy = subprocess.Popen([sys.executable, '-I', '-c', LEADER, str(decoy_dir), SLEEP],
            env=dict(os.environ, MONZERO_LTC_APP_FIXTURE=str(self.fixture)), start_new_session=True)
        self.groups.append(decoy.pid)
        self.leaders.append(decoy)
        deadline = time.monotonic() + 10
        while not (decoy_dir / 'app.pid').exists():
            self.assertLess(time.monotonic(), deadline)
            time.sleep(0.02)
        decoy_app = int((decoy_dir / 'app.pid').read_text().split()[1])
        found = rlr.find_helper(self.workspace, self.part(directory), self.binary)
        rlr.kill_owned(self.workspace, self.part(directory), dict(found, binary=self.binary))
        self.assertEqual(leader.wait(timeout=10), -signal.SIGKILL)
        self.assertFalse(alive(app) or alive(helpers[0]))
        self.assertTrue(alive(decoy_app) and decoy.poll() is None)

    def assertKillRefused(self, directory, helper, message):
        with self.assertRaisesRegex(RuntimeError, message):
            rlr.kill_owned(self.workspace, self.part(directory), helper)

    def test_refuses_a_changed_helper_identity(self):
        directory, leader, group, app, helpers = self.start('one')
        found = dict(rlr.find_helper(self.workspace, self.part(directory), self.binary), binary=self.binary)
        self.assertKillRefused(directory, dict(found, start=found['start'] + 1), 'funding helper changed')
        self.assertKillRefused(directory, dict(found, pid=app), 'funding helper changed')
        self.assertKillRefused(directory, dict(found, binary=SLEEP), 'funding helper changed')
        self.assertKillRefused(directory, dict(found, pid='12a'), 'malformed funding helper identity')
        self.assertTrue(alive(app) and alive(helpers[0]) and leader.poll() is None)

    def test_refuses_when_the_helper_already_exited(self):
        directory, leader, group, app, helpers = self.start('one')
        found = dict(rlr.find_helper(self.workspace, self.part(directory), self.binary), binary=self.binary)
        os.kill(helpers[0], signal.SIGKILL)  # stays a zombie: the fake app never reaps it
        deadline = time.monotonic() + 10
        while alive(helpers[0]):
            self.assertLess(time.monotonic(), deadline)
            time.sleep(0.02)
        self.assertKillRefused(directory, found, 'funding helper changed|exited during verification')
        self.assertTrue(alive(app) and leader.poll() is None)


class PublishedTests(unittest.TestCase):
    def test_reports_any_entry_at_the_response_path(self):
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            workspace = LocalWorkspace(root)
            self.assertFalse(rlr.published(workspace, root / '7-response.json'))
            write(root / '7-response.json', b'{}')
            self.assertTrue(rlr.published(workspace, root / '7-response.json'))
            (root / '8-response.json').symlink_to(root / 'missing')
            self.assertTrue(rlr.published(workspace, root / '8-response.json'))


class PrepareTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = pathlib.Path(self.temp.name)
        self.workspace = LocalWorkspace(self.root)
        self.previous = private_dir(self.root / 'resident')
        self.session = write(self.root / 'session.json', {'ltcNetwork': 'testnet', 'controlPublicKey': 'k' * 64,
            'controlFile': str(self.root / 'control.json'), 'appConfigFile': str(self.root / 'app/app.json'),
            'residentRole': 'bob', 'residentDirectory': str(self.previous)})
        self.ledger = write(self.previous / 'bob-started-trades.json', b'{"started":["%s"]}' % TRADE.encode())
        self.secret = write(self.previous / 'light-password', b'vm-only-password-bytes')
        self.part = mock.Mock(directory=pathlib.PurePosixPath(self.previous),
            fixture=pathlib.PurePosixPath(self.session), generation=0)

    def tearDown(self):
        self.temp.cleanup()

    def test_new_private_resident_keeps_ledger_and_moves_password(self):
        value = rlr.prepare(self.workspace, self.part)
        directory = self.root / 'resident-resumed-1'
        self.assertEqual(value, {'generation': 1, 'directory': str(directory),
            'fixture': str(self.root / 'session-resume-1.json'), 'ledgerCopied': True, 'passwordMoved': True})
        self.assertEqual(stat.S_IMODE(directory.stat().st_mode), 0o700)
        request = json.loads((self.root / 'session-resume-1.json').read_text())
        original = json.loads(self.session.read_text())
        self.assertEqual(request, dict(original, resumeResident=True, residentDirectory=str(directory)))
        self.assertEqual(stat.S_IMODE((self.root / 'session-resume-1.json').stat().st_mode), 0o600)
        self.assertEqual((directory / 'bob-started-trades.json').read_bytes(), self.ledger.read_bytes())
        self.assertTrue(self.ledger.exists())  # copied, never moved or reset
        self.assertFalse(self.secret.exists())
        self.assertEqual((directory / 'light-password').read_bytes(), b'vm-only-password-bytes')
        self.assertEqual(stat.S_IMODE((directory / 'light-password').stat().st_mode), 0o600)

    def test_generation_is_unique(self):
        private_dir(self.root / 'resident-resumed-1')
        write(self.root / 'session-resume-2.json', b'{}')
        self.assertEqual(rlr.prepare(self.workspace, self.part)['generation'], 3)

    def test_missing_testnet_ledger_refuses_before_moving_password(self):
        self.ledger.unlink()
        with self.assertRaisesRegex(RuntimeError, 'start ledger is missing'):
            rlr.prepare(self.workspace, self.part)
        self.assertTrue(self.secret.exists())

    def test_refuses_a_fixture_for_another_resident(self):
        self.part.directory = pathlib.PurePosixPath(self.root / 'elsewhere')
        with self.assertRaisesRegex(RuntimeError, 'different resident'):
            rlr.prepare(self.workspace, self.part)
        self.assertTrue(self.secret.exists())

    def test_without_password_or_regtest_ledger(self):
        self.secret.unlink()
        self.ledger.unlink()
        request = json.loads(self.session.read_text())
        del request['controlPublicKey']
        self.session.unlink()
        write(self.session, request)
        value = rlr.prepare(self.workspace, self.part)
        self.assertFalse(value['ledgerCopied'] or value['passwordMoved'])


class SnapshotTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = pathlib.Path(self.temp.name)
        self.workspace = LocalWorkspace(self.root)
        app, meta, wallet = self.root / 'app', self.root / 'app/meta', self.root / 'local-light-wallet'
        quote = write(self.root / 'start-quote.json', b'{"signed":"quote"}')
        journal = write(app / 'journal.json', {'tradeId': TRADE, 'metadataDirectory': str(meta),
            'snapshotDirectory': str(app / 'secrets'), 'startQuoteFile': str(quote)})
        payment = write(app / 'payment.json', {'tradeId': TRADE})
        write(app / 'app.json', {'schema': 'monzero-private-ltc-app-v1', 'journalConfigFile': str(journal),
            'paymentConfigFile': str(payment)})
        self.intent = write(meta / (TRADE + '.ltc-recovery-intent'), b'intent-v1')
        write(meta / (TRADE + '.ltc-light-wallet.json'), b'{}')
        write(meta / f'ltc-light-dex-ui-{TRADE}-payment.json', b'{"payment":1}')
        write(meta / f'ltc-light-dex-ui-{TRADE}-review.json', b'{"review":1}')
        self.vault = write(wallet / 'vault/keys.bin', b'SECRET-VAULT-BYTES')
        write(wallet / 'spend/allocations.json', b'{"reserved":[1]}')
        write(wallet / 'spend/signed/tx.bin', b'signed-bytes')
        write(wallet / 'addressbook/book.json', b'[]')
        config = write(wallet / 'config.json', {'wallet': {'spendDirectory': str(wallet / 'spend'),
            'addressBookDirectory': str(wallet / 'addressbook'), 'vault': {'directory': str(wallet / 'vault')}}})
        launcher = write(wallet / 'launcher.json', {'configFile': str(config)})
        self.wallet_root = wallet
        self.light = mock.Mock(setup={'launcher': str(launcher)})

    def tearDown(self):
        self.temp.cleanup()

    def take(self):
        return rlr.snapshot(self.workspace, self.light, TRADE)

    def test_returns_only_aggregates(self):
        value = self.take()
        self.assertEqual(set(value), {'wallet', 'metadata'})
        self.assertEqual(value['wallet']['count'], 4)
        self.assertEqual(value['metadata']['count'], 8)
        self.assertNotIn('SECRET', json.dumps(value))
        self.assertNotIn(str(self.root), json.dumps(value))
        self.assertEqual(self.take(), value)

    def test_detects_wallet_and_metadata_drift(self):
        saved = self.take()
        self.vault.write_bytes(b'SECRET-VAULT-BYTES-changed')
        changed = self.take()
        self.assertNotEqual(changed['wallet'], saved['wallet'])
        self.assertEqual(changed['metadata'], saved['metadata'])
        self.intent.write_bytes(b'intent-v2')
        self.assertNotEqual(self.take()['metadata'], changed['metadata'])

    def test_detects_added_and_renamed_wallet_files(self):
        saved = self.take()
        extra = write(self.wallet_root / 'spend/extra.json', b'{}')
        self.assertEqual(self.take()['wallet']['count'], saved['wallet']['count'] + 1)
        extra.unlink()
        (self.wallet_root / 'spend/signed/tx.bin').rename(self.wallet_root / 'spend/signed/tx2.bin')
        self.assertNotEqual(self.take()['wallet']['digest'], saved['wallet']['digest'])

    def test_refuses_missing_linked_or_foreign_trade(self):
        self.intent.unlink()
        with self.assertRaisesRegex(RuntimeError, 'missing or malformed'):
            self.take()
        self.intent.symlink_to(self.vault)
        with self.assertRaisesRegex(RuntimeError, 'not an owned regular file'):
            self.take()
        self.intent.unlink()
        write(self.intent, b'intent-v1')
        with self.assertRaisesRegex(RuntimeError, 'different trade'):
            rlr.snapshot(self.workspace, self.light, '11111111-2f0e-4c35-9a77-3c2f3d7c9b10')
        (self.wallet_root / 'spend/linked').symlink_to(self.wallet_root / 'vault')
        with self.assertRaisesRegex(RuntimeError, 'linked directory'):
            self.take()


FUNDING = dict(configured=True, busy=False, state='complete', operation='send', tradeId=TRADE,
    quoteCommitment='b' * 64, configurationDigest='c' * 64,
    retainedPayment={'paymentAddress': 'tltc1qcontract', 'paymentAtomic': '100000'},
    retainedReview={'signingCommitment': 'd' * 64, 'changeAddress': 'tltc1qchange'},
    retainedTxid=TXID, result={'txid': TXID, 'signed': True, 'broadcast': True},
    review={'commitment': 'e' * 64}, reviewOperation='send')
SNAP = {'wallet': {'digest': '1' * 64, 'count': 5}, 'metadata': {'digest': '2' * 64, 'count': 8}}


def state(funding=None, busy=False, reconnects=0):
    return {'app': {'busy': busy}, 'peer': {'busy': False}, 'lightFunding': copy.deepcopy(funding or {}),
        'reconnects': reconnects}


class FakeProcess:
    def __init__(self, pid):
        self.pid, self.returncode, self.released = pid, None, False

    def poll(self):
        return self.returncode

    def release(self):
        self.released = True
        self.returncode = -signal.SIGKILL

    def wait(self, timeout=None):
        return self.returncode


class FakeAlice:
    def __init__(self, log):
        self.log, self.reconnects, self.process = log, 1, FakeProcess(4242)
        self.rebuild = 1  # reconnects added when the reopened Bob connects

    def call(self, role, operation, accepted=True, **fields):
        self.log.append(('alice', operation))
        result = True
        if operation == 'wait-reconnected':
            self.reconnects += self.rebuild
            result = self.reconnects > fields['reconnects']
        self.last = {'accepted': result, 'alice': state(reconnects=self.reconnects)}
        return self.last['alice']


class FakeBob:
    def __init__(self, log, responses, generation=0):
        self.log, self.responses, self.generation = log, responses, generation
        self.process = FakeProcess(9000 + generation)
        self.directory = pathlib.PurePosixPath('/vm/trade/resident')
        self.closed = False

    def signal_remote(self, name):
        raise AssertionError('Recovery must never use the unverified signal path')

    def close(self):
        self.closed = True

    def call(self, role, operation, accepted=True, **fields):
        self.log.append(('bob', operation))
        result, value = self.responses.get(operation, (True, state()))
        self.last = {'accepted': result, 'bob': value}
        if accepted is not None and result is not accepted:
            raise RuntimeError('Resident app rejected or misrouted operation: ' + operation)
        return value


class ProtocolTests(unittest.TestCase):
    def setUp(self):
        self.build()
        # Side effects look up self.* at call time, so build() resets them in place.
        self.patches = [mock.patch.object(rlr, 'kill_owned', side_effect=lambda w, p: self.log.append(('vm', 'kill'))),
            mock.patch.object(rlr, 'prepare', side_effect=lambda w, p: self.log.append(('vm', 'prepare')) or self.prepared),
            mock.patch.object(rlr, 'snapshot', side_effect=lambda *a: self.log.append(('vm', 'snapshot')) or self.snapshots.pop(0)),
            mock.patch.object(rlr, 'reopen', side_effect=lambda *a: self.log.append(('vm', 'reopen')) or self.new)]
        for patch in self.patches:
            patch.start()
            self.addCleanup(patch.stop)

    def build(self):
        self.log = []
        self.alice = FakeAlice(self.log)
        self.old = FakeBob(self.log, {'status': (True, state(FUNDING))})
        restored = dict(FUNDING, review={}, reviewOperation='')
        self.new = FakeBob(self.log, {'reserve': (False, state()), 'light-select': (True, state(restored))}, 1)
        self.resident = SplitLitecoinApp.__new__(SplitLitecoinApp)
        self.resident.parts = {'alice': self.alice, 'bob': self.old}
        self.light = mock.Mock(setup={'launcher': '/vm/trade/local-light-wallet/launcher.json'},
            helper='/vm/trade/light-helper', digest='f' * 64, spec=['setup', 'helper', 'digest'])
        self.workspace = mock.Mock()
        self.workspace.path = lambda name: '/vm/trade/' + name
        self.prepared = {'generation': 1, 'directory': '/vm/trade/resident-resumed-1',
            'fixture': '/vm/trade/session-resume-1.json', 'ledgerCopied': True, 'passwordMoved': True}
        self.snapshots = [SNAP, copy.deepcopy(SNAP)]

    def restart(self):
        return rlr.restart_bob(self.resident, self.workspace, self.light)

    def test_successful_restart_returns_public_evidence(self):
        evidence = self.restart()
        self.assertIs(self.resident.parts['bob'], self.new)
        self.assertTrue(self.old.closed and self.old.process.released)
        self.assertEqual(self.light.resident_password_path, '/vm/trade/resident-resumed-1/light-password')
        bob = [entry[1] for entry in self.log if entry[0] == 'bob']
        self.assertEqual(bob, ['status', 'journal', 'wait-negotiation', 'reserve', 'light-resume-wallet',
            'light-select', 'light-resume-recovery', 'wait-recovery'])
        # Alice is pinged directly after every reopened-Bob operation.
        for index, entry in enumerate(self.log):
            if entry[0] == 'bob' and entry[1] != 'status':
                self.assertEqual(self.log[index + 1], ('alice', 'status'))
        vm = [operation for who, operation in self.log if who == 'vm']
        self.assertEqual(vm, ['snapshot', 'kill', 'prepare', 'reopen', 'snapshot'])
        self.assertLess(self.log.index(('vm', 'kill')), self.log.index(('vm', 'prepare')))
        self.assertEqual(evidence['generation'], 1)
        self.assertEqual(evidence['retainedTxid'], TXID)
        self.assertEqual(evidence['walletFiles'], SNAP['wallet'])
        self.assertEqual((evidence['aliceReconnectsBefore'], evidence['aliceReconnectsAfter']), (1, 2))
        self.assertTrue(evidence['alicePidUnchanged'] and evidence['oldApprovalAbsent'])
        self.assertNotIn('password', json.dumps(evidence).replace('lightPasswordMovedOnVm', ''))
        self.assertTrue(self.resident.last['accepted'])

    def test_signed_unsent_boundary_is_accepted(self):
        signed = dict(FUNDING, operation='sign', result={'txid': TXID, 'signed': True, 'broadcast': False})
        self.old.responses['status'] = (True, state(signed))
        self.new.responses['light-select'] = (True, state(dict(signed, review={}, reviewOperation='')))
        self.assertEqual(self.restart()['boundary'], 'sign')

    def assertRefusedBeforeKill(self, message):
        with self.assertRaisesRegex(RuntimeError, message):
            self.restart()
        self.assertNotIn(('vm', 'kill'), self.log)
        self.assertIs(self.resident.parts['bob'], self.old)

    def test_busy_roles_are_refused(self):
        self.old.responses['status'] = (True, state(FUNDING, busy=True))
        self.assertRefusedBeforeKill('both roles idle')

    def test_non_boundary_or_unconfirmed_funding_is_refused(self):
        for changed in (dict(FUNDING, operation='send-review'), dict(FUNDING, state='outcome-unconfirmed'),
                dict(FUNDING, busy=True), dict(FUNDING, retainedTxid=''), dict(FUNDING, retainedReview={}),
                dict(FUNDING, result={'txid': 'b' * 64}),
                dict(FUNDING, operation='sign', result={'txid': TXID, 'signed': True, 'broadcast': True})):
            with self.subTest(changed=changed):
                self.build()
                self.old.responses['status'] = (True, state(changed))
                self.assertRefusedBeforeKill('idle|retained|differs|already broadcast')

    def test_retained_field_drift_fails_without_evidence(self):
        for field in rlr.RETAINED:
            with self.subTest(field=field):
                self.build()
                value = {'retainedPayment': {'paymentAtomic': '1'}, 'retainedReview': {'changeAddress': 'x'}}.get(field, '0' * 64)
                self.new.responses['light-select'] = (True, state(dict(FUNDING, review={}, reviewOperation='', **{field: value})))
                with self.assertRaisesRegex(RuntimeError, 'changed retained local funding: ' + field):
                    self.restart()
                self.assertNotIn(('bob', 'light-resume-recovery'), self.log)

    def test_old_review_must_be_absent(self):
        self.new.responses['light-select'] = (True, state(dict(FUNDING, reviewOperation='')))
        with self.assertRaisesRegex(RuntimeError, 'reused an old approval'):
            self.restart()
        self.assertNotIn(('bob', 'light-resume-recovery'), self.log)

    def test_old_review_operation_must_be_absent(self):
        self.new.responses['light-select'] = (True, state(dict(FUNDING, review={})))
        with self.assertRaisesRegex(RuntimeError, 'reused an old approval'):
            self.restart()
        self.assertNotIn(('bob', 'light-resume-recovery'), self.log)

    def test_reserve_must_be_rejected(self):
        self.new.responses['reserve'] = (True, state())
        with self.assertRaisesRegex(RuntimeError, 'reserve'):
            self.restart()
        self.assertNotIn(('bob', 'light-select'), self.log)

    def test_alice_must_reconnect(self):
        self.alice.rebuild = 0
        with self.assertRaisesRegex(RuntimeError, 'Alice did not rebuild'):
            self.restart()
        self.assertNotIn(('bob', 'light-resume-recovery'), self.log)

    def during(self, trigger, action):
        original = self.new.call

        def call(role, operation, **fields):
            if operation == trigger:
                action()
            return original(role, operation, **fields)
        self.new.call = call

    def test_alice_process_must_not_be_replaced(self):
        self.during('wait-negotiation', lambda: setattr(self.alice, 'process', FakeProcess(4343)))
        with self.assertRaisesRegex(RuntimeError, 'Alice process changed or stopped'):
            self.restart()
        self.assertNotIn(('bob', 'reserve'), self.log)

    def test_alice_process_must_stay_live(self):
        self.during('journal', lambda: setattr(self.alice.process, 'returncode', 1))
        with self.assertRaisesRegex(RuntimeError, 'Alice process changed or stopped'):
            self.restart()
        self.assertNotIn(('bob', 'wait-negotiation'), self.log)

    def test_wallet_or_metadata_drift_fails_without_evidence(self):
        for part in ('wallet', 'metadata'):
            with self.subTest(part=part):
                self.build()
                drifted = copy.deepcopy(SNAP)
                drifted[part]['digest'] = '9' * 64
                self.snapshots = [SNAP, drifted]
                with self.assertRaisesRegex(RuntimeError, 'changed saved wallet'):
                    self.restart()

    def test_requires_local_alice_and_remote_bob(self):
        self.resident.parts['bob'] = self.alice
        with self.assertRaisesRegex(RuntimeError, 'VM-resident Bob'):
            self.restart()
        self.assertEqual(self.log, [])

    def test_reopened_busy_funding_is_refused(self):
        self.new.responses['light-select'] = (True, state(dict(FUNDING, review={}, reviewOperation='', busy=True)))
        with self.assertRaisesRegex(RuntimeError, 'Reopened light funding is busy'):
            self.restart()
        self.assertNotIn(('bob', 'light-resume-recovery'), self.log)


COMMIT = 'e' * 64
SIGNED = '5' * 64
PENDING = dict(FUNDING, operation='send-review', reviewOperation='send-review', result={'review': True},
    review={'commitment': COMMIT, 'details': {'action': 'send', 'txid': TXID, 'signedDigest': SIGNED}})
HELPER = {'pid': 7001, 'start': 123456, 'group': 7000, 'app': 7000 + 2}


class FakePeer:
    """The staged LiveElectrumPeer hook contract, without sockets."""
    def __init__(self, log):
        self.log = log
        self.broadcast_lock = threading.Lock()
        self.broadcast_held, self.release_broadcast_reply = threading.Event(), threading.Event()
        self.hold_next_broadcast_reply = False
        self.accepted_broadcast_digests, self.held_broadcast_digests = [], []
        self.holding = self.dropped_broadcast_replies = self.arms = 0
        self.threads = []

    def hold_next_broadcast(self):
        self.log.append(('peer', 'arm'))
        self.arms += 1
        self.broadcast_held.clear()
        self.release_broadcast_reply.clear()
        self.hold_next_broadcast_reply = True

    def accept(self, digest):
        """What the staged peer does once sendrawtransaction returned."""
        with self.broadcast_lock:
            self.accepted_broadcast_digests.append(digest)
            hold = self.hold_next_broadcast_reply
            if hold:
                self.hold_next_broadcast_reply = False
                self.held_broadcast_digests.append(digest)
                self.holding += 1
        if not hold:
            return

        def held():
            released = self.release_broadcast_reply.wait(10)
            self.log.append(('peer', 'release'))
            with self.broadcast_lock:
                self.holding -= 1
                if released:
                    self.dropped_broadcast_replies += 1
        thread = threading.Thread(target=held, daemon=True)
        self.threads.append(thread)
        thread.start()
        self.broadcast_held.set()

    def close(self):
        self.release_broadcast_reply.set()
        for thread in self.threads:
            thread.join(10)


class InflightBob(FakeBob):
    def __init__(self, log, responses, generation=0):
        super().__init__(log, responses, generation)
        self.sequence, self.on_send = 0, None

    def enqueue(self, role, operation, **fields):
        self.log.append(('bob-enqueue', operation, fields.get('commitment')))
        self.sequence += 1
        if self.on_send:
            self.on_send()
        return self.directory / f'{self.sequence}-response.json'


class InflightTests(unittest.TestCase):
    def setUp(self):
        self.build()
        self.patches = [mock.patch.object(rlr, 'kill_owned', side_effect=self.kill),
            mock.patch.object(rlr, 'find_helper', side_effect=lambda w, p, h: self.log.append(('vm', 'helper', h))
                or self.helper()),
            mock.patch.object(rlr, 'published', side_effect=lambda w, path: self.log.append(('vm', 'published'))
                or self.response(str(path))),
            mock.patch.object(rlr, 'prepare', side_effect=lambda w, p: self.log.append(('vm', 'prepare')) or self.prepared),
            mock.patch.object(rlr, 'snapshot', side_effect=lambda *a: self.log.append(('vm', 'snapshot'))
                or self.take()),
            mock.patch.object(rlr, 'reopen', side_effect=lambda *a: self.log.append(('vm', 'reopen')) or self.new)]
        for patch in self.patches:
            patch.start()
            self.addCleanup(patch.stop)

    def tearDown(self):
        self.peer.close()

    def build(self):
        if hasattr(self, 'peer'):
            self.peer.close()
        self.log = []
        self.alice = FakeAlice(self.log)
        self.peer = FakePeer(self.log)
        self.old = InflightBob(self.log, {'status': (True, state(PENDING))})
        self.old.on_send = lambda: self.peer.accept(SIGNED)
        restored = dict(PENDING, operation='', review={}, reviewOperation='', result={})
        self.new = FakeBob(self.log, {'reserve': (False, state()), 'light-select': (True, state(restored))}, 1)
        self.resident = SplitLitecoinApp.__new__(SplitLitecoinApp)
        self.resident.parts = {'alice': self.alice, 'bob': self.old}
        self.light = mock.Mock(setup={'launcher': '/vm/trade/local-light-wallet/launcher.json'},
            helper='/vm/trade/light-helper', digest='f' * 64, spec=['setup', 'helper', 'digest'])
        self.workspace = mock.Mock()
        self.workspace.path = lambda name: '/vm/trade/' + name
        self.prepared = {'generation': 1, 'directory': '/vm/trade/resident-resumed-1',
            'fixture': '/vm/trade/session-resume-1.json', 'ledgerCopied': True, 'passwordMoved': True}
        self.snapshots = [SNAP, copy.deepcopy(SNAP)]
        self.take = lambda: self.snapshots.pop(0)
        self.helper = lambda: dict(HELPER)
        self.response = lambda path: False
        self.at_kill = None

    def kill(self, workspace, part, helper=None):
        self.log.append(('vm', 'kill'))
        self.at_kill = {'part': part, 'helper': helper, 'released': self.peer.release_broadcast_reply.is_set(),
            'holding': self.peer.holding}

    def interrupt(self, commitment=COMMIT, **options):
        options.setdefault('poll_seconds', 0.01)
        options.setdefault('release_timeout', 5)
        return rlr.interrupt_bob_after_acceptance(self.resident, self.workspace, self.light, self.peer, commitment,
            **options)

    def vm(self):
        return [entry[1] for entry in self.log if entry[0] == 'vm']

    def at(self, entry):
        return next(index for index, value in enumerate(self.log) if value[:len(entry)] == entry)

    def test_kills_at_the_barrier_and_reopens_without_resubmitting(self):
        evidence = self.interrupt()
        self.assertIs(self.resident.parts['bob'], self.new)
        self.assertTrue(self.old.closed and self.old.process.released)
        # Exactly one asynchronous send with the caller's fresh commitment.
        self.assertEqual([e for e in self.log if e[0] == 'bob-enqueue'], [('bob-enqueue', 'light-send', COMMIT)])
        self.assertEqual(self.peer.arms, 1)
        self.assertLess(self.at(('peer', 'arm')), self.at(('bob-enqueue',)))
        # Barrier, then response check, helper check and snapshot, then the kill.
        self.assertEqual(self.vm(), ['published', 'helper', 'snapshot', 'kill', 'published', 'prepare', 'reopen',
            'snapshot', 'published'])
        self.assertLess(self.at(('bob-enqueue',)), self.at(('vm', 'snapshot')))
        self.assertEqual(self.at_kill['helper'], dict(HELPER, binary=self.light.helper))
        self.assertIs(self.at_kill['part'], self.old)
        self.assertEqual((self.at_kill['released'], self.at_kill['holding']), (False, 1))
        self.assertLess(self.at(('vm', 'kill')), self.at(('peer', 'release')))
        self.assertLess(self.at(('peer', 'release')), self.at(('vm', 'prepare')))
        self.assertEqual((self.peer.holding, self.peer.dropped_broadcast_replies), (0, 1))
        self.assertFalse(self.peer.hold_next_broadcast_reply)
        self.assertEqual(self.peer.accepted_broadcast_digests, [SIGNED])
        # The reopened Bob only resumes the swap recovery; nothing sends, retries or signs.
        bob = [entry[1] for entry in self.log if entry[0] == 'bob']
        self.assertEqual(bob, ['status', 'journal', 'wait-negotiation', 'reserve', 'light-resume-wallet',
            'light-select', 'light-resume-recovery', 'wait-recovery'])
        for index, entry in enumerate(self.log):
            if entry[0] == 'bob' and entry[1] != 'status':
                self.assertEqual(self.log[index + 1], ('alice', 'status'))
        self.assertEqual(evidence['schema'], 'monzero-ltc-remote-light-inflight-v1')
        self.assertEqual((evidence['heldSignedDigest'], evidence['acceptedBroadcasts'], evidence['retainedTxid']),
            (SIGNED, 1, TXID))
        self.assertTrue(evidence['reconcileRequired'] and evidence['replyReleasedAfterKill']
            and evidence['fundingHelperVerifiedBeforeKill'] and evidence['oldApprovalAbsent'])
        self.assertFalse(evidence['resubmitted'] or evidence['oldResponsePublished'] or evidence['executable'])
        self.assertEqual(evidence['walletFiles'], SNAP['wallet'])
        self.assertEqual((evidence['aliceReconnectsBefore'], evidence['aliceReconnectsAfter']), (1, 2))
        self.assertTrue(self.resident.last['accepted'])

    def assertNotInterrupted(self, message, **options):
        with self.assertRaisesRegex(RuntimeError, message):
            self.interrupt(**options)
        self.assertNotIn('kill', self.vm())
        self.assertNotIn('prepare', self.vm())
        self.assertIs(self.resident.parts['bob'], self.old)
        self.assertFalse(self.old.closed)
        self.assertFalse(self.peer.hold_next_broadcast_reply, 'hold left armed after failure')

    def assertRefusedBeforeArming(self, message, **options):
        self.assertNotInterrupted(message, **options)
        self.assertEqual(self.peer.arms, 0)
        self.assertFalse(any(entry[0] == 'bob-enqueue' for entry in self.log))
        self.assertEqual(self.vm(), [])

    def test_stale_commitment_is_refused_before_sending(self):
        for commitment in ('0' * 64, '', None):
            with self.subTest(commitment=commitment):
                self.build()
                self.assertRefusedBeforeArming('stale or names a different commitment', commitment=commitment)

    def test_only_an_idle_fresh_send_approval_is_interrupted(self):
        details = PENDING['review']['details']
        for changed in (dict(PENDING, operation='retry-review', reviewOperation='retry-review'),
                dict(PENDING, operation='send', reviewOperation=''), dict(PENDING, busy=True),
                dict(PENDING, state='outcome-unconfirmed'), dict(PENDING, retainedTxid=''),
                dict(PENDING, review={'commitment': COMMIT, 'details': dict(details, txid='b' * 64)}),
                dict(PENDING, review={'commitment': COMMIT, 'details': dict(details, signedDigest='xyz')}),
                dict(PENDING, review={'commitment': COMMIT, 'details': dict(details, action='retry')})):
            with self.subTest(changed=changed):
                self.build()
                self.old.responses['status'] = (True, state(changed))
                self.assertRefusedBeforeArming('idle|fresh send approval|retained signed')

    def test_busy_roles_are_refused(self):
        self.old.responses['status'] = (True, state(PENDING, busy=True))
        self.assertRefusedBeforeArming('both roles idle')

    def test_peer_without_hold_hooks_is_refused(self):
        del self.peer.held_broadcast_digests
        self.assertRefusedBeforeArming('broadcast hold hooks')

    def test_rejected_stale_send_fails_closed_without_acceptance(self):
        self.old.on_send = None  # the app answered the send itself; no relay happened
        self.response = lambda path: path == '/vm/trade/resident/1-response.json'
        self.assertNotInterrupted(r'did not reach the node-accepted broadcast barrier.*0 acceptance\(s\) observed at the bound')
        self.assertTrue(self.peer.release_broadcast_reply.is_set())
        self.assertNotIn('snapshot', self.vm())
        self.assertNotIn('helper', self.vm())

    def test_barrier_timeout_fails_closed(self):
        self.old.on_send = None
        start = time.monotonic()
        self.assertNotInterrupted(r'did not reach the node-accepted broadcast barrier.*0 acceptance\(s\) observed at the bound',
            barrier_timeout=0.2)
        self.assertLess(time.monotonic() - start, 5)
        self.assertTrue(self.peer.release_broadcast_reply.is_set())

    def test_stopped_supervisor_fails_closed(self):
        def stop():
            self.old.process.returncode = 255
        self.old.on_send = stop
        self.assertNotInterrupted('did not reach the node-accepted broadcast barrier', barrier_timeout=5)

    def test_acceptance_after_the_bound_is_reported(self):
        self.old.on_send = None

        def late(path):
            self.peer.accepted_broadcast_digests.append(SIGNED)  # bypassed the hold
            return True
        self.response = late
        self.assertNotInterrupted('1 acceptance\\(s\\) observed at the bound.*pending send may still complete.*reconcile')

    def test_held_bytes_must_be_the_retained_signed_digest(self):
        self.old.on_send = lambda: self.peer.accept('6' * 64)
        self.assertNotInterrupted('not the retained signed transaction')
        self.assertNotIn('snapshot', self.vm())
        self.peer.close()
        self.assertEqual(self.peer.dropped_broadcast_replies, 1)  # released in finally, never answered

    def test_response_published_at_the_barrier_is_refused(self):
        self.response = lambda path: True
        self.assertNotInterrupted('published a relay response before interruption')
        self.assertNotIn('helper', self.vm())
        self.assertNotIn('snapshot', self.vm())

    def test_unverified_funding_helper_is_refused(self):
        def refuse():
            raise RuntimeError('VM command failed: Refusing to interrupt relay: expected exactly one owned '
                'funding helper, found 2')
        self.helper = refuse
        self.assertNotInterrupted('exactly one owned funding helper')
        self.assertNotIn('snapshot', self.vm())
        self.assertTrue(self.peer.release_broadcast_reply.is_set())

    def test_hold_expired_before_kill_is_refused(self):
        def expire():
            self.peer.holding = 0
            return SNAP
        self.take = expire
        self.assertNotInterrupted('released or timed out before interruption')

    def test_response_after_kill_stops_before_reopen(self):
        self.response = lambda path: ('vm', 'kill') in self.log
        with self.assertRaisesRegex(RuntimeError, 'Interrupted Bob published a completed relay response'):
            self.interrupt()
        self.assertNotIn('prepare', self.vm())
        self.assertIs(self.resident.parts['bob'], self.old)
        self.assertTrue(self.peer.release_broadcast_reply.is_set())

    def test_wallet_or_metadata_drift_fails(self):
        for part in ('wallet', 'metadata'):
            with self.subTest(part=part):
                self.build()
                drifted = copy.deepcopy(SNAP)
                drifted[part]['digest'] = '9' * 64
                self.snapshots = [SNAP, drifted]
                with self.assertRaisesRegex(RuntimeError, 'changed saved wallet'):
                    self.interrupt()

    def test_retained_field_drift_or_old_approval_fails_before_recovery(self):
        restored = dict(PENDING, operation='', review={}, reviewOperation='', result={})
        for changed, message in ((dict(restored, retainedTxid='0' * 64), 'changed retained local funding: retainedTxid'),
                (dict(restored, retainedReview={'changeAddress': 'x'}), 'changed retained local funding: retainedReview'),
                (dict(restored, review=PENDING['review']), 'reused an old approval'),
                (dict(restored, reviewOperation='send-review'), 'reused an old approval')):
            with self.subTest(message=message):
                self.build()
                self.new.responses['light-select'] = (True, state(changed))
                with self.assertRaisesRegex(RuntimeError, message):
                    self.interrupt()
                self.assertNotIn(('bob', 'light-resume-recovery'), self.log)

    def test_second_broadcast_during_recovery_fails(self):
        original = self.new.call

        def call(role, operation, **fields):
            if operation == 'light-resume-recovery':
                self.peer.accept(SIGNED)
            return original(role, operation, **fields)
        self.new.call = call
        with self.assertRaisesRegex(RuntimeError, 'broadcast the funding again'):
            self.interrupt()

    def test_old_response_appearing_after_reopen_fails(self):
        self.response = lambda path: ('vm', 'reopen') in self.log
        with self.assertRaisesRegex(RuntimeError, 'Old Bob relay response appeared after reopen'):
            self.interrupt()


class ReopenTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.local = private_dir(pathlib.Path(self.temp.name) / 'resident-bob')

    def tearDown(self):
        self.temp.cleanup()

    def part(self, control):
        return mock.Mock(host='vm-alias', binary=pathlib.PurePosixPath('/vm/bin/dex_tests'),
            tls_root=pathlib.PurePosixPath('/vm/trade/light-tls/root.pem'),
            preferences=pathlib.PurePosixPath('/vm/trade/resident/preferences'), processes=[],
            local_directory=self.local, ssh=('ssh', '-F', '/home/me/.ssh/vm-config'), control=control,
            ready_timeout=420, poll_seconds=15, response_limit=524288, request_limit=1048576,
            connect_timeout=15, transport_retries=3, spec=['host', 'binary', 'tls_root', 'preferences', 'processes',
                'local_directory', 'ssh', 'control', 'ready_timeout', 'poll_seconds', 'response_limit',
                'request_limit', 'connect_timeout', 'transport_retries'])

    def test_preserves_transport_and_resident_settings(self):
        prepared = {'generation': 2, 'directory': '/vm/trade/resident-resumed-2',
            'fixture': '/vm/trade/session-resume-2.json'}
        for control, multiplex in ((None, False), (str(self.local / 'ssh-%C'), True)):
            prepared['generation'] += 1
            part = self.part(control)
            with mock.patch.object(rlr, 'RemoteLitecoinAppFixture') as fixture:
                rlr.reopen(part, prepared, 1234)
            args, kwargs = fixture.call_args
            self.assertEqual(args, ('vm-alias', part.binary, prepared['fixture'], prepared['directory'], part.processes,
                self.local / f"remote-resumed-{prepared['generation']}"))
            self.assertEqual(kwargs['ssh'], part.ssh)
            self.assertIs(kwargs['multiplex'], multiplex)
            self.assertEqual((kwargs['tls_root'], kwargs['preferences'], kwargs['generation'], kwargs['function_timeout_ms']),
                (part.tls_root, part.preferences, prepared['generation'], 1234))
            self.assertEqual(stat.S_IMODE(args[5].stat().st_mode), 0o700)

    def test_refuses_to_reuse_a_local_log_directory(self):
        private_dir(self.local / 'remote-resumed-1')
        with mock.patch.object(rlr, 'RemoteLitecoinAppFixture') as fixture:
            with self.assertRaises(FileExistsError):
                rlr.reopen(self.part(None), {'generation': 1, 'directory': '/d', 'fixture': '/f'}, 1)
        fixture.assert_not_called()


class ReleasePasswordTests(unittest.TestCase):
    def test_uses_the_current_resident_path(self):
        workspace = mock.Mock()
        workspace.path = lambda name: '/vm/trade/' + name
        light = mock.Mock(spec=['resident_password_path'], resident_password_path='/vm/trade/resident-resumed-2/light-password')
        rlr.release_password(workspace, light)
        self.assertEqual(workspace.run.call_args[0][0][-1], '/vm/trade/resident-resumed-2/light-password')
        rlr.release_password(workspace, mock.Mock(spec=[]))
        self.assertEqual(workspace.run.call_args[0][0][-1], '/vm/trade/resident/light-password')


if __name__ == '__main__':
    unittest.main()
