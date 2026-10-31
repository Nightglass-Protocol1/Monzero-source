"""Private VM monzero-wallet-rpc services (claim, observer, recipient) for the VM trader.

Wallet directories, generated RPC logins and wallet passwords are created and
read only on the VM. The host learns ports and remote paths, never credentials.
Recipient RPC is a fixed allowlist with no key, seed or transfer methods.
"""
import json
import pathlib
import re
import subprocess
import time

ROLES = ('claim', 'observer', 'recipient')
RECIPIENT_METHODS = frozenset({'create_wallet', 'open_wallet', 'close_wallet', 'refresh',
                               'get_address', 'get_balance', 'get_transfer_by_txid'})
WALLET_NAME = re.compile(r'[A-Za-z0-9][A-Za-z0-9_-]{0,63}')
TXID = re.compile(r'[0-9a-f]{64}')

# Shared VM-side helpers: private file read and digest-authenticated loopback JSON-RPC.
CALL = r'''
import json,os,stat,requests
from requests.auth import HTTPDigestAuth
def valid_boot(value):
    import uuid
    try:return isinstance(value,str) and str(uuid.UUID(value))==value
    except ValueError:return False
def launch_binding(binary,daemon,stage):
    import hashlib
    h=hashlib.sha256()
    with open(binary,"rb") as f:
        for chunk in iter(lambda:f.read(1048576),b""):h.update(chunk)
    return {"binary":binary,"sha256":h.hexdigest(),"daemonPort":daemon,"stage":stage}
def wallet_command(binary,directory,daemon,stage,port):
    return [binary]+(["--testnet"] if stage=="testnet" else [])+["--wallet-dir",str(directory/"wallets"),
        "--shared-ringdb-dir",str(directory/"ringdb"),"--daemon-address","127.0.0.1:%d"%daemon,
        "--trusted-daemon","--allow-mismatched-daemon-version","--rpc-bind-ip","127.0.0.1",
        "--rpc-bind-port",str(port),"--non-interactive","--max-concurrency","2",
        "--log-file",str(directory/"wallet-rpc.log")]
def verify_wallet_command(state,directory):
    import pathlib
    binding=state["launch"]
    expected=wallet_command(binding["binary"],directory,binding["daemonPort"],binding["stage"],state["port"])
    actual=(pathlib.Path("/proc")/str(state["pid"])/"cmdline").read_bytes().split(b"\0")[:-1]
    if actual!=[os.fsencode(arg) for arg in expected]:
        raise RuntimeError("Resumed wallet command mismatch")
def verify_running_executable(state):
    # /proc/PID/exe refers to the mapped executable, even after its pathname is
    # replaced. Hashing the pathname alone would approve a different process.
    import pathlib,hashlib
    pid=state.get("pid")
    if type(pid) is not int or pid<=1:raise RuntimeError("Invalid executable process")
    proc=pathlib.Path("/proc")/str(pid)
    def identity():
        fields=(proc/"stat").read_text().rsplit(")",1)[1].split()
        if fields[0]=="Z" or fields[19]!=str(state.get("starttime")) or proc.stat().st_uid!=os.getuid():
            raise RuntimeError("Executable process identity changed")
        if state.get("bootId")!=pathlib.Path("/proc/sys/kernel/random/boot_id").read_text().strip():
            raise RuntimeError("Executable process boot changed")
    identity()
    digest=hashlib.sha256()
    with open(proc/"exe","rb") as f:
        for chunk in iter(lambda:f.read(1048576),b""):digest.update(chunk)
    identity()
    if digest.hexdigest()!=state.get("launch",{}).get("sha256"):
        raise RuntimeError("Running wallet executable mismatch")
def private(path):
    fd=os.open(path,os.O_RDONLY|os.O_NOFOLLOW|os.O_NONBLOCK)
    with os.fdopen(fd,encoding="ascii") as f:
        m=os.fstat(f.fileno())
        if not stat.S_ISREG(m.st_mode) or m.st_uid!=os.getuid() or m.st_nlink!=1 or m.st_mode&0o077 or m.st_size>1024:
            raise RuntimeError("nonprivate file")
        return f.read().strip()
def call_owned(port,login,method,params,timeout=30):
    # Pin one connected socket: never reconnect a password-bearing request to
    # another listener after the owner check. HTTPConnection auto_open=0 makes
    # server close/reset fail rather than silently opening a replacement.
    import pathlib,socket,http.client,time
    from requests.utils import parse_dict_header
    state=json.loads(private(pathlib.Path(login).parent/"process.json"))
    pid=state.get("pid")
    if type(pid) is not int or pid<=1 or state.get("port")!=port:
        raise RuntimeError("unbound wallet endpoint")
    def check_owner(sock):
        if state.get("bootId")!=pathlib.Path("/proc/sys/kernel/random/boot_id").read_text().strip():
            raise RuntimeError("old wallet boot")
        fields=pathlib.Path("/proc/%d/stat"%pid).read_text().rsplit(")",1)[1].split()
        if fields[19]!=str(state.get("starttime")) or fields[0]=="Z":
            raise RuntimeError("wallet process changed")
        local=sock.getsockname();remote=sock.getpeername()
        endpoint=lambda v:socket.inet_aton(v[0])[::-1].hex().upper()+":%04X"%v[1]
        wanted=(endpoint(remote),endpoint(local))
        end=time.monotonic()+2
        while True:
            inodes=set()
            for line in pathlib.Path("/proc/net/tcp").read_text().splitlines()[1:]:
                cols=line.split()
                if (cols[1],cols[2])==wanted and cols[3]=="01":inodes.add(cols[9])
            owned=set()
            for fd in pathlib.Path("/proc/%d/fd"%pid).iterdir():
                try:link=os.readlink(fd)
                except FileNotFoundError:continue
                if link.startswith("socket:["):owned.add(link[8:-1])
            if inodes & owned:return
            if time.monotonic()>=end:raise RuntimeError("RPC connection is not owned by wallet process")
            time.sleep(.02)
    user,password=private(login).split(":",1)
    connection=http.client.HTTPConnection("127.0.0.1",port,timeout=timeout)
    connection.sock=socket.create_connection(("127.0.0.1",port),timeout=timeout)
    connection.auto_open=0
    try:
        check_owner(connection.sock)
        probe=json.dumps({"jsonrpc":"2.0","id":"0","method":"get_version","params":{}})
        connection.request("POST","/json_rpc",probe,{"Content-Type":"application/json"})
        response=connection.getresponse();challenge=response.getheader("WWW-Authenticate","")
        body=response.read(1048577)
        if len(body)>1048576 or response.status!=401 or not challenge.startswith("Digest "):
            raise RuntimeError("wallet digest challenge missing")
        auth=HTTPDigestAuth(user,password);auth.init_per_thread_state()
        auth._thread_local.chal=parse_dict_header(challenge[7:])
        header=auth.build_digest_header("POST","http://127.0.0.1:%d/json_rpc"%port)
        if connection.sock is None:raise RuntimeError("wallet closed pinned connection")
        check_owner(connection.sock)
        connection.request("POST","/json_rpc",json.dumps({"jsonrpc":"2.0","id":"0","method":method,"params":params}),
            {"Content-Type":"application/json","Authorization":header})
        response=connection.getresponse();body=response.read(1048577)
        if response.status!=200 or len(body)>1048576:raise RuntimeError("wallet RPC failed")
        value=json.loads(body)
        if value.get("id")!="0" or "error" in value or "result" not in value:raise RuntimeError("RPC result invalid")
        return value["result"]
    finally:connection.close()
def call(port,login,method,params,timeout=30):
    # Address discovery and settlement observations require the same owned
    # endpoint guarantee as password-bearing operations.
    return call_owned(port,login,method,params,timeout)

'''

