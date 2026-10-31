import pathlib,tempfile,unittest
from vm_lab_lease import LabVmLease

class LeaseTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.root=pathlib.Path(self.temp.name)
    def lease(self,exclusive,probe=lambda:[]):return LabVmLease(exclusive,self.root,probe)
    def test_shared_traders_block_power_loss(self):
        with self.lease(False),self.lease(False):
            with self.assertRaises(BlockingIOError):
                with self.lease(True):pass
    def test_exclusive_blocks_new_trader(self):
        with self.lease(True) as held:
            held.require_exclusive()
            with self.assertRaises(BlockingIOError):
                with self.lease(False):pass
        with self.lease(True):pass
    def test_legacy_trader_refuses_power_loss(self):
        with self.assertRaises(RuntimeError):
            with self.lease(True,lambda:[123]):pass
        with self.lease(True):pass
    def test_lost_or_shared_lease_cannot_authorize_power_loss(self):
        with self.lease(False) as held:
            with self.assertRaises(RuntimeError):held.require_exclusive()
        with self.assertRaises(RuntimeError):held.require_exclusive()
    def test_symlink_refused(self):
        target=self.root/'other';target.touch();(self.root/'monzero-dexlab.lock').symlink_to(target)
        with self.assertRaises(OSError):
            with self.lease(True):pass

if __name__=='__main__':unittest.main()
