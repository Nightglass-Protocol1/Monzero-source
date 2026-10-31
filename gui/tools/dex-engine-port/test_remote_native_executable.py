"""Real Linux process checks: an on-disk replacement cannot approve a running image."""
import fcntl
import hashlib
import json
import os
import pathlib
import shutil
import subprocess
import tempfile
import unittest
from remote_native_wallets import CALL, SERVICE_CHECKPOINT
from test_remote_native_wallets import LocalWorkspace


class ExecutableTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = pathlib.Path(self.temp.name)
        self.binary = self.root / 'wallet'
        shutil.copy2(shutil.which('sleep'), self.binary)
        self.child = subprocess.Popen([str(self.binary), '60'], start_new_session=True)
        self.addCleanup(self.stop)
        self.state = dict(pid=self.child.pid,
            starttime=pathlib.Path(f'/proc/{self.child.pid}/stat').read_text().rsplit(')',1)[1].split()[19],
            bootId=pathlib.Path('/proc/sys/kernel/random/boot_id').read_text().strip(),
            launch={'sha256':hashlib.sha256(self.binary.read_bytes()).hexdigest()})

    def stop(self):
        if self.child.poll() is None:self.child.terminate()
        self.child.wait(timeout=5)

    def check(self):
        return LocalWorkspace(self.root).run(['python3','-c',CALL+
            '\nimport sys\nverify_running_executable(json.load(sys.stdin))\n'],json.dumps(self.state).encode())

    def test_checkpoint_waits_for_all_role_locks_before_reading_records(self):
        native=self.root/'native-wallets';native.mkdir(mode=0o700)
        for role in ('claim','observer','recipient'):(native/role).mkdir(mode=0o700)
        # No process records exist: inspecting any role before obtaining all
        # locks would fail with a missing-file error instead of lock contention.
        fd=os.open(native/'recipient',os.O_RDONLY|os.O_DIRECTORY)
        try:
            fcntl.flock(fd,fcntl.LOCK_EX)
            with self.assertRaisesRegex(RuntimeError,'Wallet checkpoint lock busy'):
                LocalWorkspace(self.root).run(['python3','-c',SERVICE_CHECKPOINT,
                    str(native),str(self.binary),'1','testnet'],timeout=20)
        finally:os.close(fd)

    def test_matching_running_image(self):
        self.check()

    def test_replacement_path_does_not_change_running_image(self):
        new=self.root/'replacement';shutil.copy2(shutil.which('true'),new)
        os.replace(new,self.binary)
        self.check()  # Still the original mapped sleep, not the new file.
        self.state['launch']['sha256']=hashlib.sha256(self.binary.read_bytes()).hexdigest()
        with self.assertRaisesRegex(RuntimeError,'Running wallet executable mismatch'):self.check()
        self.assertIsNone(self.child.poll())

    def test_changed_starttime_is_rejected(self):
        self.state['starttime']='0'
        with self.assertRaisesRegex(RuntimeError,'identity changed'):self.check()

    def test_changed_boot_is_rejected(self):
        self.state['bootId']='00000000-0000-0000-0000-000000000000'
        with self.assertRaisesRegex(RuntimeError,'boot changed'):self.check()

    def test_missing_launch_binding_is_rejected(self):
        del self.state['launch']
        with self.assertRaisesRegex(RuntimeError,'executable mismatch'):self.check()

if __name__=='__main__':unittest.main()
