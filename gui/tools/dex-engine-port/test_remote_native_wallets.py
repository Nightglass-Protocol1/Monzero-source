import hashlib
import http.server
import json
import os
import pathlib
import stat
import subprocess
import sys
import tempfile
import threading
import unittest
import urllib.request

import remote_native_wallets as rnw
from remote_native_wallets import RemoteNativeWallets, RemoteWalletProcess
from remote_trade_workspace import RemoteTradeWorkspace

BINARY = '/opt/monzero/bin/monzero-wallet-rpc'


class FakeWorkspace:
    """Scripted VM: records every command, returns queued outputs."""
    def __init__(self, outputs=()):
        self.root = pathlib.PurePosixPath('/home/vm/monzero-dex-trades/trade one')
        self.processes, self.calls, self.outputs = [], [], list(outputs)

    path = RemoteTradeWorkspace.path

    def run(self, argv, data=None, timeout=60):
        self.calls.append((argv, data, timeout))
        output = self.outputs.pop(0)
        if isinstance(output, Exception):
            raise output
        return output


class LocalWorkspace:
    """Runs the VM-side scripts on this machine, failing like RemoteTradeWorkspace.run."""
    def __init__(self, root):
        self.root, self.processes, self.calls = pathlib.Path(root), [], []

    path = RemoteTradeWorkspace.path

    def run(self, argv, data=None, timeout=60):
        self.calls.append((argv, data))
        argv = [sys.executable if argv[0] == 'python3' else argv[0], '-I'] + list(argv[1:])
        result = subprocess.run(argv, input=data, capture_output=True, timeout=timeout)
        if result.returncode:
            raise RuntimeError('VM command failed: ' + result.stderr.decode(errors='replace')[-2000:])
        return result.stdout


def launched(pid, port):
    return json.dumps({'pid': pid, 'starttime': str(9000 + pid), 'port': port}).encode()


def write_private(path, text):
    with os.fdopen(os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600), 'w') as out:
        out.write(text)


def starttime(pid):
    return pathlib.Path(f'/proc/{pid}/stat').read_text().rsplit(')', 1)[1].split()[19]