# Starts one service in its own session (pgid == pid) and reports only pid, starttime and port.
LAUNCH = CALL + r'''
import pathlib,socket,subprocess,sys,time,fcntl,secrets
binary,directory,daemon,stage=sys.argv[1],pathlib.Path(sys.argv[2]),int(sys.argv[3]),sys.argv[4]
binding=launch_binding(binary,daemon,stage)
resume=len(sys.argv)>5
expected_binding=json.load(sys.stdin) if resume else binding
if binding!=expected_binding:sys.exit("Retained launch configuration changed")
if not resume:
    directory.parent.mkdir(mode=0o700,exist_ok=True)
    directory.mkdir(mode=0o700)
else:
    # Attachment already checked immutable bindings; refuse linked/private path
    # substitution here as well before acquiring the directory lock.
    for path in (directory.parent,directory,directory/"wallets",directory/"ringdb"):
        m=path.lstat()
        if not stat.S_ISDIR(m.st_mode) or m.st_uid!=os.getuid() or m.st_mode&0o077:
            sys.exit("Unsafe existing wallet directory")
lock_fd=os.open(directory,os.O_RDONLY|os.O_DIRECTORY)
fcntl.flock(lock_fd,fcntl.LOCK_EX)
if (directory/"cancelled").exists(): sys.exit("Wallet launch cancelled")
if resume:
    expected_boot,expected_port=sys.argv[5],int(sys.argv[6])
    previous=json.loads(private(directory/"process.json"))
    if not valid_boot(previous.get("bootId")):sys.exit("Invalid previous boot identity")
    if previous.get("launch")!=binding:sys.exit("Recorded launch configuration changed")
    current_boot=pathlib.Path("/proc/sys/kernel/random/boot_id").read_text().strip()
    restarting=(len(sys.argv)==8 and sys.argv[7]=="restart-exited")
    if expected_boot==current_boot:
        sys.exit("Cold-boot identity mismatch")
    if restarting:
        if previous.get("bootId")!=current_boot or previous.get("resumedFromBootId")!=expected_boot:
            sys.exit("Unbound exited service")
    elif previous.get("bootId")==current_boot or not (
            previous.get("bootId")==expected_boot or previous.get("resumedFromBootId")==expected_boot):
        sys.exit("Cold-boot identity mismatch or service already resumed")
    if type(previous.get("port")) is not int or previous["port"]!=expected_port or not 0<expected_port<65536:
        sys.exit("Cold-boot port binding changed")
    if type(previous.get("pid")) is not int or previous["pid"]<=1 or not str(previous.get("starttime","")).isdigit():
        sys.exit("Invalid previous wallet process record")
    if restarting:
        try:
            fields=pathlib.Path("/proc/%d/stat"%previous["pid"]).read_text().rsplit(")",1)[1].split()
        except FileNotFoundError:
            fields=None
        if fields is not None and fields[19]==str(previous["starttime"]) and fields[0]!="Z":
            sys.exit("Resumed service still alive; refusing duplicate")
        # Reused PIDs are not signalled. The fixed port must still be free.
    # Check persistent wallet passwords locally, never generate replacements.
    for name in ("wallet-password","recipient-password"):
        if not private(directory.parent/name): sys.exit("Missing wallet password")
    port=expected_port
else:
    (directory/"wallets").mkdir(mode=0o700)
    (directory/"ringdb").mkdir(mode=0o700)
    port=0
s=socket.socket(); s.bind(("127.0.0.1",port)); port=s.getsockname()[1]; s.close()
command=wallet_command(binary,directory,daemon,stage,port)
read_fd,write_fd=os.pipe()
ready_read,ready_write=os.pipe()
pid=os.fork()
if pid==0:
    os.close(write_fd)
    os.close(lock_fd)
    os.close(ready_read)
    os.setsid()
    os.write(ready_write,b"1")
    os.close(ready_write)
    # No wallet process may execute until its identity has been saved. If the
    # launcher dies before that acknowledgement, EOF makes this child exit.
    token=os.read(read_fd,1)
    os.close(read_fd)
    if token!=b"1": os._exit(125)
    null=os.open(os.devnull,os.O_RDWR)
    for fd in (0,1,2): os.dup2(null,fd)
    if null>2: os.close(null)
    try:
        os.chdir(directory)
        os.execv(binary,command)
    finally:
        os._exit(127)
os.close(read_fd)
os.close(ready_write)
try:
    if os.read(ready_read,1)!=b"1": sys.exit("Wallet session failed")
    os.close(ready_read)
    with open("/proc/%d/stat"%pid) as f:
        start=f.read().rsplit(")",1)[1].split()[19]
    state={"pid":pid,"starttime":start,"port":port,
        "bootId":pathlib.Path("/proc/sys/kernel/random/boot_id").read_text().strip()}
    state["launch"]=binding
    if resume: state["resumedFromBootId"]=expected_boot
    record=directory/"process.json"
    pending=directory/(".process-"+secrets.token_hex(12)) if resume else record
    fd=os.open(pending,os.O_WRONLY|os.O_CREAT|os.O_EXCL,0o600)
    with os.fdopen(fd,"w") as f:
        json.dump(state,f);f.flush();os.fsync(f.fileno())
    if resume: os.replace(pending,record)
    fd=os.open(directory,os.O_RDONLY|os.O_DIRECTORY)
    try: os.fsync(fd)
    finally: os.close(fd)
    os.write(write_fd,b"1")
finally:
    os.close(write_fd)
    os.close(lock_fd)
exited=False
def stopped():
    global exited
    if not exited: exited=os.waitpid(pid,os.WNOHANG)[0]==pid
    return exited
login=directory/("monzero-wallet-rpc.%d.login"%port)
end=time.monotonic()+30
while True:
    try:
        if stopped():
            raise RuntimeError("exited")
        call(port,str(login),"get_version",{},5)
        verify_running_executable(state)
        break
    except Exception:
        if stopped() or time.monotonic()>end:
            if not stopped():
                os.killpg(pid,9);os.waitpid(pid,0)
            sys.exit("Wallet RPC service did not become ready")
        time.sleep(.2)
print(json.dumps(state))
'''

