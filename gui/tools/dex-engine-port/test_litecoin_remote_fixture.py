#!/usr/bin/python3
"""Remote resident app transport checks over a local stand-in for ssh; no VM, Qt app or trade."""
import json
import os
import pathlib
import subprocess
import sys
import tempfile
import time
import unittest
from litecoin_remote_fixture import RemoteLitecoinAppFixture

# Stands in for ssh: records argv, then runs the command the way sshd does, in a
# new session through a POSIX shell. Refuses any command but the fixed wrapper.
FAKE_SSH = r'''#!PYTHON
import json, os, re, sys
args = sys.argv[1:]
with open(os.environ["FAKE_SSH_LOG"], "a") as log:
    log.write(json.dumps(args) + "\n")
i = 0
while i < len(args) and args[i].startswith("-"):
    i += 2 if args[i] in ("-o", "-O") else 1
if "-N" in args[:i] or "-O" in args[:i]:
    sys.exit(0)
if args[i] != os.environ["FAKE_SSH_HOST"] or len(args) != i + 2:
    sys.exit(98)
if not re.fullmatch(r"""exec /bin/sh -c 'eval "\$\(printf %s [A-Za-z0-9+/=]+ \| base64 -d\)"'""", args[i + 1]):
    sys.exit(99)
os.chdir(os.environ["FAKE_SSH_HOME"])
os.setsid()
os.execv("/bin/sh", ["/bin/sh", "-c", args[i + 1]])
'''

# Stands in for the Qt app: the resident request/response file protocol plus
# enough startup state for LitecoinAppFixture.start().
FAKE_APP = r'''#!PYTHON
import json, os, pathlib, signal, sys, time
request = json.loads(pathlib.Path(os.environ["MONZERO_LTC_APP_FIXTURE"]).read_text())
directory = pathlib.Path(request["residentDirectory"])
if request.get("ignoreTerm"):
    signal.signal(signal.SIGTERM, signal.SIG_IGN)
def publish(name, value):
    pending = directory / (name + ".partial")
    pending.write_text(value if isinstance(value, str) else json.dumps(value))
    pending.rename(directory / name)
environment = {k: os.environ.get(k) for k in ("QT_QPA_PLATFORM", "QTEST_FUNCTION_TIMEOUT",
    "MONZERO_LTC_APP_FIXTURE", "XDG_CONFIG_HOME", "SSL_CERT_FILE")}
roles = {"alice": {}, "bob": {}}
print("fake app started", sys.argv[1:], flush=True)
publish("ready.json", {"schema": "monzero-ltc-app-driver-ready-v1"})
sequence = 0
while True:
    sequence += 1
    path = directory / f"{sequence}-request.json"
    while not path.exists():
        time.sleep(0.02)
    command = json.loads(path.read_text())
    role, operation, state = command["role"], command["operation"], roles[command["role"]]
    response = {"schema": "monzero-ltc-app-driver-response-v1", "sequence": command["sequence"], "accepted": True}
    body = {"peer": {"state": "configured"}, "journal": {"valid": False}, "app": {"busy": False}}
    if operation == "status":
        body.update(echo=command, environment=environment)
    elif operation == "start-review":
        if state.get("saved"):
            response["accepted"] = False
        else:
            state["review"] = {"commitment": "ab" * 32}
            body["journal"] = {"startReview": state["review"], "snapshot": None}
    elif operation == "start-submit":
        if state.get("review") and command.get("commitment") == state["review"]["commitment"]:
            state.update(saved=True, review=None)
            body["journal"] = {"startState": "saved"}
        else:
            response["accepted"] = False
    elif operation in ("funding-prepare", "funding-submit"):
        response["accepted"] = False
    elif operation == "wait-negotiation":
        response["accepted"] = all(r.get("saved") for r in roles.values())
    elif operation == "reject":
        response["accepted"] = False
    elif operation == "big":
        body["padding"] = "x" * command["size"]
    elif operation == "wrong-sequence":
        response["sequence"] += 1
    elif operation == "wrong-schema":
        response["schema"] = "other"
    elif operation == "not-object":
        publish(f"{sequence}-response.json", "[1]")
        continue
    elif operation == "crash":
        print("fake crash marker", file=sys.stderr, flush=True)
        sys.exit(3)
    elif operation == "hang":
        time.sleep(3600)
    response[role] = body
    publish(f"{sequence}-response.json", response)
    if operation == "quit":
        sys.exit(request.get("quitCode", 0))
'''

HOST = "monzero-ltc-vm"
NASTY = "dir 'q' \"d\" $(touch PWNED) `touch PWNED2` \\ ;&|*?"