class StartTests(unittest.TestCase):
    def started(self, stage='regtest'):
        workspace = FakeWorkspace([launched(101, 30001), launched(102, 30002), launched(103, 30003), b'', b''])
        wallets = RemoteNativeWallets(workspace, BINARY, 18081, stage)
        return workspace, wallets, wallets.start()

    def test_start_returns_only_kit_fields_with_vm_paths(self):
        workspace, wallets, config = self.started()
        root = '/home/vm/monzero-dex-trades/trade one/native-wallets/'
        self.assertEqual(config, {
            'daemonPort': 18081,
            'claimWalletPort': 30001, 'claimLoginFile': root + 'claim/monzero-wallet-rpc.30001.login',
            'observerWalletPort': 30002, 'observerLoginFile': root + 'observer/monzero-wallet-rpc.30002.login',
            'walletPasswordFile': root + 'wallet-password'})
        self.assertEqual(wallets.services['recipient'], (30003, root + 'recipient/monzero-wallet-rpc.30003.login'))

    def test_each_service_is_launched_privately_and_owned(self):
        workspace, _, _ = self.started('testnet')
        launches = [argv for argv, _, _ in workspace.calls[:3]]
        for role, argv in zip(rnw.ROLES, launches):
            self.assertEqual(argv[:3], ['python3', '-c', rnw.LAUNCH])
            self.assertEqual(argv[3:], [BINARY, workspace.path('native-wallets/' + role), '18081', 'testnet'])
        self.assertEqual([(p.role, p.remote_pid, p.starttime) for p in workspace.processes],
                         [('claim', 101, '9101'), ('observer', 102, '9102'), ('recipient', 103, '9103')])
        self.assertTrue(all(p.monzero_owned_group is False for p in workspace.processes))
        for needle in ('"127.0.0.1"', 'mode=0o700', '"--testnet"'):
            self.assertIn(needle, rnw.LAUNCH)

    def test_passwords_are_generated_on_the_vm(self):
        workspace, _, _ = self.started()
        generated = workspace.calls[3:]
        self.assertEqual([argv[3] for argv, _, _ in generated],
                         [workspace.path('native-wallets/wallet-password'),
                          workspace.path('native-wallets/recipient-password')])
        self.assertTrue(all(argv[:3] == ['python3', '-c', rnw.GENERATE] for argv, _, _ in generated))
        self.assertTrue(all(data is None for _, data, _ in workspace.calls), 'nothing is sent to the VM on stdin')

    def test_arguments_are_rejected_before_touching_the_vm(self):
        workspace = FakeWorkspace()
        for binary, port, stage in ((BINARY, 18081, 'mainnet'), (BINARY, 0, 'regtest'), (BINARY, True, 'regtest'),
                                    (BINARY, '18081', 'regtest'), ('bin/wallet-rpc', 18081, 'regtest'),
                                    ('/opt/a b', 18081, 'regtest'), ('/opt/a\nb', 18081, 'regtest')):
            with self.subTest(binary=binary, port=port, stage=stage), self.assertRaises(ValueError):
                RemoteNativeWallets(workspace, binary, port, stage)
        self.assertEqual(workspace.calls, [])

    def test_partial_start_leaves_launched_services_for_cleanup(self):
        workspace = FakeWorkspace([launched(101, 30001), RuntimeError('VM command failed')])
        with self.assertRaises(RuntimeError):
            RemoteNativeWallets(workspace, BINARY, 18081, 'regtest').start()
        self.assertEqual([p.role for p in workspace.processes], ['claim', 'observer'])

    def test_invalid_launch_reports_keep_cleanup_registration(self):
        for report, error in (({'pid': 1, 'starttime': '5', 'port': 30001}, ValueError),
                              ({'pid': 101, 'starttime': '5 6', 'port': 30001}, ValueError),
                              ({'pid': 101, 'starttime': '5', 'port': 70000}, RuntimeError)):
            workspace = FakeWorkspace([json.dumps(report).encode()])
            with self.subTest(report=report), self.assertRaises(error):
                RemoteNativeWallets(workspace, BINARY, 18081, 'regtest').start()
            self.assertEqual(len(workspace.processes), 1)
            self.assertEqual(workspace.processes[0].registry, workspace.path("native-wallets/claim/process.json"))

    def test_start_twice_is_refused(self):
        _, wallets, _ = self.started()
        with self.assertRaises(RuntimeError):
            wallets.start()


