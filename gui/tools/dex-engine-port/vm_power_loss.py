"""Bounded power-loss sequencing for the sole designated DEX lab VM.

Callbacks implement the host VM controls and SSH probe. No recovery action is
repeated because an observation timed out. This module never runs at import.
"""
import time

VM_NAME = 'mz-dexlab'
VM_UUID = '39b51a01-9bdf-4e65-a510-6e9253c79c06'


def power_cycle(*, inspect_vm, read_boot, power_off, start_vm, ping_alice,
                expected_boot, timeout=300, monotonic=time.monotonic, sleep=time.sleep, record_phase=None, before_power_off=None, prepare_start=None):
    """Require confirmed power-off and a new SSH boot identity before returning.

    Caller owns exclusive access to the designated VM and must durably save the
    trade checkpoint first. Exceptions leave state for observation/recovery;
    callers must not blindly rerun this destructive sequence.
    """
    if not isinstance(expected_boot, str) or not expected_boot:
        raise ValueError('A retained guest boot identity is required')
    def record(name, **fields):
        if record_phase is not None:record_phase(name, dict(oldBootId=expected_boot, **fields))
    def inspect():
        info = inspect_vm()
        if info.get('name') != VM_NAME or info.get('uuid') != VM_UUID:
            raise RuntimeError('Refusing a VM other than the designated DEX lab')
        return info['state']
    if inspect() != 'running' or read_boot() != expected_boot:
        raise RuntimeError('VM state changed since checkpoint')
    ping_alice()
    record("power-off-requested")
    if before_power_off is not None:before_power_off()
    power_off()  # exactly once; hard power-off, never ACPI shutdown
    deadline = monotonic() + timeout
    while True:
        ping_alice()
        try:
            state = inspect()
        except (TimeoutError, ConnectionError):
            state = None
        if state == 'poweroff':
            break
        if monotonic() >= deadline:
            raise TimeoutError('Power-off not confirmed; VM was not restarted')
        sleep(1)
    ping_alice()
    record("power-off-confirmed")
    start_evidence = prepare_start() if prepare_start is not None else {}
    if not isinstance(start_evidence, dict):
        raise ValueError("Invalid VM start evidence")
    record("start-requested", **start_evidence)
    start_vm()  # exactly once, only after authoritative poweroff
    deadline = monotonic() + timeout
    while True:
        ping_alice()
        try:
            state = inspect()
        except (TimeoutError, ConnectionError):
            state = None
        if state in ('aborted', 'saved'):
            raise RuntimeError('Unexpected VM state during cold boot')
        boot = None
        if state == 'running':
            try:
                boot = read_boot()
            except (TimeoutError, ConnectionError):
                pass  # observation failure never triggers another start
        if boot:
            if boot == expected_boot:
                raise RuntimeError('Guest did not acquire a new boot identity')
            record('guest-ready', newBootId=boot)
            return {'oldBootId': expected_boot, 'newBootId': boot,
                    'powerOffConfirmed': True, 'aliceStayedAlive': True}
        if monotonic() >= deadline:
            raise TimeoutError('Guest SSH not ready; observe the existing VM')
        sleep(1)