class RemoteFixtureTest(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory(prefix="xmz-remote-")
        self.addCleanup(temp.cleanup)
        self.root = pathlib.Path(temp.name)
        self.local = self.root / "local"
        self.local.mkdir(mode=0o700)
        self.remote = self.root / "remote" / NASTY
        self.remote.mkdir(mode=0o700, parents=True)
        self.ssh, self.app = self.root / "ssh", self.remote.parent / "app $(x)"
        for path, text in ((self.ssh, FAKE_SSH), (self.app, FAKE_APP)):
            path.write_text(text.replace("PYTHON", sys.executable, 1))
            path.chmod(0o700)
        self.ssh_log = self.root / "ssh.log"
        self.home = self.root / "home"
        self.home.mkdir(mode=0o700)
        os.environ.update(FAKE_SSH_LOG=str(self.ssh_log), FAKE_SSH_HOST=HOST, FAKE_SSH_HOME=str(self.home))
        self.processes = []
        self.addCleanup(self.reap)

    def reap(self):
        for process in self.processes:
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=20)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=10)
            process.release()

    def fixture_file(self, **extra):
        path = self.remote.parent / ("fixture-" + str(len(list(self.remote.parent.glob("fixture-*")))) + ".json")
        path.write_text(json.dumps(dict(residentDirectory=str(self.remote), **extra)))
        path.chmod(0o600)
        return path

    def open(self, fixture=None, **options):
        options.setdefault("ready_timeout", 20)
        options.setdefault("poll_seconds", 1)
        options.setdefault("multiplex", True)
        resident = RemoteLitecoinAppFixture(HOST, self.app, fixture or self.fixture_file(), self.remote,
            self.processes, self.local, ssh=(str(self.ssh),), **options)
        self.addCleanup(resident.close)
        return resident

    def remote_pid(self):
        return int((self.remote / "app.pid").read_text().split()[1])

    def assert_gone(self, pid, within=15):
        deadline = time.monotonic() + within
        while time.monotonic() < deadline:
            try:
                os.kill(pid, 0)
                # The orphaned app may linger as a zombie until init reaps it.
                if pathlib.Path(f"/proc/{pid}/stat").read_text().rsplit(")", 1)[1].split()[0] == "Z":
                    return
            except (ProcessLookupError, FileNotFoundError):
                return
            time.sleep(0.1)
        self.fail("Remote app still running")

    def ssh_calls(self):
        return [json.loads(line) for line in self.ssh_log.read_text().splitlines()]

    def test_session_quoting_start_and_stop(self):
        tls = self.remote.parent / "root 'pem'.pem"
        tls.write_text("pem")
        resident = self.open(tls_root=tls)
        self.assertEqual(resident.start(), {"schema": "monzero-ltc-resident-startup-fixture-v1",
            "startedAfterApproval": True, "executable": False})
        hostile = {"commitment": "'; touch PWNED3; '", "note": "$(touch PWNED4)\n`id`\\\"", "list": ["\0", "-o"]}
        state = resident.call("bob", "status", **hostile)
        self.assertEqual({k: state["echo"][k] for k in hostile}, hostile)
        self.assertEqual(state["echo"]["sequence"], resident.sequence)
        self.assertEqual(state["environment"], {"QT_QPA_PLATFORM": "offscreen", "QTEST_FUNCTION_TIMEOUT": "1800000",
            "MONZERO_LTC_APP_FIXTURE": str(resident.fixture), "XDG_CONFIG_HOME": str(self.remote / "preferences"),
            "SSL_CERT_FILE": str(tls)})
        self.assertEqual(resident.call("alice", "reject", accepted=False)["peer"]["state"], "configured")
        self.assertIn("fake app started ['actualLitecoinAppSetup']", resident.log_tail())
        sequence = resident.sequence
        resident.stop()
        self.assertEqual(resident.process.returncode, 0)
        self.assertEqual(list(self.root.rglob("PWNED*")), [])
        names = {p.name for p in self.remote.iterdir()}
        for number in range(1, sequence + 2):  # every request and response is kept
            self.assertTrue({f"{number}-request.json", f"{number}-response.json"} <= names)
        self.assertFalse(any(name.endswith(".tmp") for name in names))
        self.assertEqual(oct((self.remote / "preferences").stat().st_mode & 0o777), "0o700")
        for path in self.remote.glob("*-request.json"):
            self.assertEqual(path.stat().st_mode & 0o777, 0o600)
        self.assertEqual((self.remote / "qt.log").stat().st_mode & 0o777, 0o600)
        calls = self.ssh_calls()
        self.assertIn("-N", calls[0])  # control master first
        self.assertEqual(calls[-1][-3:], ["-O", "exit", HOST])
        for call in calls[1:-1]:
            self.assertEqual(call[-2], HOST)
            self.assertIn("BatchMode=yes", call)
            self.assertIn("ControlMaster=no", call)

    def test_rejects_unsafe_host_and_paths_before_connecting(self):
        for host in ("-oProxyCommand=touch PWNED", "vm alias", "vm;id", "", "vm\n"):
            with self.assertRaises(ValueError):
                RemoteLitecoinAppFixture(host, self.app, self.fixture_file(), self.remote, [], self.local)
        for directory in ("relative/dir", "/tmp/a\nb", "/tmp/../etc"):
            with self.assertRaisesRegex(RuntimeError, "Unsafe remote"):
                RemoteLitecoinAppFixture(HOST, self.app, self.fixture_file(), directory, [], self.local)
        self.local.chmod(0o755)
        with self.assertRaisesRegex(RuntimeError, "private directory"):
            RemoteLitecoinAppFixture(HOST, self.app, self.fixture_file(), self.remote, [], self.local)
        self.assertFalse(self.ssh_log.exists())

    def test_preflight_refuses_unsafe_remote_directory(self):
        self.remote.chmod(0o750)
        with self.assertRaisesRegex(RuntimeError, "directory-mode"):
            self.open()
        self.remote.chmod(0o700)
        (self.remote / "preferences").mkdir(mode=0o700)
        with self.assertRaisesRegex(RuntimeError, "preferences-exist"):
            self.open()
        (self.remote / "preferences").rmdir()
        (self.remote / "1-response.json").write_text("{}")
        with self.assertRaisesRegex(RuntimeError, "stale-state"):
            self.open()
        self.assertEqual(self.processes, [])  # nothing launched

    def test_response_and_request_bounds(self):
        resident = self.open(response_limit=4096, request_limit=2048)
        self.assertEqual(len(resident.call("alice", "big", size=1000)["padding"]), 1000)
        with self.assertRaisesRegex(RuntimeError, "Oversized resident app response"):
            resident.call("alice", "big", size=5000)
        with self.assertRaisesRegex(RuntimeError, "Oversized resident app request"):
            resident.enqueue("alice", "status", padding="y" * 3000)
        resident.call("alice", "status")  # a refused request does not consume a sequence number

    def test_sequence_schema_and_acceptance(self):
        resident = self.open()
        with self.assertRaisesRegex(RuntimeError, "misrouted operation: wrong-sequence"):
            resident.call("alice", "wrong-sequence")
        with self.assertRaisesRegex(RuntimeError, "misrouted operation: wrong-schema"):
            resident.call("alice", "wrong-schema")
        with self.assertRaisesRegex(RuntimeError, "rejected or misrouted operation: reject"):
            resident.call("alice", "reject")
        with self.assertRaisesRegex(RuntimeError, "not an object"):
            resident.call("alice", "not-object")
        resident.call("alice", "status")  # sequence stays in step after refusals

    def test_app_exit_is_reported_with_remote_log(self):
        resident = self.open()
        with self.assertRaisesRegex(RuntimeError, "(?s)did not respond: .*fake crash marker"):
            resident.call("alice", "crash")
        self.assertEqual(resident.process.wait(timeout=20), 3)
        with self.assertRaisesRegex(RuntimeError, "stopped before command"):
            resident.enqueue("alice", "status")

    def test_failed_quit_status_fails_stop(self):
        resident = self.open(self.fixture_file(quitCode=4))
        with self.assertRaisesRegex(RuntimeError, r"exit 4"):
            resident.stop()

    def test_terminate_stops_remote_group(self):
        resident = self.open()
        resident.enqueue("alice", "hang")
        pid = self.remote_pid()
        resident.process.terminate()
        self.assertNotEqual(resident.process.wait(timeout=20), 0)
        self.assert_gone(pid)

    def test_watchdog_kills_app_ignoring_term(self):
        resident = self.open(self.fixture_file(ignoreTerm=True), term_grace=1)
        pid = self.remote_pid()
        resident.process.terminate()
        resident.process.wait(timeout=20)
        self.assert_gone(pid)

    def test_kill_signals_remote_group(self):
        resident = self.open(self.fixture_file(ignoreTerm=True), term_grace=600)
        pid = self.remote_pid()
        resident.process.kill()
        resident.process.wait(timeout=20)
        self.assert_gone(pid)

    def test_without_multiplexing(self):
        resident = self.open(multiplex=False)
        resident.call("alice", "status")
        resident.stop()
        self.assertFalse(any("-N" in call or "-O" in call for call in self.ssh_calls()))
        self.assertTrue(all(not any(a.startswith("ControlPath") for a in call) for call in self.ssh_calls()))

    def test_unsupported_host_side_operations(self):
        resident = self.open()
        for method in (resident.crash_resume, resident.interrupt_relay):
            with self.assertRaises(NotImplementedError):
                method()


if __name__ == "__main__":
    unittest.main()