class RecipientRpcTests(unittest.TestCase):
    def wallets(self, outputs):
        workspace = FakeWorkspace(outputs)
        wallets = RemoteNativeWallets(workspace, BINARY, 18081, 'regtest')
        wallets.services['recipient'] = (30003, workspace.path('native-wallets/recipient/monzero-wallet-rpc.30003.login'))
        return workspace, wallets

    def test_only_bounded_methods_are_reachable(self):
        workspace, wallets = self.wallets([])
        for method in ('query_key', 'get_mnemonic', 'export_outputs', 'export_key_images', 'transfer',
                       'sweep_all', 'restore_deterministic_wallet', 'stop_wallet'):
            with self.subTest(method=method), self.assertRaises(ValueError):
                wallets.rpc(method)
        self.assertEqual(workspace.calls, [])
        self.assertFalse(any('key' in m or 'seed' in m or 'mnemonic' in m or 'transfer' == m
                             for m in rnw.RECIPIENT_METHODS))
        self.assertIn('ALLOWED=' + repr(sorted(rnw.RECIPIENT_METHODS)), rnw.RPC)

    def test_password_and_login_are_referenced_by_vm_path_only(self):
        workspace, wallets = self.wallets([b'{}', b'{}', b'{"address":"4abc"}'])
        wallets.create_recipient('recipient-1')
        wallets.open_recipient('recipient-1')
        self.assertEqual(wallets.recipient_address(), '4abc')
        login = workspace.path('native-wallets/recipient/monzero-wallet-rpc.30003.login')
        secret = workspace.path('native-wallets/recipient-password')
        (create, create_data, _), (open_, open_data, _), (address, address_data, _) = workspace.calls
        self.assertEqual(create[3:7], ['30003', login, secret, 'create_wallet'])
        self.assertEqual(open_[3:7], ['30003', login, secret, 'open_wallet'])
        self.assertEqual(address[3:7], ['30003', login, '', 'get_address'])
        self.assertEqual(json.loads(create_data), {'filename': 'recipient-1', 'language': 'English'})
        self.assertEqual(json.loads(open_data), {'filename': 'recipient-1'})
        self.assertEqual(json.loads(address_data), {'account_index': 0})

    def test_names_and_txids_are_bounded(self):
        workspace, wallets = self.wallets([])
        for name in ('', '../claim/x', '-x', 'a b', 'a' * 65, None):
            with self.subTest(name=name), self.assertRaises(ValueError):
                wallets.open_recipient(name)
        for txid in ('', 'A' * 64, 'a' * 63, 'g' * 64, None):
            with self.subTest(txid=txid), self.assertRaises(ValueError):
                wallets.recipient_transfer(txid)
        self.assertEqual(workspace.calls, [])

    def test_claim_balance_uses_vm_claim_login_and_only_returns_balance(self):
        workspace, wallets = self.wallets([b'{"balance":200}'])
        login = workspace.path('native-wallets/claim/private.login')
        wallets.services['claim'] = (30001, login)
        self.assertEqual(wallets.claim_balance(), 200)
        self.assertEqual(workspace.calls[0][0][3:], ['30001', login])
        self.assertIsNone(workspace.calls[0][1])

    def test_balance_and_transfer_results(self):
        workspace, wallets = self.wallets([b'{"balance":5,"unlocked_balance":3,"per_subaddress":[]}',
                                           b'{"transfer":{"amount":5,"confirmations":2}}', b'{"blocks_fetched":1}'])
        self.assertEqual(wallets.recipient_balance(), {'balance': 5, 'unlocked_balance': 3})
        self.assertEqual(wallets.recipient_transfer('ab' * 32), {'amount': 5, 'confirmations': 2})
        self.assertEqual(wallets.refresh_recipient(), {'blocks_fetched': 1})
        self.assertEqual(workspace.calls[2][0][7], '600')