# Recover a lost resume reply without starting another service. A current-boot
# process must bind to the retained old boot and the exact expected command.
INSPECT_RESUME = CALL + r'''
import pathlib,sys,fcntl,time
binary,directory,daemon,stage,old_boot,port=sys.argv[1],pathlib.Path(sys.argv[2]),int(sys.argv[3]),sys.argv[4],sys.argv[5],int(sys.argv[6])
binding=launch_binding(binary,daemon,stage)
if json.load(sys.stdin)!=binding:sys.exit("Retained launch configuration changed")
m=directory.lstat()
if not stat.S_ISDIR(m.st_mode) or m.st_uid!=os.getuid() or m.st_mode&0o077:
    sys.exit("Unsafe wallet recovery directory")
lock=os.open(directory,os.O_RDONLY|os.O_DIRECTORY|os.O_NOFOLLOW)
deadline=time.monotonic()+10
while True:
    try:
        fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB);break
    except BlockingIOError:
        if time.monotonic()>deadline: sys.exit("Wallet recovery lock busy")
        time.sleep(.05)
state=json.loads(private(directory/"process.json"))
if not valid_boot(state.get("bootId")):sys.exit("Invalid recorded boot identity")
if state.get("launch")!=binding:sys.exit("Recorded launch configuration changed")
current=pathlib.Path("/proc/sys/kernel/random/boot_id").read_text().strip()
if type(state.get("port")) is not int or state["port"]!=port:
    sys.exit("Recovery port mismatch")
if state.get("bootId")!=current and old_boot!=current and (
        state.get("bootId")==old_boot or state.get("resumedFromBootId")==old_boot):
    print(json.dumps({"state":"needs-resume"}));sys.exit(0)
if state.get("bootId")!=current or state.get("resumedFromBootId")!=old_boot or old_boot==current:
    sys.exit("Unbound current-boot service")
pid=state.get("pid")
if type(pid) is not int or pid<=1 or not str(state.get("starttime","")).isdigit():
    sys.exit("Invalid recovery process identity")
proc=pathlib.Path("/proc")/str(pid)
try:
    fields=(proc/"stat").read_text().rsplit(")",1)[1].split()
except FileNotFoundError:
    fields=None
if fields is None or fields[19]!=str(state["starttime"]) or fields[0]=="Z":
    print(json.dumps({"state":"needs-restart"}));sys.exit(0)
if os.getpgid(pid)!=pid or proc.stat().st_uid!=os.getuid():
    sys.exit("Resumed process identity changed")
verify_wallet_command(state,directory)
verify_running_executable(state)
call(port,str(directory/("monzero-wallet-rpc.%d.login"%port)),"get_version",{},5)
print(json.dumps({"state":"running","process":state}))
'''

