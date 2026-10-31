import pathlib,tempfile,unittest
from unittest.mock import patch
from vm_lab_lease import LabVmLease

class RegistryTests(unittest.TestCase):
    def setUp(self):
        tmp=tempfile.TemporaryDirectory();self.addCleanup(tmp.cleanup);self.root=pathlib.Path(tmp.name)
        self.checkpoint=self.root/'checkpoint';self.digest='a'*64
    def lease(self,exclusive=True):return LabVmLease(exclusive,self.root,lambda:[])
    def test_unfinished_record_survives_lease_and_rejects_different_run(self):
        with self.lease() as lease:lease.begin_recovery(self.checkpoint,self.digest)
        with self.lease() as lease:
            lease.require_recovery(self.checkpoint,self.digest)
            with self.assertRaisesRegex(RuntimeError,'unfinished'):lease.begin_recovery(self.root/'other','b'*64)
        with self.assertRaisesRegex(RuntimeError,'unfinished'):
            with self.lease(False):pass
    def test_only_owner_can_complete_then_new_trade_allowed(self):
        with self.lease() as lease:
            lease.begin_recovery(self.checkpoint,self.digest)
            with self.assertRaises(RuntimeError):lease.complete_recovery(self.checkpoint,'b'*64)
            lease.complete_recovery(self.checkpoint,self.digest)
        with self.lease(False):pass
        with self.lease() as lease:
            with self.assertRaisesRegex(RuntimeError,'already completed'):lease.begin_recovery(self.checkpoint,self.digest)
            lease.begin_recovery(self.root/'next','b'*64)
    def test_abandonment_write_failure_keeps_owner(self):
        with self.lease() as lease:
            lease.begin_recovery(self.checkpoint,self.digest)
            with patch('recovery_run_registry.persist_checkpoint',side_effect=OSError('disk full')):
                with self.assertRaises(OSError):lease.abandon_unissued_recovery(self.checkpoint,self.digest)
            lease.require_recovery(self.checkpoint,self.digest)

    def test_abandonment_marker_survives_interrupted_unlink(self):
        with self.lease() as lease:
            lease.begin_recovery(self.checkpoint,self.digest)
            with patch.object(lease.registry,'clear_completed',side_effect=OSError('interrupted')):
                with self.assertRaises(OSError):lease.abandon_unissued_recovery(self.checkpoint,self.digest)
        with self.lease() as lease:
            with self.assertRaisesRegex(RuntimeError,'already abandoned'):lease.begin_recovery(self.checkpoint,self.digest)
            self.assertIsNone(lease.registry.current())
        with self.lease(False):pass

    def test_quarantine_marker_retry_preserves_original_observation(self):
        with self.lease() as lease:
            lease.begin_recovery(self.checkpoint,self.digest)
            first={'retirementOnly':True,'vmState':'poweroff'}
            with patch.object(lease.registry,'clear_completed',side_effect=OSError('interrupted')):
                with self.assertRaises(OSError):lease.registry.quarantine(self.checkpoint,self.digest,first)
            lease.registry.quarantine(self.checkpoint,self.digest,{'retirementOnly':True,'vmState':'running'})
            self.assertIsNone(lease.registry.current())
            from remote_recovery_checkpoint import read_private_json
            value=lease.registry.binding(self.checkpoint,self.digest)
            self.assertEqual(read_private_json(lease.registry.quarantined(value))['observation'],first)

    def test_shared_lease_cannot_claim(self):
        with self.lease(False) as lease:
            with self.assertRaises(RuntimeError):lease.begin_recovery(self.checkpoint,self.digest)
    def test_symlink_active_record_rejected(self):
        with self.lease() as lease:
            lease.registry.active.symlink_to(self.root/'missing')
            with self.assertRaises(OSError):lease.registry.current()

if __name__=='__main__':unittest.main()
