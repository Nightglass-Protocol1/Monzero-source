"""Offline regression tests for integration-test RPC deadlines and diagnostics."""

import unittest
from unittest.mock import patch

import monzero_multinode as harness


class RPCDiagnostics(unittest.TestCase):
    def test_wallet_and_node_deadlines(self):
        with patch.object(harness, "request", return_value={"result": {}}) as request:
            harness.json_rpc(harness.WALLET_RPC_PORT, "create_wallet")
            self.assertEqual(request.call_args.kwargs["timeout"], 60.0)
            harness.json_rpc(harness.RPC_PORTS[0], "get_info")
            self.assertEqual(request.call_args.kwargs["timeout"], 5.0)

    def test_timeout_identifies_operation_without_parameters_or_retry(self):
        with patch.object(harness.urllib.request, "urlopen", side_effect=TimeoutError) as open_url:
            with self.assertRaises(harness.TestFailure) as caught:
                harness.json_rpc(harness.WALLET_RPC_PORT, "restore_deterministic_wallet", {
                    "seed": "PRIVATE_TEST_SEED", "password": "PRIVATE_TEST_PASSWORD",
                })
            message = str(caught.exception)
            self.assertIn("restore_deterministic_wallet", message)
            self.assertIn(str(harness.WALLET_RPC_PORT), message)
            self.assertNotIn("PRIVATE_TEST", message)
            open_url.assert_called_once()


if __name__ == "__main__":
    unittest.main()
