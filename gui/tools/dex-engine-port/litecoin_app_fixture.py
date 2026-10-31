"""Command transport for the owned, long-running Qt app integration fixture."""
import json
import hashlib
from pathlib import Path
import os
import signal
import subprocess
import time
from test_litecoin_settlement import write_private, require


class LitecoinAppFixture:
    def __init__(self, binary, fixture, directory, processes, tls_root=None, preferences=None, generation=0,
            function_timeout_ms=1800000):
        self.directory, self.sequence = directory, 0
        self.log = directory / "qt.log"
        self.binary,self.fixture,self.processes,self.tls_root=binary,fixture,processes,tls_root
        self.generation=generation
        preferences = preferences or directory / "preferences"
        preferences.mkdir(mode=0o700,exist_ok=generation>0)
        self.preferences=preferences
        environment = dict(os.environ, QT_QPA_PLATFORM="offscreen", QTEST_FUNCTION_TIMEOUT=str(function_timeout_ms),
            MONZERO_LTC_APP_FIXTURE=str(fixture), XDG_CONFIG_HOME=str(preferences))
        if tls_root is not None:
            environment["SSL_CERT_FILE"] = str(tls_root)
        with self.log.open("xb") as output:
            os.chmod(self.log, 0o600)
            # This one Qt function spans block mining and the entire two-chain trade.
            # Its command/operation deadlines remain shorter and independently enforced.
            self.process = subprocess.Popen([str(binary), "actualLitecoinAppSetup"],
                env=environment, start_new_session=True,
                stdout=output, stderr=subprocess.STDOUT)
        self.process.monzero_owned_group=True
        processes.append(self.process)
        ready = self.read_when_ready(directory / "ready.json")
        require(ready == {"schema": "monzero-ltc-app-driver-ready-v1"}, "Unexpected resident app greeting")

    def read_when_ready(self, path):
        deadline = time.monotonic() + 420
        while not path.exists():
            if self.process.poll() is not None or time.monotonic() > deadline:
                raise RuntimeError("Resident app did not respond: " + self.log.read_text(errors="replace")[-16000:])
            time.sleep(0.05)
        require(path.stat().st_size <= 524288, "Oversized resident app response")
        return json.loads(path.read_text())

    def enqueue(self, role, operation, **fields):
        require(self.process.poll() is None, "Resident app stopped before command")
        self.sequence += 1
        pending=self.directory / f"{self.sequence}-request.tmp"
        write_private(pending, dict(sequence=self.sequence, role=role, operation=operation, **fields))
        pending.rename(self.directory / f"{self.sequence}-request.json")
        return self.directory / f"{self.sequence}-response.json"

    def call(self, role, operation, accepted=True, **fields):
        response = self.read_when_ready(self.enqueue(role,operation,**fields))
        require(response["schema"] == "monzero-ltc-app-driver-response-v1"
            and response["sequence"] == self.sequence and (accepted is None or response["accepted"] is accepted),
            "Resident app rejected or misrouted operation: " + operation)
        self.last = response
        return response[role]

    def start(self):
        for role in ("alice", "bob"):
            state = self.call(role, "status")
            require(state["peer"]["state"] == "configured" and not state["journal"]["valid"], "Peer started without saved approval")
            self.call(role, "start-submit", accepted=False, commitment="0" * 64)
            self.call(role, "funding-prepare", accepted=False)
            self.call(role, "funding-submit", accepted=False, commitment="0" * 64)
            state = self.call(role, "start-review")
            require(state["peer"]["state"] == "configured", "Review started peer exchange")
            review = state["journal"]["startReview"]
            require(review and not state["journal"]["snapshot"], "Review created trade state")
            state = self.call(role, "start-submit", commitment=review["commitment"])
            require(state["journal"]["startState"] == "saved", "Approved startup not saved")
        self.call("alice", "wait-negotiation")
        for role in ("alice", "bob"):
            self.call(role, "start-review", accepted=False)
        return {"schema": "monzero-ltc-resident-startup-fixture-v1", "startedAfterApproval": True, "executable": False}

    def crash_resume(self, helper, digest, launcher):
        for role in ('alice','bob'):
            state=self.call(role,'status')
            require(not state['app']['busy'] and not state['peer']['busy'], 'Crash fixture requires idle operation boundary')
        self._replace_process(helper,digest,launcher,self.last['bob']['lightFunding'])

    def interrupt_relay(self, helper, digest, launcher, peer, commitment):
        for role in ('alice','bob'):
            state=self.call(role,'status')
            require(not state['app']['busy'] and not state['peer']['busy'], 'Relay test must start idle')
        before=self.last['bob']['lightFunding']
        peer.broadcast_held.clear();peer.release_broadcast_reply.clear();peer.hold_next_broadcast_reply=True
        response=self.enqueue('bob','light-send',commitment=commitment)
        try:
            require(peer.broadcast_held.wait(180), 'Owned relay did not reach node acceptance barrier')
            require(self.process.poll() is None and not response.exists(), 'Relay completed before interruption')
            children=Path(f'/proc/{self.process.pid}/task/{self.process.pid}/children').read_text().split()
            helpers=[]
            for child in children:
                entry=Path('/proc')/child
                try:
                    command=(entry/'cmdline').read_bytes().split(b'\0')
                    executable=(entry/'exe').resolve(strict=True)
                except OSError:
                    continue
                if executable==Path(helper) and b'--dex-funding-send-stdio-v1' in command:
                    require(os.getpgid(int(child))==self.process.pid, 'Active helper escaped owned process group')
                    helpers.append(int(child))
            require(len(helpers)==1, 'Expected exactly one owned active funding helper')
            self._replace_process(helper,digest,launcher,before,peer.release_broadcast_reply.set)
            require(not response.exists(), 'Interrupted app published a completed relay response')
            for child in helpers:
                stat=Path(f'/proc/{child}/stat')
                require(not stat.exists() or stat.read_text().rsplit(')',1)[1].split()[0]=='Z',
                    'Owned relay helper still running after interruption')
        finally:
            peer.release_broadcast_reply.set()

    def _replace_process(self, helper, digest, launcher, before, after_kill=None):
        wallet=json.loads(Path(json.loads(Path(launcher).read_text())['configFile']).read_text())['wallet']
        folders={name:Path(wallet[name]) for name in ('spendDirectory','addressBookDirectory')}
        folders['vault']=Path(wallet['vault']['directory'])
        def wallet_records():
            return {(name,str(path.relative_to(folder))):hashlib.sha256(path.read_bytes()).hexdigest()
                for name,folder in folders.items() for path in folder.rglob('*') if path.is_file()}
        saved_wallet_records=wallet_records()
        generation=self.generation+1
        previous=self.directory
        directory=previous.parent/f'resident-app-resumed-{generation}'
        directory.mkdir(mode=0o700)
        fixture=previous.parent/f'resident-resume-{generation}.json'
        request=json.loads(self.fixture.read_text())
        request.update(resumeResident=True,residentDirectory=str(directory))
        write_private(fixture,request)
        require(self.process.poll() is None and os.getpgid(self.process.pid)==self.process.pid,
            'Refusing to interrupt an unverified process group')
        os.killpg(self.process.pid,signal.SIGKILL)
        require(self.process.wait(timeout=20)<0,'Owned app was not interrupted')
        if after_kill is not None: after_kill()
        secret=previous/'light-password'
        if secret.exists(): secret.rename(directory/'light-password')
        self.__init__(self.binary,fixture,directory,self.processes,self.tls_root,self.preferences,generation)
        for role in ('alice','bob'):
            self.call(role,'journal')
        self.call('bob','wait-negotiation')
        self.call('bob','reserve',accepted=False)
        self.call('bob','light-resume-wallet',helper=str(helper),digest=digest,launcher=launcher)
        restored=self.call('bob','light-select')['lightFunding']
        for field in ('tradeId','quoteCommitment','configurationDigest','retainedPayment','retainedReview','retainedTxid'):
            require(restored[field]==before[field], 'Restart changed retained local funding: '+field)
        require(not restored['review'] and not restored['reviewOperation'], 'Restart reused an old approval')
        intent=previous.parent/'bob-meta'/(before['tradeId']+'.ltc-recovery-intent')
        retained=intent.read_bytes()
        try:
            intent.write_bytes(b'{')
            refused=self.call('bob','light-resume-recovery',accepted=False)
            require(not refused['peer']['recoveryStarted'], 'Malformed intent started a recovery exchange')
        finally:
            intent.write_bytes(retained)
        self.call('bob','light-resume-recovery')
        self.call('bob','wait-recovery')
        require(wallet_records()==saved_wallet_records, 'App restart changed saved wallet, signatures or allocations')

    def submit_payment(self, name):
        role="bob" if name=="payout" else "alice"
        for retry in (False, True):
            state=self.call(role,name+"-review",retry=retry)["payment"]
            review=state[name+"Review"]
            require(review["details"]["action"]==("retransmit" if retry else "send"),"Wrong payment approval action")
            self.call(role,name+"-submit",accepted=False,commitment="0"*64)
            state=self.call(role,name+"-submit",commitment=review["commitment"])["payment"]
            require(state[name+"Submission"]["state"]=="acknowledged", "Resident payment not acknowledged")
            self.call(role,name+"-submit",accepted=False,commitment=review["commitment"])

    def check_payment(self, name, txid, reject=False):
        role="bob" if name=="payout" else "alice"
        self.call(role,"payment-refresh")
        state=self.call(role,name+"-check")["payment"]
        observation=state[name+"Confirmation"]
        if reject:
            require(state[name+"CheckState"]!="confirmed" and not observation,"Stale resident payment confirmation")
        else:
            require(state[name+"CheckState"]=="confirmed" and observation["txid"]==txid,"Resident payment not confirmed")

    def stop(self):
        self.call("alice", "quit")
        require(self.process.wait(timeout=30) == 0, "Resident app failed: " + self.log.read_text(errors="replace")[-16000:])


