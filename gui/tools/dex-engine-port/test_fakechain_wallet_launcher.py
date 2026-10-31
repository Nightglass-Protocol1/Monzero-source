#!/usr/bin/python3
"""Launcher boundary checks; no daemon, wallet, or child process is started."""
import json
import os
import pathlib
import tempfile
import unittest
from fakechain_wallet_launcher import GENESIS, checked_command


class LauncherTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="xmz-launcher-test-")
        self.addCleanup(self.temp.cleanup)
        self.root = pathlib.Path(self.temp.name)
        self.service = self.root / "service"
        self.service.mkdir(mode=0o700)
        (self.service / "wallets").mkdir(mode=0o700)
        self.manifest = self.root / "fakechain-wallet-launch.json"
        self.binary = pathlib.Path("/usr/bin/true").resolve(strict=True)
        self.manifest.write_text(json.dumps({"schema": "monzero-fakechain-launch-v1",
            "walletRpc": str(self.binary), "daemonPort": 12345}))
        self.manifest.chmod(0o600)
        self.args = ["--wallet-dir", str(self.service / "wallets"), "--daemon-address", "127.0.0.1:12345",
            "--rpc-bind-ip", "127.0.0.1", "--rpc-bind-port", "12346", "--non-interactive",
            "--max-concurrency", "2", "--log-file", str(self.service / "rpc.log")]
        self.info = {"nettype": "fakechain", "offline": True, "incoming_connections_count": 0,
                     "outgoing_connections_count": 0}
        self.genesis = GENESIS

    def probe(self, method, params):
        return self.info if method == "get_info" else {"block_header": {"hash": self.genesis}}

    def test_verified_fixture_only(self):
        command = checked_command(self.args, self.probe)
        self.assertEqual(command, [str(self.binary), *self.args,
                                  "--allow-mismatched-daemon-version", "--trusted-daemon"])

    def test_rejects_chain_and_connectivity(self):
        for key, value in (("nettype", "mainnet"), ("offline", False),
                           ("incoming_connections_count", 1), ("outgoing_connections_count", 1)):
            with self.subTest(key=key):
                original = self.info[key]
                self.info[key] = value
                with self.assertRaises(ValueError):
                    checked_command(self.args, self.probe)
                self.info[key] = original
        self.genesis = "0" * 64
        with self.assertRaises(ValueError):
            checked_command(self.args, self.probe)

    def test_rejects_endpoint_and_path_changes(self):
        for index, value in ((3, "192.168.0.1:12345"), (5, "0.0.0.0"), (7, "12345"),
                             (10, "8"), (12, str(self.root / "rpc.log"))):
            with self.subTest(index=index):
                args = list(self.args)
                args[index] = value
                with self.assertRaises(ValueError):
                    checked_command(args, self.probe)

    def test_rejects_unsafe_manifest(self):
        self.manifest.chmod(0o644)
        with self.assertRaises(ValueError):
            checked_command(self.args, self.probe)
        self.manifest.chmod(0o600)
        linked = self.root / "linked"
        os.link(self.manifest, linked)
        with self.assertRaises(ValueError):
            checked_command(self.args, self.probe)
        linked.unlink()
        original = self.manifest.read_bytes()
        self.manifest.write_bytes(original[:-1] + b',"daemonPort":12345}')
        with self.assertRaises(ValueError):
            checked_command(self.args, self.probe)
        self.manifest.unlink()
        linked.write_bytes(original)
        self.manifest.symlink_to(linked)
        with self.assertRaises(OSError):
            checked_command(self.args, self.probe)


if __name__ == "__main__":
    unittest.main()
