import json
import os
import signal
import time
import pathlib
import subprocess
import sys
import tempfile
import unittest
from litecoin_remote_fixture import APP_PROCESS_RECORD, APP_PROCESS_SIGNAL

class AppBootTests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.addCleanup(self.tmp.cleanup)
        self.record=pathlib.Path(self.tmp.name)/'app.process.json'
        self.child=subprocess.Popen(['sleep','60'],start_new_session=True)
        self.addCleanup(self.cleanup)
        subprocess.run([sys.executable,'-c',APP_PROCESS_RECORD,str(self.record),str(self.child.pid),str(self.child.pid)],check=True)
    def cleanup(self):
        if self.child.poll() is None:self.child.kill()
        self.child.wait(timeout=5)
    def send(self):
        return subprocess.run([sys.executable,'-c',APP_PROCESS_SIGNAL,str(self.record),'TERM'],capture_output=True)
    def change(self,**fields):
        value=json.loads(self.record.read_text());value.update(fields);self.record.write_text(json.dumps(value))
    def test_current_owned_group_stops(self):
        self.assertEqual(self.send().returncode,0)
        self.assertEqual(self.child.wait(timeout=5),-15)
    def test_previous_boot_never_signals_matching_pid(self):
        self.change(bootId='00000000-0000-0000-0000-000000000000')
        self.assertEqual(self.send().returncode,0);self.assertIsNone(self.child.poll())
    def test_reused_pid_never_signalled(self):
        self.change(groupStart='0',appStart='0')
        self.assertNotEqual(self.send().returncode,0);self.assertIsNone(self.child.poll())
    def test_orphaned_app_is_stopped_through_recorded_app_identity(self):
        leader=subprocess.Popen([sys.executable,'-c',
            'import os,sys,time; p=os.fork(); '
            'time.sleep(60) if p==0 else (print(p,flush=True),sys.stdin.read())'],
            stdin=subprocess.PIPE,stdout=subprocess.PIPE,start_new_session=True)
        app=int(leader.stdout.readline())
        record=self.record.with_name('orphan.json')
        try:
            subprocess.run([sys.executable,'-c',APP_PROCESS_RECORD,str(record),str(leader.pid),str(app)],check=True)
            leader.kill();leader.wait(timeout=5)
            result=subprocess.run([sys.executable,'-c',APP_PROCESS_SIGNAL,str(record),'TERM'],capture_output=True)
            self.assertEqual(result.returncode,0,result.stderr)
            for _ in range(50):
                path=pathlib.Path('/proc/%d/stat'%app)
                if not path.exists() or path.read_text().rsplit(')',1)[1].split()[0]=='Z':break
                time.sleep(.02)
            else:self.fail('Orphan app remained alive')
        finally:
            try:os.kill(app,signal.SIGKILL)
            except ProcessLookupError:pass
            if leader.poll() is None:leader.kill();leader.wait()
            leader.stdin.close();leader.stdout.close()
    def test_missing_record_fails_closed(self):
        self.record.unlink();self.assertNotEqual(self.send().returncode,0);self.assertIsNone(self.child.poll())
    def test_public_record_fails_closed(self):
        self.record.chmod(0o644);self.assertNotEqual(self.send().returncode,0);self.assertIsNone(self.child.poll())
    def test_symlink_record_fails_closed(self):
        other=self.record.with_name('other');self.record.rename(other);self.record.symlink_to(other)
        self.assertNotEqual(self.send().returncode,0);self.assertIsNone(self.child.poll())

if __name__=='__main__':unittest.main()
