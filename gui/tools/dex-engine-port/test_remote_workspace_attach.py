import hashlib
import json
import os
import pathlib
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
from remote_trade_workspace import RemoteTradeWorkspace, VERIFY_EXISTING

class AttachTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = pathlib.Path(self.tmp.name)
        self.file = self.root / 'identity'
        self.file.write_bytes(b'retained private fixture')
        self.file.chmod(0o600)
        self.expected = {'identity': hashlib.sha256(self.file.read_bytes()).hexdigest()}

    def verify(self, expected=None):
        return subprocess.run([sys.executable, '-c', VERIFY_EXISTING, str(self.root)],
            input=json.dumps(self.expected if expected is None else expected).encode(), capture_output=True)

    def test_preserves_existing_files(self):
        before = self.file.stat()
        self.assertEqual(self.verify().returncode, 0)
        after = self.file.stat()
        self.assertEqual((before.st_ino, before.st_mtime_ns), (after.st_ino, after.st_mtime_ns))
        self.assertEqual(list(self.root.iterdir()), [self.file])

    def test_changed_binding_rejected(self):
        self.file.write_bytes(b'replaced identity')
        self.assertNotEqual(self.verify().returncode, 0)

    def test_missing_binding_rejected(self):
        self.file.unlink()
        self.assertNotEqual(self.verify().returncode, 0)

    def test_public_file_rejected(self):
        self.file.chmod(0o644)
        self.assertNotEqual(self.verify().returncode, 0)

    def test_public_root_rejected(self):
        self.root.chmod(0o755)
        self.assertNotEqual(self.verify().returncode, 0)

    def test_symlink_rejected(self):
        target = self.root / 'target'
        self.file.rename(target)
        self.file.symlink_to(target)
        self.assertNotEqual(self.verify().returncode, 0)

    def test_hardlink_rejected(self):
        os.link(self.file, self.root / 'alias')
        self.assertNotEqual(self.verify().returncode, 0)

    def test_escape_empty_and_malformed_rejected(self):
        for expected in ({}, {'../identity': self.expected['identity']},
                         {'/identity': self.expected['identity']}, {'identity': 'bad'}):
            with self.subTest(expected=expected):
                self.assertNotEqual(self.verify(expected).returncode, 0)

    def test_attach_does_not_create_or_upload(self):
        with patch.object(RemoteTradeWorkspace, 'run', return_value=b'verified\n') as run, \
             patch.object(RemoteTradeWorkspace, 'upload') as upload:
            result = RemoteTradeWorkspace.attach_existing('lab-vm', str(self.root), '/bin/app', '/bin/helper', [], self.expected)
            self.assertEqual(result.root, self.root)
            self.assertEqual(run.call_count, 1)
            upload.assert_not_called()
            self.assertNotIn(b'retained private fixture', run.call_args.args[1])

    def test_no_binding_means_no_remote_action(self):
        with patch.object(RemoteTradeWorkspace, 'run') as run:
            with self.assertRaises(ValueError):
                RemoteTradeWorkspace.attach_existing('lab-vm', str(self.root), '/bin/app', '/bin/helper', [], {})
            run.assert_not_called()

if __name__ == '__main__':
    unittest.main()