# Checkpoint service bindings only. The trade driver separately binds identity,
# immutable app configuration and retained transaction/journal evidence.
SERVICE_CHECKPOINT = CALL + r'''
import pathlib,sys,hashlib,fcntl,time
root=pathlib.Path(sys.argv[1])
binding=launch_binding(sys.argv[2],int(sys.argv[3]),sys.argv[4])
current=pathlib.Path("/proc/sys/kernel/random/boot_id").read_text().strip()
services={}
# Hold every role lock through the entire snapshot, in fixed launch-role order.
locks=[]
for role in ("claim","observer","recipient"):
    directory=root/role
    m=directory.lstat()
    if not stat.S_ISDIR(m.st_mode) or m.st_uid!=os.getuid() or m.st_mode&0o077:
        sys.exit("Unsafe checkpoint role directory")
    lock=os.open(directory,os.O_RDONLY|os.O_DIRECTORY|os.O_NOFOLLOW)
    deadline=time.monotonic()+10
    while True:
        try:fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB);break
        except BlockingIOError:
            if time.monotonic()>deadline:sys.exit("Wallet checkpoint lock busy")
            time.sleep(.05)
    locks.append(lock)
for role in ("claim","observer","recipient"):
    directory=root/role
    state=json.loads(private(directory/"process.json"))
    if state.get("launch")!=binding:sys.exit("Live launch configuration changed")
    if state.get("bootId")!=current:
        sys.exit("Wallet service belongs to another boot")
    pid,port=state.get("pid"),state.get("port")
    if type(pid) is not int or pid<=1 or type(port) is not int or not 0<port<65536:
        sys.exit("Invalid wallet service checkpoint")
    fields=pathlib.Path("/proc/%d/stat"%pid).read_text().rsplit(")",1)[1].split()
    if fields[19]!=str(state.get("starttime")) or fields[0]=="Z" or os.getpgid(pid)!=pid:
        sys.exit("Wallet service checkpoint is not live")
    verify_wallet_command(state,directory)
    verify_running_executable(state)
    call(port,str(directory/("monzero-wallet-rpc.%d.login"%port)),"get_version",{},5)
    services[role]={"bootId":current,"port":port,"launch":binding}
print(json.dumps(services))
'''

