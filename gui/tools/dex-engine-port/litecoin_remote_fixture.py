"""SSH command transport for the owned resident Qt app fixture on a provisioned VM.

The caller provisions the remote binary, fixture file and a private (0700)
resident directory and names the VM by an ssh_config alias. This class only
launches, drives and stops the app there; it never deletes remote files.

Every remote step is a POSIX sh script whose values are shlex-quoted. The script
travels base64-encoded inside one fixed single-quoted word, so the remote login
shell (sh, bash, fish, ...) never parses a caller-supplied byte.
"""
import base64
import json
import os
import re
import shlex
import stat
import subprocess
import tempfile
import threading
import time
from pathlib import Path, PurePosixPath
from litecoin_app_fixture import LitecoinAppFixture
from test_litecoin_settlement import require

HOST = re.compile(r"[A-Za-z0-9_][A-Za-z0-9._@-]{0,252}\Z")
ENCODED = re.compile(r"[A-Za-z0-9+/=]+\Z")
SCRIPT_LIMIT = 65536
LOG_TAIL = 16000
# ssh's own exit status for connection/transport failures.
SSH_FAILED = 255


def remote_path(value, name):
    text = str(value)
    path = PurePosixPath(text)
    require(path.is_absolute() and ".." not in path.parts and not any(ord(c) < 32 or ord(c) == 127 for c in text),
        "Unsafe remote " + name + " path")
    return path


def remote_command(script):
    """The one word the remote login shell sees; only base64 varies inside it."""
    encoded = base64.b64encode(script.encode()).decode()
    require(len(encoded) <= SCRIPT_LIMIT and ENCODED.match(encoded), "Oversized remote script")
    return "exec /bin/sh -c 'eval \"$(printf %s " + encoded + " | base64 -d)\"'"


def q(value):
    return shlex.quote(str(value))


APP_PROCESS_RECORD = r'''
import json,os,pathlib,sys
path,group,app=pathlib.Path(sys.argv[1]),int(sys.argv[2]),int(sys.argv[3])
def start(pid):return pathlib.Path("/proc/%d/stat"%pid).read_text().rsplit(")",1)[1].split()[19]
if group<=1 or app<=1 or os.getpgid(group)!=group or os.getpgid(app)!=group:
    sys.exit("Invalid app process ownership")
value={"bootId":pathlib.Path("/proc/sys/kernel/random/boot_id").read_text().strip(),
       "group":group,"groupStart":start(group),"app":app,"appStart":start(app)}
with os.fdopen(os.open(path,os.O_WRONLY|os.O_CREAT|os.O_EXCL,0o600),"w") as f:
    json.dump(value,f);f.flush();os.fsync(f.fileno())
fd=os.open(path.parent,os.O_RDONLY|os.O_DIRECTORY);os.fsync(fd);os.close(fd)
'''

APP_PROCESS_SIGNAL = r'''
import json,os,pathlib,signal,stat,sys
path,name=pathlib.Path(sys.argv[1]),sys.argv[2]
if name not in ("TERM","KILL"):sys.exit("Invalid app signal")
try:fd=os.open(path,os.O_RDONLY|os.O_NOFOLLOW|os.O_NONBLOCK)
except FileNotFoundError:sys.exit("Missing app boot identity; refusing signal")
with os.fdopen(fd) as f:
    m=os.fstat(f.fileno())
    if not stat.S_ISREG(m.st_mode) or m.st_uid!=os.getuid() or m.st_mode&0o077 or m.st_nlink!=1 or m.st_size>1024:
        sys.exit("Unsafe app process record")
    value=json.load(f)
boot=value.get("bootId")
if not isinstance(boot,str) or len(boot)!=36:sys.exit("Missing app boot identity")
if boot!=pathlib.Path("/proc/sys/kernel/random/boot_id").read_text().strip():sys.exit(0)
group=value.get("group")
if type(group) is not int or group<=1:sys.exit("Invalid app group")
def matches(pid,start):
    if type(pid) is not int or pid<=1 or not str(start).isdigit():return False
    try:
        proc=pathlib.Path("/proc/%d"%pid)
        fields=(proc/"stat").read_text().rsplit(")",1)[1].split()
        return fields[19]==str(start) and fields[0]!="Z" and proc.stat().st_uid==os.getuid() and os.getpgid(pid)==group
    except (FileNotFoundError,ProcessLookupError):return False
if matches(group,value.get("groupStart")) or matches(value.get("app"),value.get("appStart")):
    try:os.killpg(group,getattr(signal,"SIG"+name))
    except ProcessLookupError:pass
else:
    try:os.killpg(group,0)
    except ProcessLookupError:sys.exit(0)
    sys.exit("App group still exists without a verified owner; refusing signal")

'''

