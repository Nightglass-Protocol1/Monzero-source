import pathlib
import tempfile
import unittest
from unittest.mock import Mock,patch
from vm_lab_control import LabVmControl
from vm_power_loss import VM_UUID

class StartProcessTests(unittest.TestCase):
    def setUp(self):
        temp=tempfile.TemporaryDirectory();self.addCleanup(temp.cleanup)
        self.root=pathlib.Path(temp.name);self.proc=self.root/'123';self.proc.mkdir()
        self.fields=['S']+['0']*18+['456']
        self.write_stat()
        (self.proc/'exe').symlink_to('/usr/lib/virtualbox/VBoxHeadless')
        self.command=[b'/usr/lib/virtualbox/VBoxHeadless',b'--startvm',VM_UUID.encode(),b'--vrde',b'off']
        self.write_command()
        self.control=LabVmControl();self.control.started_unit='monzero-dexlab-recovery-'+'a'*32
        self.observed={'unit':self.control.started_unit,'LoadState':'loaded','ActiveState':'active','SubState':'running','MainPID':'123'}
        self.control.inspect_start_attempt=Mock(return_value=self.observed)
    def write_stat(self):
        (self.proc/'stat').write_text('123 (VBoxHeadless) '+' '.join(self.fields))
    def write_command(self):
        (self.proc/'cmdline').write_bytes(b'\0'.join(self.command)+b'\0')
    def verify(self):
        real_path=pathlib.Path
        with patch('pathlib.Path',side_effect=lambda x: self.root if x=='/proc' else real_path(x)):
            return self.control.require_live_start_attempt(self.observed)
    def test_exact_owned_process_and_unit(self):
        self.assertEqual(self.verify(),{'pid':123,'starttime':'456'})
    def test_wrong_vm_is_rejected(self):
        self.command[2]=b'other-vm';self.write_command()
        with self.assertRaisesRegex(RuntimeError,'another VM'):self.verify()
    def test_different_executable_is_rejected(self):
        (self.proc/'exe').unlink();(self.proc/'exe').symlink_to('/usr/bin/sleep')
        with self.assertRaisesRegex(RuntimeError,'VBoxHeadless'):self.verify()
    def test_zombie_is_rejected(self):
        self.fields[0]='Z';self.write_stat()
        with self.assertRaises(RuntimeError):self.verify()
    def test_unit_changed_during_check_is_rejected(self):
        self.control.inspect_start_attempt.return_value=dict(self.observed,MainPID='456')
        with self.assertRaisesRegex(RuntimeError,'changed'):self.verify()

if __name__=='__main__':unittest.main()