class SplitLitecoinApp(LitecoinAppFixture):
    """The same fixture with each trader in its own process (residentRole), so the
    peers talk over a real network address. Waits and keepalives go to both."""
    SHARED = {"wait-negotiation", "wait-recovery", "wait-settlement", "wait-reconnected", "status", "quit"}

    def __init__(self, binary, fixtures, directories, processes, tls_root=None, function_timeout_ms=1800000,
            remote_bob=None):
        # Alice first: her process provisions the shared app files the second one reads.
        self.parts = {}
        for role in ("alice", "bob"):
            if role == "bob" and remote_bob is not None:
                self.parts[role] = remote_bob()
                continue
            self.parts[role] = LitecoinAppFixture(binary, fixtures[role], directories[role], processes, tls_root,
                function_timeout_ms=function_timeout_ms)
        self.directory = directories["bob"]  # light funding (Bob) keeps its password here
        self.processes = processes

    def _merge(self, accepted):
        self.last = {"schema": "monzero-ltc-app-driver-response-v1", "accepted": accepted,
            **{role: part.last[role] for role, part in self.parts.items() if hasattr(part, "last")}}

    def call(self, role, operation, accepted=True, **fields):
        if operation in self.SHARED:
            results = []
            for name, part in self.parts.items():
                extra = dict(fields)
                if operation == "wait-reconnected":  # each process counts only its own rebuilds
                    extra["reconnects"] = part.last[name]["reconnects"] if hasattr(part, "last") else 0
                part.call(name, operation, accepted=None, **extra)
                results.append(part.last["accepted"])
            self._merge(all(results))
            require(accepted is None or self.last["accepted"] is accepted,
                "Split resident apps rejected or misrouted operation: " + operation)
            return self.last[role]
        part = self.parts[role]
        part.call(role, operation, accepted=accepted, **fields)
        self._merge(part.last["accepted"])
        return self.last[role]

    def stop(self):
        for name, part in self.parts.items():
            if hasattr(part, "signal_remote"):
                try:
                    part.call(name, "quit")
                    if part.process.wait(timeout=30) != 0:
                        raise RuntimeError("Remote resident app failed: " + part.log_tail())
                finally:
                    part.close()
                continue
            part.call(name, "quit")
            require(part.process.wait(timeout=30) == 0, "Resident app failed: " + part.log.read_text(errors="replace")[-16000:])