class RemoteProcess:
    """Popen-like handle for the remote app, so driver cleanup loops work unchanged.

    The local ssh session is the supervisor: its exit status is the app's (0 on
    quit, 128+N if the app died from signal N, 255 if ssh itself failed). Closing
    its stdin makes the remote watchdog TERM the app's process group, then KILL it
    after term_grace seconds."""
    monzero_owned_group = False  # the group lives remotely; never killpg a local pid

    def __init__(self, popen, owner):
        self.popen, self.owner = popen, owner

    @property
    def pid(self):
        return self.popen.pid

    @property
    def returncode(self):
        return self.popen.returncode

    def poll(self):
        return self.popen.poll()

    def wait(self, timeout=None):
        return self.popen.wait(timeout=timeout)

    def release(self):
        if self.popen.stdin and not self.popen.stdin.closed:
            try:
                self.popen.stdin.close()
            except OSError:
                pass

    def terminate(self):
        self.release()

    def kill(self):
        try:
            self.owner.signal_remote("KILL")
        except Exception:
            pass
        self.release()
        if self.popen.poll() is None:
            self.popen.kill()


class RemoteLitecoinAppFixture(LitecoinAppFixture):
    """LitecoinAppFixture driven over SSH. call/enqueue/read_when_ready/start/stop,
    submit_payment and check_payment behave as locally; paths are remote."""

    def __init__(self, host, binary, fixture, directory, processes, local_directory, tls_root=None, preferences=None,
            generation=0, function_timeout_ms=1800000, *, ssh=("ssh",), multiplex=True, ready_timeout=420,
            poll_seconds=15, response_limit=524288, request_limit=1048576, connect_timeout=15, term_grace=10,
            transport_retries=3):
        if not isinstance(host, str) or not HOST.match(host):
            raise ValueError("SSH host must be a plain configured alias")
        self.host, self.ssh = host, tuple(str(part) for part in ssh)
        self.binary = remote_path(binary, "binary")
        self.fixture = remote_path(fixture, "fixture")
        self.directory = remote_path(directory, "resident directory")
        self.tls_root = remote_path(tls_root, "TLS root") if tls_root is not None else None
        self.preferences = remote_path(preferences or self.directory / "preferences", "preferences")
        self.log = self.directory / "qt.log"
        self.pid_file = self.directory / "app.pid"
        self.processes, self.generation, self.sequence = processes, generation, 0
        self.ready_timeout, self.poll_seconds, self.connect_timeout = ready_timeout, poll_seconds, connect_timeout
        self.response_limit, self.request_limit, self.transport_retries = response_limit, request_limit, transport_retries
        self.local_directory = Path(local_directory)
        info = self.local_directory.lstat()
        require(stat.S_ISDIR(info.st_mode) and info.st_uid == os.getuid() and not info.st_mode & 0o077,
            "Local fixture directory must be a private directory")
        self.control = None
        if multiplex:
            self.control = str(self.local_directory / "ssh-%C")
            require(len(self.control) + 38 <= 100, "Local fixture directory too long for an SSH control socket")
        if self.control:
            self._open_master()
        try:
            self._preflight()
        except Exception:
            self.close()
            raise
        self.supervisor_log = self.local_directory / f"remote-supervisor-{generation}.log"
        with self.supervisor_log.open("xb"):
            os.chmod(self.supervisor_log, 0o600)
        environment = dict(QT_QPA_PLATFORM="offscreen", QTEST_FUNCTION_TIMEOUT=str(int(function_timeout_ms)),
            MONZERO_LTC_APP_FIXTURE=str(self.fixture), XDG_CONFIG_HOME=str(self.preferences))
        if self.tls_root is not None:
            environment["SSL_CERT_FILE"] = str(self.tls_root)
        exports = "\n".join(f"export {name}={q(value)}" for name, value in environment.items())
        script = f"""set -u
umask 077
d={q(self.directory)}
[ "$(ps -o pgid= -p $$ | tr -d ' ')" = "$$" ] || {{ echo 'remote shell does not own its process group' >&2; exit 70; }}
(set -C; : > "$d/qt.log") || exit 71
{exports}
exec 3<&0
{q(self.binary)} actualLitecoinAppSetup </dev/null >>"$d/qt.log" 2>&1 3<&- &
app=$!
(set -C; printf '%s %s\\n' "$$" "$app" > "$d/app.pid.tmp") && mv -- "$d/app.pid.tmp" "$d/app.pid" || {{ kill -KILL "$app"; exit 72; }}
python3 -c {q(APP_PROCESS_RECORD)} "$d/app.process.json" "$$" "$app" || {{ kill -KILL "$app"; exit 73; }}
(
  trap '' TERM
  cat <&3 >/dev/null 2>&1
  kill -TERM 0 2>/dev/null
  sleep {int(term_grace)}
  kill -KILL 0 2>/dev/null
) </dev/null >/dev/null 2>&1 &
watch=$!
exec 3<&-
wait "$app"
status=$?
kill -KILL "$watch" 2>/dev/null
exit "$status"
"""
        with self.supervisor_log.open("ab") as output:
            popen = subprocess.Popen(self._ssh_command(remote_command(script)), stdin=subprocess.PIPE,
                stdout=output, stderr=subprocess.STDOUT)
        self.process = RemoteProcess(popen, self)
        processes.append(self.process)
        ready = self.read_when_ready(self.directory / "ready.json")
        require(ready == {"schema": "monzero-ltc-app-driver-ready-v1"}, "Unexpected resident app greeting")

    # ---- transport ----

    def _ssh_options(self, master=False):
        options = [*self.ssh, "-T", "-o", "BatchMode=yes", "-o", f"ConnectTimeout={int(self.connect_timeout)}",
            "-o", "ServerAliveInterval=15", "-o", "ServerAliveCountMax=4"]
        if self.control:
            options += ["-o", "ControlPath=" + self.control,
                "-o", "ControlMaster=" + ("yes" if master else "no")]
        return options

    def _ssh_command(self, command):
        return [*self._ssh_options(), self.host, command]

    def _open_master(self):
        with tempfile.TemporaryFile() as errors:
            code = subprocess.run([*self._ssh_options(master=True), "-o", "ControlPersist=120", "-N", "-f", self.host],
                stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=errors,
                timeout=self.connect_timeout + 30).returncode
            errors.seek(0)
            require(code == 0, "SSH connection failed: " + errors.read(4000).decode(errors="replace"))

    def _run(self, script, data=None, timeout=60, limit=65536):
        expired = threading.Event()
        with tempfile.TemporaryFile() as errors:
            process = subprocess.Popen(self._ssh_command(remote_command(script)),
                stdin=subprocess.PIPE if data is not None else subprocess.DEVNULL,
                stdout=subprocess.PIPE, stderr=errors)
            def expire():
                expired.set()
                process.kill()
            timer = threading.Timer(timeout, expire)
            timer.start()
            try:
                if data is not None:
                    try:
                        process.stdin.write(data)
                        process.stdin.close()
                    except BrokenPipeError:
                        pass
                output = process.stdout.read(limit + 1)
                if len(output) > limit:
                    process.kill()
                process.stdout.close()
                code = process.wait()
            finally:
                timer.cancel()
                if process.poll() is None:
                    process.kill()
                    process.wait()
            errors.seek(0, os.SEEK_END)
            errors.seek(max(0, errors.tell() - 4000))
            tail = errors.read().decode(errors="replace")
        require(not expired.is_set(), "Remote command timed out: " + tail)
        require(len(output) <= limit, "Oversized remote output")
        return code, output, tail

    def _preflight(self):
        tls = f"[ -f {q(self.tls_root)} ] && [ -r {q(self.tls_root)} ] || {{ echo tls; exit 0; }}" if self.tls_root else ""
        existing = ('[ -d "$p" ] && [ ! -L "$p" ] && [ -O "$p" ] || { echo preferences; exit 0; }' if self.generation > 0
            else "echo preferences-exist; exit 0")
        script = f"""set -u
umask 077
d={q(self.directory)}
[ -d "$d" ] && [ ! -L "$d" ] && [ -O "$d" ] || {{ echo directory; exit 0; }}
[ "$(stat -c %a -- "$d")" = 700 ] || {{ echo directory-mode; exit 0; }}
[ -f {q(self.binary)} ] && [ -x {q(self.binary)} ] || {{ echo binary; exit 0; }}
[ -f {q(self.fixture)} ] && [ -r {q(self.fixture)} ] || {{ echo fixture; exit 0; }}
{tls}
for name in qt.log ready.json app.pid app.pid.tmp app.process.json 1-request.tmp 1-request.json 1-response.json; do
  [ ! -e "$d/$name" ] && [ ! -L "$d/$name" ] || {{ echo stale-state; exit 0; }}
done
p={q(self.preferences)}
if [ -e "$p" ] || [ -L "$p" ]; then
  {existing}
else
  mkdir -m 700 -- "$p" || {{ echo preferences; exit 0; }}
fi
echo ok
"""
        code, output, errors = self._run(script, timeout=self.connect_timeout + 30, limit=64)
        require(code == 0 and output == b"ok\n",
            "Remote resident preflight failed: " + output.decode(errors="replace").strip() + " " + errors)

    def signal_remote(self, name):
        require(name in ("TERM", "KILL"), "Unsupported remote signal")
        script = "exec python3 -c " + q(APP_PROCESS_SIGNAL) + " " + q(self.directory / "app.process.json") + " " + q(name)
        code, _, errors = self._run(script, timeout=self.connect_timeout + 15, limit=0)
        require(code == 0, "Remote signal failed: " + errors)

    def log_tail(self, size=LOG_TAIL):
        try:
            _, output, _ = self._run(f'f={q(self.log)}; [ -f "$f" ] || exit 0; exec tail -c {int(size)} -- "$f"',
                timeout=self.connect_timeout + 15, limit=size)
            return output.decode(errors="replace")
        except Exception as error:
            return "<remote log unavailable: %s>" % error

    # ---- fixture protocol ----

    def read_when_ready(self, path):
        path = remote_path(path, "response")
        deadline = time.monotonic() + self.ready_timeout
        failures = 0
        while True:
            ticks = max(1, int(min(self.poll_seconds, max(0, deadline - time.monotonic())) * 10))
            script = f"""set -u
p={q(path)}; f={q(self.pid_file)}
i=0
while [ ! -e "$p" ]; do
  if [ -f "$f" ] && read group app < "$f" && ! kill -0 "$app" 2>/dev/null; then
    [ -e "$p" ] && break
    echo stopped; exit 0
  fi
  i=$((i+1)); [ "$i" -le {ticks} ] || {{ echo pending; exit 0; }}
  sleep 0.1
done
[ -f "$p" ] && [ ! -L "$p" ] || {{ echo invalid; exit 0; }}
size=$(wc -c < "$p" | tr -d ' ')
[ "$size" -le {int(self.response_limit)} ] || {{ echo oversized; exit 0; }}
echo "ready $size"
exec head -c "$size" -- "$p"
"""
            code, output, errors = self._run(script, timeout=ticks / 10 + self.connect_timeout + 30,
                limit=self.response_limit + 64)
            if code == SSH_FAILED and failures < self.transport_retries and time.monotonic() < deadline:
                failures += 1
                time.sleep(1)
                continue
            require(code == 0, "Remote response read failed: " + errors)
            failures = 0
            header, _, body = output.partition(b"\n")
            if header == b"pending" and self.process.poll() is None and time.monotonic() < deadline:
                continue
            if header in (b"pending", b"stopped"):
                raise RuntimeError("Resident app did not respond: " + self.log_tail())
            require(header != b"oversized", "Oversized resident app response")
            require(re.fullmatch(rb"ready [0-9]{1,9}", header) is not None, "Invalid resident app response file")
            size = int(header[6:])
            require(size <= self.response_limit and len(body) == size, "Truncated resident app response")
            value = json.loads(body)
            require(isinstance(value, dict), "Resident app response is not an object")
            return value

    def enqueue(self, role, operation, **fields):
        require(self.process.poll() is None, "Resident app stopped before command")
        data = json.dumps(dict(sequence=self.sequence + 1, role=role, operation=operation, **fields),
            separators=(",", ":")).encode()
        require(len(data) <= self.request_limit, "Oversized resident app request")
        self.sequence += 1
        pending = self.directory / f"{self.sequence}-request.tmp"
        final = self.directory / f"{self.sequence}-request.json"
        script = f"""set -u
umask 077
t={q(pending)}; f={q(final)}
[ ! -e "$t" ] && [ ! -L "$t" ] && [ ! -e "$f" ] && [ ! -L "$f" ] || exit 17
(set -C; head -c {len(data)} > "$t") || exit 18
[ "$(wc -c < "$t" | tr -d ' ')" = {len(data)} ] || exit 19
sync -- "$t" 2>/dev/null || sync
mv -- "$t" "$f" || exit 20
"""
        code, _, errors = self._run(script, data=data, timeout=self.connect_timeout + 30, limit=0)
        reasons = {17: "request already exists", 18: "request write failed", 19: "request truncated",
            20: "request publish failed"}
        require(code == 0, "Remote command write failed: " + reasons.get(code, "exit %s" % code) + " " + errors)
        return self.directory / f"{self.sequence}-response.json"

    def stop(self):
        try:
            self.call("alice", "quit")
            try:
                code = self.process.wait(timeout=30)
            except subprocess.TimeoutExpired:
                code = None
            if code != 0:
                raise RuntimeError("Resident app failed (exit %s): " % code + self.log_tail())
        finally:
            self.close()

    def close(self):
        """Release the supervisor (the remote watchdog then stops a still-running app)
        and the SSH control master. Remote files are left in place."""
        if hasattr(self, "process"):
            self.process.release()
        if self.control:
            try:
                subprocess.run([*self.ssh, "-o", "ControlPath=" + self.control, "-O", "exit", self.host],
                    stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                    timeout=self.connect_timeout + 15, check=False)
            except subprocess.TimeoutExpired:
                pass

    def crash_resume(self, *args, **kwargs):
        raise NotImplementedError("Crash resume inspects host /proc and wallet folders; not supported remotely")

    def interrupt_relay(self, *args, **kwargs):
        raise NotImplementedError("Relay interruption inspects host /proc; not supported remotely")
