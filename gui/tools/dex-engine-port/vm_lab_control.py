"""Host control adapter for the designated mz-dexlab power-loss fixture only."""
import json
import subprocess
import uuid
from vm_power_loss import VM_NAME, VM_UUID


class LabVmControl:
    def __init__(self, runner=subprocess.run):
        self.runner = runner
        self.started_unit = None
        self.prepared_unit = None

    def command(self, argv, timeout=30, ssh=False):
        try:
            result = self.runner(argv, capture_output=True, timeout=timeout)
        except subprocess.TimeoutExpired as exc:
            raise TimeoutError('VM observation/control command timed out; do not repeat mutations') from exc
        if result.returncode:
            if ssh and result.returncode == 255:
                raise ConnectionError('Guest SSH is not ready')
            raise RuntimeError('Designated VM command failed: ' + str(result.returncode))
        return result.stdout.decode().strip()

    def inspect(self):
        output = self.command(['/usr/bin/VBoxManage', 'showvminfo', VM_UUID, '--machinereadable'])
        values = {}
        for line in output.splitlines():
            key, sep, value = line.partition('=')
            if sep and key in ('name', 'UUID', 'VMState'):
                if key in values:
                    raise RuntimeError('Duplicate VM identity field')
                values[key] = json.loads(value)
        if values.get('name') != VM_NAME or values.get('UUID') != VM_UUID:
            raise RuntimeError('Designated VM identity changed')
        return {'name': values['name'], 'uuid': values['UUID'], 'state': values['VMState']}

    def read_boot(self):
        value = self.command(['ssh', '-o', 'BatchMode=yes', '-o', 'ConnectTimeout=10',
            'monzero-dexlab-vm', 'cat /proc/sys/kernel/random/boot_id'], timeout=15, ssh=True)
        try:
            if str(uuid.UUID(value)) != value:
                raise ValueError()
        except ValueError as exc:
            raise RuntimeError('Invalid guest boot identity') from exc
        return value

    def require_no_pending_power_commands(self):
        """Read-only check for this user's outstanding VBoxManage VM commands."""
        import os,pathlib
        for proc in pathlib.Path('/proc').iterdir():
            if not proc.name.isdigit():continue
            try:
                if proc.stat().st_uid!=os.getuid():continue
                command=(proc/'cmdline').read_bytes().split(b'\0')
            except FileNotFoundError:continue
            if (command and pathlib.PurePosixPath(os.fsdecode(command[0])).name=='VBoxManage'
                    and any(value in command for value in (VM_UUID.encode(),VM_NAME.encode()))):
                raise RuntimeError('A designated VM control command is still running')

    def hard_off(self):
        if self.inspect()['state'] != 'running':
            raise RuntimeError('VM is not running at power-loss boundary')
        self.command(['/usr/bin/VBoxManage', 'controlvm', VM_UUID, 'poweroff'], timeout=60)

    def prepare_start(self):
        """Allocate the unit identity for durable journaling, without launching."""
        if self.started_unit is not None:
            raise RuntimeError('VM start already attempted; observe that attempt')
        if self.prepared_unit is None:
            self.prepared_unit = 'monzero-dexlab-recovery-' + uuid.uuid4().hex
        return {'startUnit': self.prepared_unit, 'vmUuid': VM_UUID}

    def retain_start_attempt(self, evidence):
        """Restore an uncertain start intent; never infer permission to retry."""
        import re
        if (not isinstance(evidence, dict) or evidence.get('vmUuid') != VM_UUID
                or not isinstance(evidence.get('startUnit'), str)
                or re.fullmatch(r'monzero-dexlab-recovery-[0-9a-f]{32}', evidence['startUnit']) is None):
            raise ValueError('Invalid retained VM start identity')
        unit = evidence['startUnit']
        if self.started_unit not in (None, unit) or self.prepared_unit not in (None, unit):
            raise RuntimeError('Conflicting retained VM start identity')
        self.prepared_unit = self.started_unit = unit

    def inspect_start_attempt(self):
        if self.started_unit is None:
            raise RuntimeError('No retained VM start attempt')
        fields = ('LoadState', 'ActiveState', 'SubState', 'MainPID')
        output = self.command(['systemctl', '--user', 'show', self.started_unit,
                               '--property=' + ','.join(fields)])
        result = {}
        for line in output.splitlines():
            key, sep, value = line.partition('=')
            if not sep or key not in fields or key in result:
                raise RuntimeError('Invalid start-unit observation')
            result[key] = value
        if set(result) != set(fields) or not result['MainPID'].isdigit():
            raise RuntimeError('Incomplete start-unit observation')
        return dict(result, unit=self.started_unit)

    def require_live_start_attempt(self, observed):
        import os
        import pathlib
        if (observed.get('LoadState')!='loaded' or observed.get('ActiveState')!='active'
                or observed.get('SubState')!='running' or observed.get('unit')!=self.started_unit):
            raise RuntimeError('Recorded VM start unit is not running')
        pid=int(observed.get('MainPID','0'))
        if pid<=1:raise RuntimeError('Recorded VM start unit has no live process')
        proc=pathlib.Path('/proc')/str(pid)
        before=(proc/'stat').read_text().rsplit(')',1)[1].split()
        expected=pathlib.Path('/usr/lib/virtualbox/VBoxHeadless')
        if (before[0]=='Z' or proc.stat().st_uid!=os.getuid()
                or (proc/'exe').resolve(strict=True)!=expected.resolve(strict=True)):
            raise RuntimeError('Recorded VM process is not owned VBoxHeadless')
        command=(proc/'cmdline').read_bytes().split(b'\0')[:-1]
        if (not command or command[0] not in (b'/usr/bin/VBoxHeadless',os.fsencode(expected))
                or command[1:]!=[b'--startvm',VM_UUID.encode(),b'--vrde',b'off']):
            raise RuntimeError('Recorded VM process targets another VM or command')
        after=(proc/'stat').read_text().rsplit(')',1)[1].split()
        if after[0]=='Z' or before[19]!=after[19] or self.inspect_start_attempt()!=observed:
            raise RuntimeError('Recorded VM start process changed during observation')
        return {'pid':pid,'starttime':after[19]}

    def start(self):
        if self.started_unit is not None:
            raise RuntimeError('VM start already attempted; observe that attempt')
        if self.inspect()['state'] != 'poweroff':
            raise RuntimeError('VM power-off is not confirmed')
        # Save the unit before invoking the command so a lost reply cannot cause
        # a second attempt through this controller instance.
        self.started_unit = self.prepare_start()['startUnit']
        self.command(['systemd-run', '--user', '--unit=' + self.started_unit,
            '/usr/bin/VBoxHeadless', '--startvm', VM_UUID, '--vrde', 'off'])
