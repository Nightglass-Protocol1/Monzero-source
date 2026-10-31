"""Private VM workspace for the test-only, node-backed two-machine swap driver.

Identity generation and signing run in the VM. Only its public card/signature
return to the host. Node RPC access uses loopback-only SSH reverse forwards.
"""
import json
import pathlib
import json
import shlex
import subprocess
import tempfile
import time


# Read-only cold-boot attachment: expected public digests come from the retained
# host-side recovery checkpoint. Secret bytes are never returned over SSH.
VERIFY_EXISTING = r'''
import hashlib,json,os,pathlib,stat,sys
root=pathlib.Path(sys.argv[1])
expected=json.load(sys.stdin)
if not isinstance(expected,dict) or not expected:
    sys.exit("Missing recovery bindings")
def owned(path,directory,forbidden=0o077):
    m=path.lstat()
    kind=stat.S_ISDIR if directory else stat.S_ISREG
    if not kind(m.st_mode) or m.st_uid!=os.getuid() or m.st_mode&forbidden:
        sys.exit("Unsafe recovery path")
    if not directory and (m.st_nlink!=1 or m.st_size>16777216):
        sys.exit("Unsafe recovery file")
# Resolve no symlinks, including ancestors outside the private workspace.
for parent in reversed(root.parents):
    if not stat.S_ISDIR(parent.lstat().st_mode):
        sys.exit("Linked recovery ancestor")
owned(root,True)
for name,wanted in expected.items():
    rel=pathlib.PurePosixPath(name)
    if rel.is_absolute() or not rel.parts or ".." in rel.parts:
        sys.exit("Invalid recovery binding path")
    if not isinstance(wanted,str) or len(wanted)!=64 or any(c not in "0123456789abcdef" for c in wanted):
        sys.exit("Invalid recovery digest")
    path=root
    for part in rel.parts[:-1]:
        path=path/part; owned(path,True)
    forbidden=0o022 if name in ("kit-ltc/card.json","kit-ltc.sig") else 0o077
    path=path/rel.parts[-1]; owned(path,False,forbidden)
    fd=os.open(path,os.O_RDONLY|os.O_NOFOLLOW)
    with os.fdopen(fd,"rb") as f:
        m=os.fstat(f.fileno())
        if not stat.S_ISREG(m.st_mode) or m.st_uid!=os.getuid() or m.st_mode&forbidden or m.st_nlink!=1 or m.st_size>16777216:
            sys.exit("Unsafe opened recovery file")
        value=hashlib.sha256(f.read(16777217)).hexdigest()
    if value!=wanted:
        sys.exit("Recovery binding changed")
print("verified")
'''


KIT_RUN = r'''
import os,pathlib,secrets,stat,subprocess,sys
root=pathlib.Path(sys.argv[1]);attached=sys.argv[2]=="attached"
secret=root/"kit-ltc/identity.passphrase"
try:fd=os.open(secret,os.O_RDONLY|os.O_NOFOLLOW|os.O_NONBLOCK)
except FileNotFoundError:
    if attached or os.path.lexists(root/"kit-ltc/identity.bin"):
        sys.exit("Existing identity passphrase is missing; refusing regeneration")
    password=secrets.token_hex(24)
else:
    with os.fdopen(fd) as f:
        m=os.fstat(f.fileno())
        if not stat.S_ISREG(m.st_mode) or m.st_uid!=os.getuid() or m.st_mode&0o077 or m.st_nlink!=1 or m.st_size>4096:
            sys.exit("Unsafe identity passphrase file")
        password=f.read()
    if not password:sys.exit("Empty identity passphrase")
env=dict(os.environ,MONZERO_KIT_PASSPHRASE=password)
subprocess.run([sys.executable,str(root/"trade_kit.py"),*sys.argv[3:]],env=env,check=True)
'''

class ForwardProcess:
    """Owned SSH control master; -f returns only after forwards are accepted."""
    monzero_owned_group = False

    def __init__(self, command, directory):
        self.command, self.directory, self.returncode = command, directory, None

    def poll(self):
        if self.returncode is None:
            checked = subprocess.run(self.command[:-1] + ['-O', 'check', self.command[-1]], capture_output=True, timeout=10)
            if checked.returncode:
                self.returncode = checked.returncode
        return self.returncode

    def terminate(self):
        stopped = subprocess.run(self.command[:-1] + ['-O', 'exit', self.command[-1]], capture_output=True, timeout=10)
        self.returncode = stopped.returncode
        self.directory.cleanup()

    def kill(self):
        self.terminate()

    def wait(self, timeout=None):
        deadline = time.monotonic() + (timeout if timeout is not None else 30)
        while self.poll() is None:
            if time.monotonic() > deadline:
                raise subprocess.TimeoutExpired(self.command, timeout)
            time.sleep(.1)
        return self.returncode


