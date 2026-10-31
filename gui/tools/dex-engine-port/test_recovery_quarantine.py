import pathlib,subprocess,tempfile,unittest
from unittest.mock import Mock,patch
from local_process_identity import local_process_identity
from remote_recovery_checkpoint import persist_checkpoint,RecoveryPhaseJournal
from remote_vm_recovery import quarantine_failed_lab_recovery,checkpoint_digest
from vm_lab_lease import LabVmLease
from vm_power_loss import VM_NAME,VM_UUID

class QuarantineTests(unittest.TestCase):
    def setUp(self):
        tmp=tempfile.TemporaryDirectory();self.addCleanup(tmp.cleanup)
        self.root=pathlib.Path(tmp.name);self.path=self.root/'checkpoint'
        self.process=subprocess.Popen(['sleep','60']);self.addCleanup(self.stop)
        self.checkpoint={'schema':'monzero-vm-powerloss-checkpoint-v1',
            'oldBootId':'00000000-0000-0000-0000-000000000001',
            'aliceProcess':local_process_identity(self.process),
            'services':{r:{'launch':{'stage':'regtest'}} for r in ('claim','observer','recipient')}}
        persist_checkpoint(self.path,self.checkpoint);self.digest=checkpoint_digest(self.checkpoint)
        journal=RecoveryPhaseJournal(str(self.path)+'.phases')
        journal.record('checkpoint-bound',{'sha256':self.digest})
        for phase in ('power-off-requested','power-off-confirmed'):
            journal.record(phase,{'oldBootId':self.checkpoint['oldBootId']})
        self.control=Mock();self.control.inspect.return_value={'name':VM_NAME,'uuid':VM_UUID,'state':'poweroff'}
        with self.lease() as lease:lease.begin_recovery(self.path,self.digest)
    def lease(self,exclusive=True):return LabVmLease(exclusive,self.root,lambda:[])
    def stop(self):
        if self.process.poll() is None:self.process.terminate()
        self.process.wait(timeout=5)
    def quarantine(self,lease):
        try:return quarantine_failed_lab_recovery(self.path,controller=self.control,vm_lease=lease)
        finally:
            self.control.start.assert_not_called();self.control.hard_off.assert_not_called()
    def test_dead_original_can_quarantine_without_reusing_checkpoint(self):
        self.stop()
        with self.lease() as lease:
            result=self.quarantine(lease)
            self.assertEqual(result['observation'],'power-off-observed')
            self.assertIsNone(lease.registry.current())
            with self.assertRaisesRegex(RuntimeError,'already quarantined'):lease.begin_recovery(self.path,self.digest)
        with self.lease(False):pass
        self.assertTrue(self.path.exists())
    def test_pending_vm_command_keeps_claim(self):
        self.stop();self.control.require_no_pending_power_commands.side_effect=RuntimeError('command running')
        with self.lease() as lease:
            with self.assertRaisesRegex(RuntimeError,'command running'):self.quarantine(lease)
            self.assertIsNotNone(lease.registry.current())

    def test_unresolved_start_must_be_terminal(self):
        self.stop()
        journal=RecoveryPhaseJournal.attach(str(self.path)+'.phases')
        journal.record('start-requested',{'oldBootId':self.checkpoint['oldBootId'],
            'vmUuid':VM_UUID,'startUnit':'monzero-dexlab-recovery-'+'a'*32})
        self.control.inspect_start_attempt.return_value={'ActiveState':'activating','MainPID':'123'}
        with self.lease() as lease:
            with self.assertRaisesRegex(RuntimeError,'remains unresolved'):self.quarantine(lease)
            self.assertIsNotNone(lease.registry.current())
            self.control.inspect_start_attempt.return_value={'ActiveState':'failed','MainPID':'0'}
            self.quarantine(lease)
            self.assertIsNone(lease.registry.current())

    def test_late_running_guest_is_not_retired(self):
        self.stop()
        journal=RecoveryPhaseJournal.attach(str(self.path)+'.phases')
        newboot='00000000-0000-0000-0000-000000000002'
        journal.record('start-requested',{'oldBootId':self.checkpoint['oldBootId'],
            'vmUuid':VM_UUID,'startUnit':'monzero-dexlab-recovery-'+'a'*32})
        journal.record('guest-ready',{'oldBootId':self.checkpoint['oldBootId'],'newBootId':newboot})
        for phase in ('bindings-verified','tunnels-restored','wallet-services-requested'):
            journal.record(phase,{})
        self.control.inspect.return_value['state']='running';self.control.read_boot.return_value=newboot
        self.control.inspect_start_attempt.return_value={'ActiveState':'active','MainPID':'123'}
        with self.lease() as lease:
            with self.assertRaisesRegex(RuntimeError,'Guest services may still be active'):self.quarantine(lease)
            self.assertIsNotNone(lease.registry.current())

    def test_abandoned_journal_can_finish_interrupted_registry_release(self):
        self.stop()
        folder=pathlib.Path(str(self.path)+'.phases')
        for p in folder.glob('*.json'):p.unlink()
        journal=RecoveryPhaseJournal.attach(folder)
        journal.record('checkpoint-bound',{'sha256':self.digest})
        journal.record('power-off-abandoned',{'errorClass':'OSError'})
        with self.lease() as lease:
            result=self.quarantine(lease)
            self.assertEqual(result['observation'],'abandoned-before-power-action')
            self.assertIsNone(lease.registry.current())
            with self.assertRaisesRegex(RuntimeError,'already abandoned'):lease.begin_recovery(self.path,self.digest)
        self.control.inspect.assert_not_called()

    def test_live_original_refuses_quarantine(self):
        with self.lease() as lease:
            with self.assertRaisesRegex(RuntimeError,'still alive'):self.quarantine(lease)
            self.assertIsNotNone(lease.registry.current())
        self.control.inspect.assert_not_called()
    def test_uncertain_observation_retains_claim(self):
        self.stop();self.control.inspect.side_effect=TimeoutError('unavailable')
        with self.lease() as lease:
            with self.assertRaises(TimeoutError):self.quarantine(lease)
            self.assertIsNotNone(lease.registry.current())
    def test_transitioning_vm_refuses_quarantine(self):
        self.stop();self.control.inspect.return_value['state']='starting'
        with self.lease() as lease:
            with self.assertRaisesRegex(RuntimeError,'transitioning'):self.quarantine(lease)
            self.assertIsNotNone(lease.registry.current())

if __name__=='__main__':unittest.main()
