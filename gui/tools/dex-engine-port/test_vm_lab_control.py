import subprocess
import unittest
from vm_lab_control import LabVmControl
from vm_power_loss import VM_NAME, VM_UUID

class ControlTests(unittest.TestCase):
    def controller(self, states):
        self.calls=[];states=iter(states)
        def run(argv, **kwargs):
            self.calls.append(argv)
            text=''
            if 'showvminfo' in argv:
                text='name="%s"\nUUID="%s"\nVMState="%s"\n'%(VM_NAME,VM_UUID,next(states))
            return subprocess.CompletedProcess(argv,0,text.encode(),b'')
        return LabVmControl(run)
    def test_poweroff_targets_only_fixed_uuid(self):
        c=self.controller(['running']);c.hard_off()
        self.assertEqual(self.calls[-1],['/usr/bin/VBoxManage','controlvm',VM_UUID,'poweroff'])
    def test_start_is_once_and_headless(self):
        c=self.controller(['poweroff']);c.start()
        self.assertNotIn('--collect',self.calls[-1])
        self.assertIn('/usr/bin/VBoxHeadless',self.calls[-1]);self.assertIn(VM_UUID,self.calls[-1])
        with self.assertRaises(RuntimeError):c.start()
        self.assertEqual(len(self.calls),2)
    def test_prepared_identity_is_stable_and_used_by_launch(self):
        c=self.controller(['poweroff']);evidence=c.prepare_start()
        self.assertEqual(c.prepare_start(),evidence);self.assertEqual(self.calls,[])
        self.assertEqual(evidence['vmUuid'],VM_UUID)
        c.start()
        self.assertIn('--unit='+evidence['startUnit'],self.calls[-1])
        self.assertEqual(c.started_unit,evidence['startUnit'])
        with self.assertRaises(RuntimeError):c.prepare_start()

    def test_retained_start_is_observed_without_relaunch(self):
        evidence={'vmUuid':VM_UUID,'startUnit':'monzero-dexlab-recovery-'+'a'*32}
        calls=[]
        def run(argv,**kwargs):
            calls.append(argv)
            return subprocess.CompletedProcess(argv,0,b'LoadState=loaded\nActiveState=active\nSubState=running\nMainPID=123\n',b'')
        c=LabVmControl(run);c.retain_start_attempt(evidence)
        self.assertEqual(c.inspect_start_attempt()['MainPID'],'123')
        with self.assertRaises(RuntimeError):c.start()
        self.assertEqual(len(calls),1);self.assertEqual(calls[0][0],'systemctl')

    def test_retained_start_rejects_other_vm_or_unit(self):
        c=self.controller([])
        for evidence in ({'vmUuid':'other','startUnit':'monzero-dexlab-recovery-'+'a'*32},
                         {'vmUuid':VM_UUID,'startUnit':'unrelated.service'}):
            with self.assertRaises(ValueError):c.retain_start_attempt(evidence)
        self.assertEqual(self.calls,[])

    def test_inactive_or_missing_start_process_cannot_bind_guest(self):
        c=self.controller([]);c.retain_start_attempt({'vmUuid':VM_UUID,'startUnit':'monzero-dexlab-recovery-'+'a'*32})
        for active,pid in [('inactive','0'),('active','0')]:
            observed={'unit':c.started_unit,'LoadState':'loaded','ActiveState':active,'SubState':'running','MainPID':pid}
            with self.assertRaises(RuntimeError):c.require_live_start_attempt(observed)
        self.assertEqual(self.calls,[])

    def test_running_vm_cannot_be_started(self):
        c=self.controller(['running'])
        with self.assertRaises(RuntimeError):c.start()
        self.assertEqual(len(self.calls),1)
    def test_lost_start_reply_does_not_repeat_start(self):
        c=self.controller(['poweroff']);old=c.runner
        def run(argv,**kwargs):
            if argv[0]=='systemd-run':raise subprocess.TimeoutExpired(argv,30)
            return old(argv,**kwargs)
        c.runner=run
        with self.assertRaises(TimeoutError):c.start()
        with self.assertRaises(RuntimeError):c.start()
    def test_bad_vm_identity_fails_closed(self):
        c=LabVmControl(lambda argv,**kw:subprocess.CompletedProcess(argv,0,b'name="other"\nUUID="wrong"\nVMState="running"',b''))
        with self.assertRaises(RuntimeError):c.hard_off()
    def test_ssh_transport_failure_is_retryable_observation(self):
        c=LabVmControl(lambda argv,**kw:subprocess.CompletedProcess(argv,255,b'',b''))
        with self.assertRaises(ConnectionError):c.read_boot()

if __name__=='__main__':unittest.main()
