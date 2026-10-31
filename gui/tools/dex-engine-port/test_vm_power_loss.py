import unittest
from vm_power_loss import power_cycle, VM_NAME, VM_UUID

class CycleTests(unittest.TestCase):
    def setup_case(self, states, boots):
        self.states=iter(states);self.boots=iter(boots);self.actions=[];self.clock=0
        def inspect():
            state=next(self.states)
            if isinstance(state,Exception):raise state
            return {'name':VM_NAME,'uuid':VM_UUID,'state':state}
        def boot():
            v=next(self.boots)
            if isinstance(v,Exception):raise v
            return v
        def sleep(n):self.clock+=n
        return dict(inspect_vm=inspect,read_boot=boot,power_off=lambda:self.actions.append('off'),
            start_vm=lambda:self.actions.append('start'),ping_alice=lambda:None,
            expected_boot='old',timeout=2,monotonic=lambda:self.clock,sleep=sleep)
    def test_confirmed_poweroff_and_new_boot(self):
        args=self.setup_case(['running','stopping','poweroff','running'],['old','new'])
        self.assertEqual(power_cycle(**args)['newBootId'],'new');self.assertEqual(self.actions,['off','start'])
    def test_transient_ssh_failure_does_not_restart_again(self):
        args=self.setup_case(['running','poweroff','running','running'],['old',TimeoutError(),'new'])
        power_cycle(**args);self.assertEqual(self.actions,['off','start'])
    def test_no_start_without_confirmed_poweroff(self):
        args=self.setup_case(['running']*5,['old'])
        with self.assertRaises(TimeoutError):power_cycle(**args)
        self.assertEqual(self.actions,['off'])
    def test_vm_observation_timeout_does_not_repeat_mutations(self):
        args=self.setup_case(['running',TimeoutError(),'poweroff',TimeoutError(),'running'],['old','new'])
        power_cycle(**args);self.assertEqual(self.actions,['off','start'])

    def test_phase_write_failure_prevents_power_action(self):
        args=self.setup_case(['running'],['old'])
        def fail(*args):raise OSError('disk failure')
        args['record_phase']=fail
        with self.assertRaises(OSError):power_cycle(**args)
        self.assertEqual(self.actions,[])

    def test_phase_order_records_requests_before_actions(self):
        args=self.setup_case(['running','poweroff','running'],['old','new'])
        args['record_phase']=lambda phase,fields:self.actions.append(phase)
        power_cycle(**args)
        self.assertEqual(self.actions,['power-off-requested','off','power-off-confirmed','start-requested','start','guest-ready'])

    def test_final_boundary_failure_prevents_power_off(self):
        args=self.setup_case(['running'],['old'])
        def changed():raise RuntimeError('trade changed')
        args['before_power_off']=changed
        with self.assertRaisesRegex(RuntimeError,'trade changed'):power_cycle(**args)
        self.assertEqual(self.actions,[])

    def test_final_boundary_runs_after_intent_before_action(self):
        args=self.setup_case(['running','poweroff','running'],['old','new'])
        args['record_phase']=lambda phase,fields:self.actions.append(phase)
        args['before_power_off']=lambda:self.actions.append('boundary')
        power_cycle(**args)
        self.assertEqual(self.actions[:3],['power-off-requested','boundary','off'])

    def test_start_identity_is_persisted_before_action(self):
        args=self.setup_case(['running','poweroff','running'],['old','new'])
        evidence={'startUnit':'monzero-dexlab-recovery-fixture','vmUuid':VM_UUID}
        args['prepare_start']=lambda:evidence
        saved=[]
        def record(phase,fields):
            if phase=='start-requested':saved.append(fields)
        args['record_phase']=record
        original=args['start_vm']
        def start():
            self.assertEqual(saved[0]['startUnit'],evidence['startUnit']);original()
        args['start_vm']=start
        power_cycle(**args)

    def test_start_record_failure_prevents_start(self):
        args=self.setup_case(['running','poweroff'],['old'])
        def record(phase,fields):
            if phase=='start-requested':raise OSError('disk failure')
        args['record_phase']=record
        with self.assertRaises(OSError):power_cycle(**args)
        self.assertEqual(self.actions,['off'])

    def test_same_boot_rejected(self):
        args=self.setup_case(['running','poweroff','running'],['old','old'])
        with self.assertRaises(RuntimeError):power_cycle(**args)
    def test_other_vm_is_never_touched(self):
        args=self.setup_case([],[]);args['inspect_vm']=lambda:{'name':'other','uuid':VM_UUID,'state':'running'}
        with self.assertRaises(RuntimeError):power_cycle(**args)
        self.assertEqual(self.actions,[])
    def test_ssh_timeout_leaves_single_running_attempt(self):
        args=self.setup_case(['running','poweroff']+['running']*4,['old']+[TimeoutError()]*4)
        with self.assertRaises(TimeoutError):power_cycle(**args)
        self.assertEqual(self.actions,['off','start'])

if __name__=='__main__':unittest.main()
