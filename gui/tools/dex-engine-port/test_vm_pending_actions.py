import pathlib,tempfile,unittest
from unittest.mock import patch
from vm_lab_control import LabVmControl
from vm_power_loss import VM_UUID,VM_NAME

class PendingActionTests(unittest.TestCase):
    def setUp(self):
        tmp=tempfile.TemporaryDirectory();self.addCleanup(tmp.cleanup)
        self.root=pathlib.Path(tmp.name);self.proc=self.root/'123';self.proc.mkdir()
        self.command=self.proc/'cmdline'
    def check(self):
        original=pathlib.Path
        with patch('pathlib.Path',side_effect=lambda p:self.root if p=='/proc' else original(p)):
            LabVmControl().require_no_pending_power_commands()
    def test_pending_uuid_or_name_command_refused(self):
        for name in (VM_UUID,VM_NAME):
            self.command.write_bytes(b'\0'.join(x.encode() for x in ('/usr/bin/VBoxManage','controlvm',name,'poweroff'))+b'\0')
            with self.assertRaisesRegex(RuntimeError,'still running'):self.check()
    def test_foreign_vm_is_not_our_pending_action(self):
        self.command.write_bytes(b'/usr/bin/VBoxManage\0controlvm\0other\0poweroff\0')
        self.check()
    def test_disappeared_process_is_harmless(self):
        self.check()

if __name__=='__main__':unittest.main()