class ProcessHandleTests(unittest.TestCase):
    def test_poll_terminate_kill_and_wait(self):
        workspace = FakeWorkspace([b'running\n', b'running\n', b'exited\n'])
        process = RemoteWalletProcess(workspace, 'claim', 101, '9101')
        self.assertIsNone(process.poll())
        process.terminate()
        self.assertEqual(workspace.calls[1][0][3:], ['101', '9101', 'TERM', '0'])
        self.assertEqual(process.wait(timeout=5), 0)
        process.terminate(); process.kill()
        self.assertEqual(len(workspace.calls), 3, 'an exited process is never signalled again')

    def test_wait_is_bounded(self):
        process = RemoteWalletProcess(FakeWorkspace([b'running\n']), 'observer', 101, '9101')
        with self.assertRaises(subprocess.TimeoutExpired):
            process.wait(timeout=0)

    def test_unexpected_state_and_survivors_are_errors(self):
        with self.assertRaises(RuntimeError):
            RemoteWalletProcess(FakeWorkspace([b'maybe\n']), 'claim', 101, '9101').poll()
        process = RemoteWalletProcess(FakeWorkspace([b'running\n']), 'claim', 101, '9101')
        with self.assertRaisesRegex(RuntimeError, 'survived'):
            process.stop(grace=1)

    def test_stop_all_signals_only_wallet_services_in_reverse(self):
        workspace = FakeWorkspace([b'exited\n', b'exited\n'])
        other = object()
        workspace.processes += [RemoteWalletProcess(workspace, 'claim', 101, '1'), other,
                                RemoteWalletProcess(workspace, 'observer', 102, '2')]
        RemoteNativeWallets(workspace, BINARY, 18081, 'regtest').stop(grace=3)
        self.assertEqual([argv[3:] for argv, _, _ in workspace.calls],
                         [['102', '2', 'stop', '3'], ['101', '1', 'stop', '3']])

    def test_cleanup_continues_after_one_service_fails(self):
        workspace = FakeWorkspace([RuntimeError('transport unavailable'), b'exited\n'])
        workspace.processes = [RemoteWalletProcess(workspace, 'claim', 101, '1'),
                               RemoteWalletProcess(workspace, 'observer', 102, '2')]
        with self.assertRaisesRegex(RuntimeError, 'observer'):
            RemoteNativeWallets(workspace, BINARY, 18081, 'regtest').stop(grace=0)
        self.assertEqual(len(workspace.calls), 2)
        self.assertEqual(workspace.processes[0].returncode, 0)


