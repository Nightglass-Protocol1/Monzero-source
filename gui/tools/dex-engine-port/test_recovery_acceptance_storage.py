import pathlib,tempfile,unittest
from unittest.mock import patch
from recovery_acceptance_storage import require_recovery_storage

class StorageTests(unittest.TestCase):
    def test_explicit_parent_and_keep_are_required(self):
        for parent,keep in ((None,True),('/tmp',False)):
            with self.assertRaisesRegex(RuntimeError,'--keep'):require_recovery_storage(parent,keep)
    def test_memory_backed_storage_is_rejected(self):
        with tempfile.TemporaryDirectory() as folder:
            for kind in ('tmpfs','ramfs','','overlayfs','UNKNOWN (0x123)'):
                with patch('recovery_acceptance_storage.subprocess.check_output',return_value=kind):
                    with self.assertRaisesRegex(RuntimeError,'persistent storage'):require_recovery_storage(folder,True)
    def test_disk_path_is_retained(self):
        with tempfile.TemporaryDirectory() as folder:
            with patch('recovery_acceptance_storage.subprocess.check_output',return_value='btrfs\n'):
                self.assertEqual(require_recovery_storage(folder,True),pathlib.Path(folder).resolve())
    def test_unreadable_filesystem_does_not_default_to_safe(self):
        with tempfile.TemporaryDirectory() as folder:
            with patch('recovery_acceptance_storage.subprocess.check_output',side_effect=OSError('unavailable')):
                with self.assertRaises(OSError):require_recovery_storage(folder,True)

if __name__=='__main__':unittest.main()