# Signals the owned group only after PID, starttime and process group all match.
CONTROL = r'''
import os,signal,sys,time
pid,start,action,grace=int(sys.argv[1]),sys.argv[2],sys.argv[3],float(sys.argv[4])
def alive():
    try:
        with open("/proc/%d/stat"%pid) as f:
            fields=f.read().rsplit(")",1)[1].split()
        group=os.getpgid(pid)
    except (FileNotFoundError,ProcessLookupError):
        return False
    if fields[19]!=start or fields[0]=="Z":
        return False
    if group!=pid:
        sys.exit("Wallet RPC process group changed")
    return True
def gone(seconds):
    end=time.monotonic()+seconds
    while alive():
        if time.monotonic()>end:
            return False
        time.sleep(.1)
    return True
if action in ("TERM","KILL") and alive():
    os.killpg(pid,getattr(signal,"SIG"+action))
elif action=="stop" and alive():
    os.killpg(pid,signal.SIGTERM)
    if not gone(grace) and alive():
        os.killpg(pid,signal.SIGKILL)
        gone(5)
print("running" if alive() else "exited")
'''

# Known path survives a lost launch reply. The PID record is never trusted
# without owner/mode/link checks, then CONTROL verifies PID reuse and group.
REGISTRY_CONTROL = r'''
import os,json,stat,sys,pathlib,fcntl,time
record=pathlib.Path(sys.argv[1])
record.parent.mkdir(parents=True,mode=0o700,exist_ok=True)
lock_fd=os.open(record.parent,os.O_RDONLY|os.O_DIRECTORY)
end=time.monotonic()+10
while True:
    try:
        fcntl.flock(lock_fd,fcntl.LOCK_EX|fcntl.LOCK_NB)
        break
    except BlockingIOError:
        if time.monotonic()>end: sys.exit("Wallet launch still in progress")
        time.sleep(.05)
try:
    fd=os.open(sys.argv[1],os.O_RDONLY|os.O_NOFOLLOW|os.O_NONBLOCK)
except FileNotFoundError:
    # Cancel a delayed launcher as well as observing the absent process.
    # Its exclusive mkdir or locked cancellation check will refuse to start.
    cancel=os.open(record.parent/"cancelled",os.O_WRONLY|os.O_CREAT,0o600)
    os.fsync(cancel);os.close(cancel);os.fsync(lock_fd)
    print("exited");sys.exit(0)
with os.fdopen(fd) as f:
    m=os.fstat(f.fileno())
    if not stat.S_ISREG(m.st_mode) or m.st_uid!=os.getuid() or m.st_mode&0o077 or m.st_nlink!=1 or m.st_size>1024:
        sys.exit("Unsafe wallet process record")
    state=json.load(f)
if type(state.get("pid")) is not int or state["pid"]<=1 or not str(state.get("starttime","")).isdigit():
    sys.exit("Invalid wallet process record")
boot=state.get("bootId")
if not isinstance(boot,str) or len(boot)!=36:
    sys.exit("Missing wallet process boot identity")
if boot!=pathlib.Path("/proc/sys/kernel/random/boot_id").read_text().strip():
    print("exited");sys.exit(0)
sys.argv[1:2]=[str(state["pid"]),str(state["starttime"])]
''' + CONTROL