@unittest.skipUnless(sys.platform.startswith('linux'), 'needs /proc')
class VmScriptTests(unittest.TestCase):
    """Executes the VM-side scripts locally against local stand-ins."""
    def setUp(self):
        directory = tempfile.TemporaryDirectory(prefix='mz-native-wallets-')
        self.addCleanup(directory.cleanup)
        self.workspace = LocalWorkspace(directory.name)

    def spawn(self, own_group=True):
        child = subprocess.Popen(['sleep', '60'], start_new_session=own_group)
        self.addCleanup(lambda: (child.poll() is None and child.kill(), child.wait()))
        return child

    def test_owned_group_is_stopped_with_term(self):
        child = self.spawn()
        process = RemoteWalletProcess(self.workspace, 'claim', child.pid, starttime(child.pid))
        self.assertIsNone(process.poll())
        process.stop(grace=5)
        self.assertEqual(child.wait(timeout=5), -15)

    def test_registered_handle_stops_service_without_launch_reply(self):
        child = self.spawn()
        process = RemoteWalletProcess.registered(self.workspace, 'claim')
        record = pathlib.Path(process.registry)
        record.parent.mkdir(parents=True, mode=0o700)
        write_private(record, json.dumps({'pid': child.pid, 'starttime': starttime(child.pid), 'bootId': pathlib.Path('/proc/sys/kernel/random/boot_id').read_text().strip()}))
        self.assertIsNone(process.remote_pid)
        process.stop(grace=2)
        self.assertEqual(child.wait(timeout=5), -15)

    def test_cleanup_before_record_cancels_delayed_launch(self):
        process = RemoteWalletProcess.registered(self.workspace, 'claim')
        process.stop(grace=0)
        self.assertEqual(process.returncode, 0)
        directory = str(pathlib.Path(process.registry).parent)
        with self.assertRaises(RuntimeError):
            self.workspace.run(['python3', '-c', rnw.LAUNCH, '/bin/true', directory, '18081', 'regtest'])
        self.assertFalse(pathlib.Path(process.registry).exists())
        self.assertTrue((pathlib.Path(directory) / 'cancelled').exists())

    def test_failed_record_write_never_executes_wallet(self):
        directory = self.workspace.path('native-wallets/claim')
        marker = pathlib.Path(self.workspace.root) / 'executed'
        binary = pathlib.Path(self.workspace.root) / 'fake-wallet'
        binary.write_text('#!/bin/sh\ntouch "' + str(marker) + '"\n')
        binary.chmod(0o700)
        broken = rnw.LAUNCH.replace('json.dump(state,f);', 'raise OSError("record failure");')
        with self.assertRaises(RuntimeError):
            self.workspace.run(['python3', '-c', broken, str(binary), directory, '18081', 'regtest'])
        self.assertFalse(marker.exists())

    def test_registry_reused_pid_is_never_signalled(self):
        child = self.spawn()
        process = RemoteWalletProcess.registered(self.workspace, 'claim')
        record = pathlib.Path(process.registry)
        record.parent.mkdir(parents=True, mode=0o700)
        write_private(record, json.dumps({'pid': child.pid, 'starttime': str(int(starttime(child.pid)) + 1), 'bootId': pathlib.Path('/proc/sys/kernel/random/boot_id').read_text().strip()}))
        process.stop(grace=0)
        self.assertIsNone(child.poll())

    def test_record_from_previous_boot_never_signals_matching_pid(self):
        child = self.spawn()
        process = RemoteWalletProcess.registered(self.workspace, 'claim')
        record = pathlib.Path(process.registry)
        record.parent.mkdir(parents=True, mode=0o700)
        write_private(record, json.dumps({'pid': child.pid, 'starttime': starttime(child.pid),
                                         'bootId': '00000000-0000-0000-0000-000000000000'}))
        process.stop(grace=0)
        self.assertEqual(process.returncode, 0)
        self.assertIsNone(child.poll())

    def test_registry_symlink_is_refused(self):
        child = self.spawn()
        process = RemoteWalletProcess.registered(self.workspace, 'claim')
        record = pathlib.Path(process.registry)
        record.parent.mkdir(parents=True, mode=0o700)
        target = record.parent / 'other.json'
        write_private(target, json.dumps({'pid': child.pid, 'starttime': starttime(child.pid), 'bootId': pathlib.Path('/proc/sys/kernel/random/boot_id').read_text().strip()}))
        record.symlink_to(target)
        with self.assertRaises(RuntimeError):
            process.stop(grace=0)
        self.assertIsNone(child.poll())

    def test_reused_pid_is_never_signalled(self):
        child = self.spawn()
        process = RemoteWalletProcess(self.workspace, 'claim', child.pid, str(int(starttime(child.pid)) + 1))
        process.stop(grace=1)
        self.assertEqual(process.returncode, 0)
        self.assertIsNone(child.poll(), 'a different process with the same pid survives')

    def test_changed_process_group_is_refused(self):
        child = self.spawn(own_group=False)
        process = RemoteWalletProcess(self.workspace, 'claim', child.pid, starttime(child.pid))
        with self.assertRaisesRegex(RuntimeError, 'process group changed'):
            process.terminate()
        self.assertIsNone(child.poll())

    def test_generated_password_is_private_and_never_replaced(self):
        target = self.workspace.path('native-wallets/wallet-password')
        self.workspace.run(['python3', '-c', rnw.GENERATE, target])
        first = pathlib.Path(target).read_text()
        self.assertRegex(first, r'^[0-9a-f]{64}$')
        self.assertEqual(stat.S_IMODE(os.stat(target).st_mode), 0o600)
        self.assertEqual(stat.S_IMODE(os.stat(os.path.dirname(target)).st_mode), 0o700)
        with self.assertRaises(RuntimeError):
            self.workspace.run(['python3', '-c', rnw.GENERATE, target])
        self.assertEqual(pathlib.Path(target).read_text(), first)

    def test_rpc_failures_do_not_reveal_credentials(self):
        login = self.workspace.path('rpc.login')
        write_private(login, 'user:login-secret-7f3a')
        with socket_port() as closed:
            wallets = RemoteNativeWallets(self.workspace, BINARY, 18081, 'regtest')
            wallets.services['recipient'] = (closed, login)
            with self.assertRaises(RuntimeError) as failure:
                wallets.recipient_balance()
        self.assertIn('Wallet RPC get_balance failed', str(failure.exception))
        self.assertNotIn('login-secret-7f3a', str(failure.exception))
        os.chmod(login, 0o644)
        with self.assertRaisesRegex(RuntimeError, 'Wallet RPC get_address failed'):
            wallets.recipient_address()

    def test_password_rpc_refuses_listener_owned_by_different_process(self):
        login,secret=self.workspace.path('rpc.login'),self.workspace.path('native-wallets/recipient-password')
        os.makedirs(os.path.dirname(secret),mode=0o700)
        write_private(login,'mzuser:login-secret');write_private(secret,'wallet-secret')
        server=digest_server('mzuser','login-secret')
        self.addCleanup(server.server_close);self.addCleanup(server.shutdown)
        other=self.spawn()
        write_private(self.workspace.path('process.json'),json.dumps({'pid':other.pid,'starttime':starttime(other.pid),
            'bootId':pathlib.Path('/proc/sys/kernel/random/boot_id').read_text().strip(),'port':server.server_address[1]}))
        wallets=RemoteNativeWallets(self.workspace,BINARY,18081,'regtest')
        wallets.services['recipient']=(server.server_address[1],login)
        for operation in (lambda:wallets.open_recipient('fixture'),wallets.recipient_address,wallets.recipient_balance):
            with self.assertRaises(RuntimeError):operation()
        self.assertEqual(server.all_requests,[])

    def test_password_rpc_never_reconnects_after_challenge_closes_socket(self):
        bodies=[]
        class Handler(http.server.BaseHTTPRequestHandler):
            protocol_version='HTTP/1.1'
            def log_message(self,*args):pass
            def do_POST(self):
                bodies.append(self.rfile.read(int(self.headers['Content-Length'])))
                self.send_response(401)
                self.send_header('WWW-Authenticate','Digest realm="monero-rpc", qop="auth", algorithm=MD5, nonce="fixture"')
                self.send_header('Content-Length','0');self.send_header('Connection','close');self.end_headers()
                self.close_connection=True
        server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Handler)
        threading.Thread(target=server.serve_forever,daemon=True).start()
        self.addCleanup(server.server_close);self.addCleanup(server.shutdown)
        login,secret=self.workspace.path('rpc.login'),self.workspace.path('native-wallets/recipient-password')
        os.makedirs(os.path.dirname(secret),mode=0o700)
        write_private(login,'mzuser:login-secret');write_private(secret,'wallet-secret')
        write_private(self.workspace.path('process.json'),json.dumps({'pid':os.getpid(),'starttime':starttime(os.getpid()),
            'bootId':pathlib.Path('/proc/sys/kernel/random/boot_id').read_text().strip(),'port':server.server_address[1]}))
        wallets=RemoteNativeWallets(self.workspace,BINARY,18081,'regtest');wallets.services['recipient']=(server.server_address[1],login)
        with self.assertRaises(RuntimeError):wallets.open_recipient('fixture')
        self.assertEqual(len(bodies),1)
        self.assertNotIn(b'wallet-secret',bodies[0])
        self.assertEqual(json.loads(bodies[0])['method'],'get_version')

    def test_digest_rpc_reads_login_and_password_on_the_vm_side(self):
        login, secret = self.workspace.path('rpc.login'), self.workspace.path('native-wallets/recipient-password')
        os.makedirs(os.path.dirname(secret), mode=0o700)
        write_private(login, 'mzuser:login-secret-7f3a')
        write_private(secret, 'wallet-secret-91c2')
        server = digest_server('mzuser', 'login-secret-7f3a')
        write_private(self.workspace.path('process.json'),json.dumps({'pid':os.getpid(),'starttime':starttime(os.getpid()),
            'bootId':pathlib.Path('/proc/sys/kernel/random/boot_id').read_text().strip(),'port':server.server_address[1]}))
        self.addCleanup(server.server_close)
        self.addCleanup(server.shutdown)
        wallets = RemoteNativeWallets(self.workspace, BINARY, 18081, 'regtest')
        wallets.services['recipient'] = (server.server_address[1], login)
        wallets.open_recipient('recipient-1')
        self.assertEqual(wallets.recipient_address(), '4test')
        self.assertEqual([r['params'] for r in server.requests],
                         [{'filename': 'recipient-1', 'password': 'wallet-secret-91c2'}, {'account_index': 0}])
        sent = repr(self.workspace.calls)
        self.assertNotIn('login-secret-7f3a', sent)
        self.assertNotIn('wallet-secret-91c2', sent)

    def test_wallet_rpc_does_not_follow_redirects(self):
        calls = []

        class Handler(http.server.BaseHTTPRequestHandler):
            def log_message(self, *args):
                pass

            def do_POST(self):
                self.rfile.read(int(self.headers['Content-Length']))
                calls.append(self.path)
                self.send_response(307)
                self.send_header('Location', '/redirect-target')
                self.send_header('Content-Length', '0')
                self.end_headers()

        server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
        threading.Thread(target=server.serve_forever, daemon=True).start()
        self.addCleanup(server.server_close)
        self.addCleanup(server.shutdown)
        login = self.workspace.path('rpc.login')
        write_private(login, 'mzuser:private-test-password')
        write_private(self.workspace.path('process.json'),json.dumps({'pid':os.getpid(),'starttime':starttime(os.getpid()),
            'bootId':pathlib.Path('/proc/sys/kernel/random/boot_id').read_text().strip(),'port':server.server_address[1]}))
        wallets = RemoteNativeWallets(self.workspace, BINARY, 18081, 'regtest')
        wallets.services['recipient'] = (server.server_address[1], login)
        with self.assertRaisesRegex(RuntimeError, 'Wallet RPC get_address failed'):
            wallets.recipient_address()
        self.assertEqual(calls, ['/json_rpc'])


