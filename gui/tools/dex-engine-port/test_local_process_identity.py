import subprocess
import pathlib,time
from unittest.mock import patch
import unittest
from local_process_identity import local_process_identity,require_local_process,require_local_process_dead

class IdentityTests(unittest.TestCase):
    def setUp(self):
        self.process=subprocess.Popen(['sleep','60'])
        self.addCleanup(self.stop)
    def stop(self):
        if self.process.poll() is None:self.process.terminate()
        self.process.wait(timeout=5)
    def test_live_identity_roundtrip(self):
        value=local_process_identity(self.process)
        self.assertEqual(value['pid'],self.process.pid)
        require_local_process(self.process,value)
    def test_different_start_or_boot_rejected(self):
        value=local_process_identity(self.process)
        for field in ('pid','starttime','bootId'):
            changed=dict(value);changed[field]='other'
            with self.assertRaisesRegex(RuntimeError,'differs'):require_local_process(self.process,changed)
    def test_dead_proof_rejects_live_original(self):
        value=local_process_identity(self.process)
        with self.assertRaisesRegex(RuntimeError,'still alive'):require_local_process_dead(value)

    def test_dead_proof_accepts_prior_boot_or_reused_pid(self):
        value=local_process_identity(self.process)
        require_local_process_dead(dict(value,bootId='00000000-0000-0000-0000-000000000001'))
        require_local_process_dead(dict(value,starttime=str(int(value['starttime'])+1)))

    def test_dead_proof_accepts_zombie_without_signalling(self):
        value=local_process_identity(self.process);self.process.terminate()
        stat=pathlib.Path('/proc')/str(self.process.pid)/'stat'
        deadline=time.monotonic()+3
        while stat.read_text().rsplit(')',1)[1].split()[0]!='Z':
            if time.monotonic()>deadline:self.fail('Process did not become a zombie')
            time.sleep(.01)
        require_local_process_dead(value)

    def test_dead_proof_does_not_treat_unreadable_proc_as_absent(self):
        value=local_process_identity(self.process);original=pathlib.Path.read_text
        def read(path,*args,**kwargs):
            if str(path)=='/proc/'+str(self.process.pid)+'/stat':raise PermissionError('denied')
            return original(path,*args,**kwargs)
        with patch.object(pathlib.Path,'read_text',read):
            with self.assertRaises(PermissionError):require_local_process_dead(value)

    def test_dead_original_rejected(self):
        value=local_process_identity(self.process);self.stop()
        with self.assertRaisesRegex(RuntimeError,'not a live'):require_local_process(self.process,value)

if __name__=='__main__':unittest.main()
