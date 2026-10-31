import hashlib
import json
import pathlib
import tempfile
import unittest
from unittest.mock import patch
from test_remote_native_wallets import LocalWorkspace
from remote_recovery_checkpoint import capture_bindings,persist_checkpoint,RecoveryPhaseJournal,capture_native_wallets

class CheckpointTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.root=pathlib.Path(self.temp.name);self.ws=LocalWorkspace(self.root)
        self.secret=self.root/'secret';self.secret.write_bytes(b'fixture-secret');self.secret.chmod(0o600)
    def test_only_hash_returns(self):
        value=capture_bindings(self.ws,['secret'])
        self.assertEqual(value,{'secret':hashlib.sha256(b'fixture-secret').hexdigest()})
        self.assertNotIn('fixture-secret',json.dumps(value))
    def test_missing_input_fails(self):
        with self.assertRaises(RuntimeError):capture_bindings(self.ws,['missing'])
    def test_symlink_fails(self):
        (self.root/'link').symlink_to(self.secret)
        with self.assertRaises(RuntimeError):capture_bindings(self.ws,['link'])
    def test_public_file_fails(self):
        self.secret.chmod(0o644)
        with self.assertRaises(RuntimeError):capture_bindings(self.ws,['secret'])
    def test_explicit_public_signature_is_allowed_but_not_public_writes(self):
        path=self.root/'kit-ltc.sig';path.write_text('public signature');path.chmod(0o644)
        self.assertIn('kit-ltc.sig',capture_bindings(self.ws,['kit-ltc.sig']))
        path.chmod(0o666)
        with self.assertRaises(RuntimeError):capture_bindings(self.ws,['kit-ltc.sig'])
    def test_private_durable_checkpoint_does_not_overwrite(self):
        path=self.root/'checkpoint.json';persist_checkpoint(path,{'boot':'fixture'})
        self.assertEqual(path.stat().st_mode&0o777,0o600)
        with self.assertRaises(FileExistsError):persist_checkpoint(path,{'boot':'replacement'})
        self.assertEqual(json.loads(path.read_text()),{'boot':'fixture'})
    def test_fsync_failure_propagates(self):
        with patch('remote_recovery_checkpoint.os.fsync',side_effect=OSError('disk failure')):
            with self.assertRaises(OSError):persist_checkpoint(self.root/'checkpoint.json',{})
    def test_failed_file_fsync_never_publishes_final_name(self):
        target=self.root/'atomic.json'
        with patch('remote_recovery_checkpoint.os.fsync',side_effect=OSError('disk failure')):
            with self.assertRaises(OSError):persist_checkpoint(target,{'complete':True})
        self.assertFalse(target.exists())
        self.assertEqual(list(self.root.glob('.recovery-pending-*')),[])

    def test_phase_journal_is_private_ordered_and_not_recreated(self):
        path=self.root/'phases';journal=RecoveryPhaseJournal(path)
        journal.record('power-off-requested',{'oldBootId':'fixture'})
        journal.record('power-off-confirmed',{})
        files=sorted(path.iterdir());self.assertEqual(len(files),2)
        self.assertEqual([json.loads(p.read_text())['sequence'] for p in files],[0,1])
        self.assertTrue(all(p.stat().st_mode&0o777==0o600 for p in files))
        with self.assertRaises(FileExistsError):RecoveryPhaseJournal(path)

    def test_native_wallet_snapshot_detects_changed_or_missing_keys(self):
        native=self.root/'native-wallets';native.mkdir(mode=0o700)
        for role in ('claim','observer','recipient'):
            (native/role).mkdir(mode=0o700);(native/role/'wallets').mkdir(mode=0o700)
        key=native/'recipient/wallets/payout.keys';key.write_bytes(b'fixture key');key.chmod(0o600)
        before=capture_native_wallets(self.ws)
        key.write_bytes(b'changed key');self.assertNotEqual(before,capture_native_wallets(self.ws))
        # A cache or another role's wallet must not disguise lost payout keys.
        cache=native/'recipient/wallets/payout';cache.write_bytes(b'cache');cache.chmod(0o600)
        other=native/'claim/wallets/payout.keys';other.write_bytes(b'other wallet');other.chmod(0o600)
        key.unlink()
        with self.assertRaisesRegex(RuntimeError,'payout wallet keys are missing'):
            capture_native_wallets(self.ws)

    def test_public_parent_rejected(self):
        self.root.chmod(0o755)
        with self.assertRaises(ValueError):persist_checkpoint(self.root/'checkpoint.json',{})

if __name__=='__main__':unittest.main()