class socket_port:
    """A loopback port with no listener."""
    def __enter__(self):
        import socket
        with socket.socket() as probe:
            probe.bind(('127.0.0.1', 0))
            return probe.getsockname()[1]

    def __exit__(self, *exc):
        return False


def digest_server(user, password):
    md5 = lambda text: hashlib.md5(text.encode()).hexdigest()

    class Handler(http.server.BaseHTTPRequestHandler):
        protocol_version = 'HTTP/1.1'

        def setup(self):
            super().setup()
            self.nonce = os.urandom(16).hex()

        def log_message(self, *args):
            pass

        def reply(self, code, body=b'', challenge=False):
            self.send_response(code)
            if challenge:
                self.send_header('WWW-Authenticate', 'Digest realm="monero-rpc", qop="auth", algorithm=MD5, nonce="' + self.nonce + '"')
            self.send_header('Content-Length', str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def do_POST(self):
            body = self.rfile.read(int(self.headers['Content-Length']))
            self.server.all_requests.append(body)
            auth = self.headers.get('Authorization', '')
            if not auth.startswith('Digest '):
                return self.reply(401, challenge=True)
            f = urllib.request.parse_keqv_list(urllib.request.parse_http_list(auth[7:]))
            expected = md5(':'.join([md5(f'{user}:monero-rpc:{password}'), f['nonce'], f['nc'], f['cnonce'],
                                     f['qop'], md5('POST:' + f['uri'])]))
            if f['username'] != user or f['response'] != expected or f['nonce'] != self.nonce:
                return self.reply(401, challenge=True)
            request = json.loads(body)
            self.server.requests.append(request)
            result = {'address': '4test'} if request['method'] == 'get_address' else {}
            self.reply(200, json.dumps({'jsonrpc': '2.0', 'id': request['id'], 'result': result}).encode())

    server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
    server.requests = []
    server.all_requests = []
    threading.Thread(target=server.serve_forever, daemon=True).start()
    return server


if __name__ == '__main__':
    unittest.main()
