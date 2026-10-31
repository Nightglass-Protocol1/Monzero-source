import subprocess
import unittest
from unittest.mock import Mock
from test_litecoin_live_swap import stop_processes, recovered_native_balance


class CleanupTests(unittest.TestCase):
    def process(self):
        process = Mock()
        process.monzero_owned_group = False
        process.poll.return_value = None
        return process

    def test_poll_failure_does_not_skip_other_services(self):
        healthy, unavailable = self.process(), self.process()
        unavailable.poll.side_effect = RuntimeError('SSH unavailable')
        errors = stop_processes([healthy, unavailable])
        self.assertEqual(len(errors), 1)
        healthy.terminate.assert_called_once()
        healthy.wait.assert_called_once_with(timeout=20)

    def test_timeout_escalates_and_waits(self):
        process = self.process()
        process.wait.side_effect = [subprocess.TimeoutExpired('wallet', 20), 0]
        self.assertEqual(stop_processes([process]), [])
        process.kill.assert_called_once()
        self.assertEqual(process.wait.call_count, 2)

    def test_exited_process_is_not_signalled(self):
        process = self.process()
        process.poll.return_value = 0
        self.assertEqual(stop_processes([process]), [])
        process.terminate.assert_not_called()


class RecoveryRoutingTests(unittest.TestCase):
    def test_bob_uses_vm_claim_wallet(self):
        host, remote = Mock(), Mock()
        remote.claim_balance.return_value = 200
        self.assertEqual(recovered_native_balance(host, remote, "bob"), 200)
        host.assert_not_called()
        remote.claim_balance.assert_called_once()

    def test_alice_keeps_host_claim_wallet(self):
        host, remote = Mock(), Mock()
        host.return_value = {"balance": 200}
        self.assertEqual(recovered_native_balance(host, remote, "alice"), 200)
        remote.claim_balance.assert_not_called()
        self.assertEqual(host.call_args_list[-1].args, ("get_balance",))
