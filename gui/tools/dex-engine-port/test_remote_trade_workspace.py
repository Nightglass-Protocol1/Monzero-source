import pathlib
import shlex
import subprocess
import unittest
from unittest.mock import patch

from remote_trade_workspace import RemoteTradeWorkspace


class RemoteWorkspaceTests(unittest.TestCase):
    def bare(self):
        obj = RemoteTradeWorkspace.__new__(RemoteTradeWorkspace)
        obj.root = pathlib.PurePosixPath('/home/test/trade with spaces')
        obj.ssh = ['ssh', '-o', 'BatchMode=yes', 'lab-vm']
        obj.host, obj.processes = 'lab-vm', []
        return obj

    def test_workspace_cannot_escape(self):
        obj = self.bare()
        for bad in ('/tmp/key', '../key', 'x/../../key'):
            with self.subTest(bad=bad), self.assertRaises(ValueError):
                obj.path(bad)
        self.assertEqual(obj.path('kit/card.json'), '/home/test/trade with spaces/kit/card.json')

    def test_arguments_are_shell_quoted_and_secrets_use_stdin(self):
        obj = self.bare()
        args = ['python3', '-c', 'print("$HOME")', "a'; touch /tmp/nope; '"]
        with patch('remote_trade_workspace.subprocess.run') as run:
            run.return_value = subprocess.CompletedProcess([], 0, b'ok', b'')
            self.assertEqual(obj.run(args, b'private bytes'), b'ok')
            call = run.call_args
            self.assertEqual(shlex.split(call.args[0][-1]), args)
            self.assertEqual(call.kwargs['input'], b'private bytes')
            self.assertNotIn('private bytes', repr(call.args))

    def test_failed_command_does_not_return_output_as_success(self):
        with patch('remote_trade_workspace.subprocess.run') as run:
            run.return_value = subprocess.CompletedProcess([], 1, b'partial', b'failed')
            with self.assertRaises(RuntimeError):
                self.bare().run(['false'])

    def test_host_options_are_rejected_before_execution(self):
        for host in ('', '-oProxyCommand=bad', 'host with space'):
            with self.subTest(host=host), self.assertRaises(ValueError):
                RemoteTradeWorkspace(host, pathlib.Path('/trade'), '/binary', '/helper', [])

    def test_forward_requires_ssh_acknowledgement_not_just_a_listener(self):
        obj = self.bare()
        with patch('remote_trade_workspace.subprocess.run') as run, patch.object(obj, 'run') as remote:
            run.return_value = subprocess.CompletedProcess([], 255, b'', b'remote port forwarding failed')
            with self.assertRaisesRegex(RuntimeError, 'setup rejected'):
                obj.forward([19332])
            remote.assert_not_called()
            self.assertEqual(obj.processes, [])

    def test_forward_owns_a_control_master_and_stops_it(self):
        obj = self.bare()
        with patch('remote_trade_workspace.subprocess.run') as run, patch.object(obj, 'run'):
            run.return_value = subprocess.CompletedProcess([], 0, b'', b'')
            process = obj.forward([19332])
            setup = run.call_args_list[0].args[0]
            self.assertIn('-f', setup)
            self.assertIn('ExitOnForwardFailure=yes', setup)
            self.assertIn('ControlMaster=yes', setup)
            self.assertEqual(process.poll(), None)
            process.terminate()
            self.assertEqual(run.call_args.args[0][-3:], ['-O', 'exit', 'lab-vm'])
            self.assertEqual(process.wait(timeout=1), 0)


if __name__ == '__main__':
    unittest.main()