# Recipient RPC: the password (if any) and the login are read on the VM, never sent.
RPC = CALL + 'ALLOWED=%r\n' % sorted(RECIPIENT_METHODS) + r'''
import sys
port,login,secret,method,timeout=int(sys.argv[1]),sys.argv[2],sys.argv[3],sys.argv[4],float(sys.argv[5])
try:
    if method not in ALLOWED:
        raise RuntimeError("method")
    params=json.loads(sys.stdin.read(65536))
    if secret:
        params["password"]=private(secret)
    result=(call_owned if secret else call)(port,login,method,params,timeout)
except Exception:
    sys.exit("Wallet RPC "+(method if method in ALLOWED else "request")+" failed")
sys.stdout.write(json.dumps(result))
'''

CLAIM_BALANCE = CALL + r'''
import sys
try:
    call(int(sys.argv[1]),sys.argv[2],"refresh",{},600)
    result=call(int(sys.argv[1]),sys.argv[2],"get_balance",{"account_index":0})
    print(json.dumps({"balance":int(result["balance"])}))
except Exception:
    sys.exit("VM claim wallet balance check failed")
'''

GENERATE = ('import os,secrets,sys; p=sys.argv[1]; os.makedirs(os.path.dirname(p),mode=0o700,exist_ok=True); '
            'f=os.open(p,os.O_WRONLY|os.O_CREAT|os.O_EXCL,0o600); os.write(f,secrets.token_hex(32).encode()); '
            'os.fsync(f); os.close(f)')


class RemoteWalletProcess:
    """Popen-like handle for one VM wallet service, so driver cleanup loops work unchanged."""
    monzero_owned_group = False  # the group lives on the VM; never killpg a local pid

    def __init__(self, workspace, role, remote_pid, starttime):
        self.workspace, self.role, self.registry, self.returncode = workspace, role, None, None
        self.bind_report(remote_pid,starttime)

    def bind_report(self, remote_pid, starttime):
        if not isinstance(remote_pid, int) or remote_pid <= 1 or not str(starttime).isdigit():
            raise ValueError('Invalid remote wallet process identity')
        self.remote_pid, self.starttime = remote_pid, str(starttime)

    @classmethod
    def registered(cls, workspace, role):
        result=cls.__new__(cls)
        result.workspace,result.role,result.returncode=workspace,role,None
        result.remote_pid,result.starttime=None,None
        result.registry=workspace.path('native-wallets/'+role+'/process.json')
        return result

    def control(self, action, grace=0):
        prefix=([REGISTRY_CONTROL,self.registry] if self.registry else [CONTROL,str(self.remote_pid),self.starttime])
        state = self.workspace.run(['python3', '-c', *prefix, action,
                                    str(grace)], timeout=grace + 30).decode().strip()
        if state not in ('running', 'exited'):
            raise RuntimeError('Unexpected wallet process state')
        if state == 'exited':
            self.returncode = 0
        return self.returncode

    def poll(self):
        return self.returncode if self.returncode is not None else self.control('check')

    def terminate(self):
        if self.returncode is None:
            self.control('TERM')

    def kill(self):
        if self.returncode is None:
            self.control('KILL')

    def stop(self, grace=10):
        """Bounded TERM then KILL in one VM call."""
        if self.returncode is None:
            self.control('stop', grace)
        if self.returncode is None:
            raise RuntimeError('Wallet RPC ' + self.role + ' survived KILL')

    def wait(self, timeout=None):
        deadline = time.monotonic() + (30 if timeout is None else timeout)
        while self.poll() is None:
            if time.monotonic() >= deadline:
                raise subprocess.TimeoutExpired('monzero-wallet-rpc ' + self.role, timeout)
            time.sleep(1)
        return self.returncode