class RemoteTradeWorkspace:
    def __init__(self, host, root, binary, helper, processes):
        if not host or host.startswith('-') or any(c.isspace() for c in host):
            raise ValueError('Use a configured SSH host alias')
        self.host, self.binary, self.helper = host, binary, helper
        self.processes = processes
        self.ssh = ['ssh', '-o', 'BatchMode=yes', '-o', 'ConnectTimeout=10',
                    '-o', 'ServerAliveInterval=15', '-o', 'ServerAliveCountMax=3', host]
        base = self.run(['python3', '-c', 'import pathlib; print(pathlib.Path.home())']).decode().strip()
        self.root = pathlib.PurePosixPath(base) / 'monzero-dex-trades' / root.name
        self.run(['python3', '-c',
                  'import pathlib,sys; p=pathlib.Path(sys.argv[1]); p.parent.mkdir(mode=0o700,exist_ok=True); p.mkdir(mode=0o700)',
                  str(self.root)])
        self.upload(pathlib.Path(__file__).resolve().parents[1] / 'dex-trade-kit/trade_kit.py', 'trade_kit.py')

    @classmethod
    def attach_existing(cls, host, root, binary, helper, processes, expected):
        """Attach without creating files; caller must bind every immutable input.

        This does not resume services or approve transactions. The caller must
        quiesce writers and supply its retained checkpoint, not freshly hash the
        remote files and treat those hashes as trusted recovery bindings.
        """
        if not host or host.startswith('-') or any(c.isspace() for c in host):
            raise ValueError('Use a configured SSH host alias')
        root = pathlib.PurePosixPath(root)
        if not root.is_absolute() or '..' in root.parts:
            raise ValueError('Recovery workspace must be an absolute VM path')
        if not isinstance(expected, dict) or not expected:
            raise ValueError('Recovery bindings are required')
        result = cls.__new__(cls)
        result.host, result.binary, result.helper = host, binary, helper
        result.processes, result.root = processes, root
        result.attached_existing = True
        result.ssh = ['ssh', '-o', 'BatchMode=yes', '-o', 'ConnectTimeout=10',
                      '-o', 'ServerAliveInterval=15', '-o', 'ServerAliveCountMax=3', host]
        verified = result.run(['python3', '-c', VERIFY_EXISTING, str(root)],
                              json.dumps(expected).encode()).decode().strip()
        if verified != 'verified':
            raise RuntimeError('Recovery workspace verification failed')
        return result

    def run(self, argv, data=None, timeout=60):
        result = subprocess.run(self.ssh + [shlex.join(map(str, argv))], input=data,
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=timeout)
        if result.returncode:
            raise RuntimeError('VM command failed: ' + result.stderr.decode(errors='replace')[-2000:])
        return result.stdout

    def path(self, name):
        p = pathlib.PurePosixPath(name)
        if p.is_absolute() or '..' in p.parts:
            raise ValueError('Remote workspace name must be relative')
        return str(self.root / p)

    def put(self, name, data):
        self.run(['python3', '-c',
            'import os,sys; p=sys.argv[1]; os.makedirs(os.path.dirname(p),mode=0o700,exist_ok=True); '
            'f=os.fdopen(os.open(p,os.O_WRONLY|os.O_CREAT|os.O_EXCL,0o600),"wb"); '
            'f.write(sys.stdin.buffer.read()); f.flush(); os.fsync(f.fileno()); f.close()', self.path(name)], data)

    def upload(self, local, name):
        self.put(name, pathlib.Path(local).read_bytes())

    def read(self, name):
        return self.run(['python3', '-c',
            'import pathlib,sys; p=pathlib.Path(sys.argv[1]); '
            'assert p.stat().st_size<=524288; sys.stdout.buffer.write(p.read_bytes())', self.path(name)])

    def kit(self, *args):
        mode = 'attached' if getattr(self, 'attached_existing', False) else 'fresh'
        return self.run(['python3', '-c', KIT_RUN, str(self.root), mode, *args], timeout=120)

    def forward(self, ports):
        directory = tempfile.TemporaryDirectory(prefix='mz-tunnel-')
        control = str(pathlib.Path(directory.name) / 'control')
        args = self.ssh[:-1] + ['-S', control, '-o', 'ControlMaster=yes',
            '-o', 'ExitOnForwardFailure=yes', '-f', '-N']
        for port in sorted(set(ports)):
            if not isinstance(port, int) or not 0 < port < 65536:
                raise ValueError('Invalid forwarded port')
            args += ['-R', f'127.0.0.1:{port}:127.0.0.1:{port}']
        try:
            # OpenSSH delays backgrounding until every requested remote forward
            # is confirmed. A coincidental existing listener cannot satisfy this.
            result = subprocess.run(args + [self.host], stdin=subprocess.DEVNULL,
                                    capture_output=True, timeout=30)
            if result.returncode:
                raise RuntimeError('VM tunnel setup rejected: ' + result.stderr.decode(errors='replace')[-2000:])
        except Exception:
            subprocess.run(self.ssh[:-1] + ['-S', control, '-O', 'exit', self.host],
                           capture_output=True, timeout=10)
            directory.cleanup()
            raise
        process = ForwardProcess(self.ssh[:-1] + ['-S', control, self.host], directory)
        self.processes.append(process)
        # A second SSH connection checks readiness without invoking any wallet RPC.
        self.run(['python3', '-c',
            'import socket,sys,time; ports=list(map(int,sys.argv[1:])); end=time.monotonic()+15\n'
            'for p in ports:\n'
            ' while True:\n'
            '  try:\n'
            '   s=socket.create_connection(("127.0.0.1",p),timeout=1); s.close(); break\n'
            '  except OSError:\n'
            '   if time.monotonic()>end: raise\n'
            '   time.sleep(.1)', *map(str, sorted(set(ports)))])
        if process.poll() is not None:
            raise RuntimeError('VM RPC tunnel stopped')
        return process