class RemoteNativeWallets:
    def __init__(self, workspace, binary, daemon_port, stage):
        if stage not in ('regtest', 'testnet'):
            raise ValueError('Stage must be regtest or testnet')
        if not isinstance(daemon_port, int) or isinstance(daemon_port, bool) or not 0 < daemon_port < 65536:
            raise ValueError('Invalid daemon port')
        if not isinstance(binary, str) or not pathlib.PurePosixPath(binary).is_absolute() \
                or any(c.isspace() or not c.isprintable() for c in binary):
            raise ValueError('Remote wallet binary must be an absolute VM path')
        self.workspace, self.binary, self.daemon_port, self.stage = workspace, binary, daemon_port, stage
        self.services = {}

    def start(self):
        """Start all three services; each is owned by workspace.processes once launched."""
        if self.services:
            raise RuntimeError('Remote wallet services already started')
        for role in ROLES:
            directory = self.workspace.path('native-wallets/' + role)
            process=RemoteWalletProcess.registered(self.workspace,role)
            self.workspace.processes.append(process)
            info = json.loads(self.workspace.run(['python3', '-c', LAUNCH, self.binary, directory,
                                                  str(self.daemon_port), self.stage], timeout=90))
            process.bind_report(info.get('pid'),info.get('starttime'))
            port = info.get('port')
            if not isinstance(port, int) or not 0 < port < 65536:
                raise RuntimeError('Invalid remote wallet port')
            self.services[role] = (port, self.workspace.path(f'native-wallets/{role}/monzero-wallet-rpc.{port}.login'))
        for name in ('wallet-password', 'recipient-password'):
            self.workspace.run(['python3', '-c', GENERATE, self.workspace.path('native-wallets/' + name)])
        return self.config()

    def validate_launch_binding(self, value):
        if not isinstance(value, dict) or set(value) != {'binary', 'sha256', 'daemonPort', 'stage'}:
            raise ValueError('Missing launch configuration binding')
        if value['binary'] != self.binary or type(value['daemonPort']) is not int or value['daemonPort'] != self.daemon_port or value['stage'] != self.stage:
            raise ValueError('Launch configuration differs from retained checkpoint')
        if not isinstance(value['sha256'], str) or not re.fullmatch(r'[0-9a-f]{64}', value['sha256']):
            raise ValueError('Invalid retained binary hash')

    def recovery_checkpoint(self):
        """Return live service boot/port bindings, never wallet credentials.

        Caller must durably retain these alongside immutable trade bindings
        before stopping the VM. This is not an active-trade backup.
        """
        if set(self.services) != set(ROLES):
            raise RuntimeError('All wallet services must be started before checkpointing')
        checkpoint = json.loads(self.workspace.run(['python3', '-c', SERVICE_CHECKPOINT,
            self.workspace.path('native-wallets'), self.binary, str(self.daemon_port), self.stage], timeout=30))
        if not isinstance(checkpoint, dict) or set(checkpoint) != set(ROLES):
            raise RuntimeError('Incomplete service checkpoint')
        boots = set()
        for role in ROLES:
            entry = checkpoint[role]
            if not isinstance(entry, dict) or set(entry) != {'bootId', 'port', 'launch'}:
                raise RuntimeError('Malformed service checkpoint')
            if type(entry['port']) is not int or entry['port'] != self.services[role][0]:
                raise RuntimeError('Service checkpoint port changed')
            if not isinstance(entry['bootId'], str) or not re.fullmatch(
                    r'[0-9a-f]{8}-(?:[0-9a-f]{4}-){3}[0-9a-f]{12}', entry['bootId']):
                raise RuntimeError('Service checkpoint boot identity is invalid')
            self.validate_launch_binding(entry['launch'])
            boots.add(entry['bootId'])
        if len(boots) != 1:
            raise RuntimeError('Service checkpoint spans different boots')
        return checkpoint

    def resume_after_boot(self, checkpoint, *, restart_exited=False, require_unstarted=False):
        """Resume owned RPCs after a new guest boot, keeping configured ports.

        Requires verified workspace attachment and a retained checkpoint mapping
        each role to its old bootId and port. Never creates wallets/passwords.
        Verified live services can be reattached after a lost resume reply.
        """
        if self.services:
            raise RuntimeError('Remote wallet services already started')
        if not isinstance(checkpoint, dict) or set(checkpoint) != set(ROLES):
            raise ValueError('All wallet service recovery bindings are required')
        for role in ROLES:
            entry = checkpoint[role]
            if not isinstance(entry, dict) or set(entry) != {'bootId', 'port', 'launch'}:
                raise ValueError('Invalid wallet service recovery binding')
            if not isinstance(entry['bootId'], str) or not re.fullmatch(
                    r'[0-9a-f]{8}-(?:[0-9a-f]{4}-){3}[0-9a-f]{12}', entry['bootId']):
                raise ValueError('Invalid previous boot identity')
            if type(entry['port']) is not int or not 0 < entry['port'] < 65536:
                raise ValueError('Invalid retained wallet port')
            self.validate_launch_binding(entry['launch'])
        if len({checkpoint[r]['port'] for r in ROLES}) != len(ROLES):
            raise ValueError('Retained wallet ports collide')
        for role in ROLES:
            entry = checkpoint[role]
            process = RemoteWalletProcess.registered(self.workspace, role)
            args = [self.binary, self.workspace.path('native-wallets/' + role),
                    str(self.daemon_port), self.stage, entry['bootId'], str(entry['port'])]
            inspected = json.loads(self.workspace.run(
                ['python3', '-c', INSPECT_RESUME, *args], json.dumps(entry['launch']).encode(), timeout=30))
            if require_unstarted and inspected.get('state') != 'needs-resume':
                raise RuntimeError('Wallet service started without retained resume intent')
            if inspected.get('state') not in ('needs-resume', 'needs-restart', 'running'):
                raise RuntimeError('Unrecognized wallet recovery state')
            if inspected.get('state') == 'needs-restart' and not restart_exited:
                raise RuntimeError('Exited resumed service requires explicit restart')
            # Inspection is read-only. Register cleanup only for an accepted
            # existing service or an actual launch attempt, never a failed probe.
            self.workspace.processes.append(process)
            if inspected.get('state') == 'needs-resume':
                info = json.loads(self.workspace.run(['python3', '-c', LAUNCH, *args], json.dumps(entry['launch']).encode(), timeout=90))
            elif inspected.get('state') == 'needs-restart':
                if not restart_exited:
                    raise RuntimeError('Exited resumed service requires explicit restart')
                info = json.loads(self.workspace.run(
                    ['python3', '-c', LAUNCH, *args, 'restart-exited'], json.dumps(entry['launch']).encode(), timeout=90))
            elif inspected.get('state') == 'running':
                info = inspected['process']
            else:
                raise RuntimeError('Unrecognized wallet recovery state')
            process.bind_report(info.get('pid'), info.get('starttime'))
            if info.get('port') != entry['port']:
                raise RuntimeError('Resumed wallet port changed')
            self.services[role] = (entry['port'], self.workspace.path(
                f"native-wallets/{role}/monzero-wallet-rpc.{entry['port']}.login"))
        return self.config()

    def config(self):
        """Trade kit local.json xmz fields: ports and VM paths only."""
        return {'daemonPort': self.daemon_port,
                'claimWalletPort': self.services['claim'][0], 'claimLoginFile': self.services['claim'][1],
                'observerWalletPort': self.services['observer'][0], 'observerLoginFile': self.services['observer'][1],
                'walletPasswordFile': self.workspace.path('native-wallets/wallet-password')}

    def stop(self, grace=10):
        failed=[]
        for process in reversed(self.workspace.processes):
            if isinstance(process, RemoteWalletProcess) and process.workspace is self.workspace:
                try: process.stop(grace)
                except Exception: failed.append(process.role)
        if failed:
            raise RuntimeError('Could not verify wallet service cleanup: '+', '.join(failed))

    def rpc(self, method, params=None, password=False, timeout=60):
        if method not in RECIPIENT_METHODS:
            raise ValueError('Wallet RPC method not allowed')
        port, login = self.services['recipient']
        secret = self.workspace.path('native-wallets/recipient-password') if password else ''
        return json.loads(self.workspace.run(['python3', '-c', RPC, str(port), login, secret, method, str(timeout)],
                                             json.dumps(params or {}).encode(), timeout=timeout + 30))

    @staticmethod
    def wallet_name(name):
        if not isinstance(name, str) or not WALLET_NAME.fullmatch(name):
            raise ValueError('Invalid wallet name')
        return name

    def create_recipient(self, name):
        self.rpc('create_wallet', {'filename': self.wallet_name(name), 'language': 'English'}, password=True)

    def open_recipient(self, name):
        self.rpc('open_wallet', {'filename': self.wallet_name(name)}, password=True)

    def close_recipient(self):
        self.rpc('close_wallet')

    def refresh_recipient(self):
        return self.rpc('refresh', timeout=600)

    def recipient_address(self):
        return self.rpc('get_address', {'account_index': 0})['address']

    def claim_balance(self):
        """Observe the wallet the VM app restored, without exporting its credentials."""
        port, login = self.services['claim']
        value = json.loads(self.workspace.run(['python3', '-c', CLAIM_BALANCE, str(port), login], timeout=660))
        if set(value) != {'balance'} or type(value['balance']) is not int or value['balance'] < 0:
            raise RuntimeError('Invalid VM claim wallet balance')
        return value['balance']

    def recipient_balance(self):
        result = self.rpc('get_balance', {'account_index': 0})
        return {'balance': int(result['balance']), 'unlocked_balance': int(result['unlocked_balance'])}

    def recipient_transfer(self, txid):
        if not isinstance(txid, str) or not TXID.fullmatch(txid):
            raise ValueError('Invalid txid')
        return self.rpc('get_transfer_by_txid', {'txid': txid})['transfer']
