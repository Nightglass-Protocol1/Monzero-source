"""Crash and reopen the VM-resident Bob of a split light-funding fixture.

Two entry points share one reopen path:
- restart_bob: idle boundaries after the light lock was signed or its broadcast
  was acknowledged. A busy operation is refused.
- interrupt_bob_after_acceptance: one fresh send approval is submitted and Bob
  is killed only once the owned Electrum peer holds the reply to a node-accepted
  broadcast of the retained signed bytes. Recovery never sends, retries or signs.
  The driver reconciles the uncertain outcome after this returns.

Alice's process stays up throughout and must rebuild her session against the
reopened Bob.

Every private file stays on the VM. Wallet folders and protected trade metadata
are hashed there, and only an aggregate digest and file count come back. The kill
is sent only to the process group recorded in app.pid, after the VM confirms it is
this user's resident app for this fixture (and, in flight, that the one funding
helper is still the verified member of that group). Nothing else is signalled.
"""
import contextlib
import json
import re
import time

from litecoin_remote_fixture import RemoteLitecoinAppFixture

TXID = re.compile(r"[0-9a-f]{64}\Z")
DIGEST = re.compile(r"[0-9a-f]{64}\Z")
RETAINED = ("tradeId", "quoteCommitment", "configurationDigest", "retainedPayment", "retainedReview", "retainedTxid")
# Idle and safe to interrupt: signed but unsent, or relay acknowledged.
BOUNDARIES = ("sign", "send", "retry")
# LitecoinLightFundingJob starts the helper as: <helper> --dex-funding-send-stdio-v1 <config>
SEND_FLAG = "--dex-funding-send-stdio-v1"


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


SNAPSHOT = r'''
import hashlib,json,os,stat,sys
app,launcher,trade=sys.argv[1:]
def load(path,limit=16777216):
    info=os.lstat(path)
    if not stat.S_ISREG(info.st_mode) or info.st_uid!=os.getuid() or info.st_size>limit:
        sys.exit("Protected file is not an owned regular file")
    with open(path,"rb") as f:
        return f.read()
def digest(path):
    return hashlib.sha256(load(path)).hexdigest()
def aggregate(records):
    data=json.dumps(sorted(records),separators=(",",":")).encode()
    return {"digest":hashlib.sha256(data).hexdigest(),"count":len(records)}
try:
    manifest=json.loads(load(app,1048576))
    journal=json.loads(load(manifest["journalConfigFile"],1048576))
    if journal.get("tradeId")!=trade:
        sys.exit("Journal names a different trade")
    meta=journal["metadataDirectory"]
    protected={"app":app,"journal":manifest["journalConfigFile"],"payment":manifest["paymentConfigFile"],
        "quote":journal["startQuoteFile"],"intent":os.path.join(meta,trade+".ltc-recovery-intent"),
        "wallet-record":os.path.join(meta,trade+".ltc-light-wallet.json"),
        "ui-payment":os.path.join(meta,"ltc-light-dex-ui-"+trade+"-payment.json"),
        "ui-review":os.path.join(meta,"ltc-light-dex-ui-"+trade+"-review.json")}
    metadata=[(name,digest(path)) for name,path in protected.items()]
    wallet=json.loads(load(json.loads(load(launcher,1048576))["configFile"],1048576))["wallet"]
    folders={"spend":wallet["spendDirectory"],"addressbook":wallet["addressBookDirectory"],"vault":wallet["vault"]["directory"]}
except (KeyError,TypeError,ValueError,OSError):
    sys.exit("Protected trade or wallet files are missing or malformed")
records=[]
for name,folder in folders.items():
    info=os.lstat(folder)
    if not stat.S_ISDIR(info.st_mode) or info.st_uid!=os.getuid():
        sys.exit("Wallet folder is not an owned directory")
    for base,dirs,files in os.walk(folder):
        for entry in dirs:
            if os.path.islink(os.path.join(base,entry)):
                sys.exit("Wallet folder contains a linked directory")
        for entry in files:
            path=os.path.join(base,entry)
            records.append((name,os.path.relpath(path,folder),digest(path)))
print(json.dumps({"wallet":aggregate(records),"metadata":aggregate(metadata)}))
'''

# Shared by HELPER and KILL. PREFIX is set by the script that includes it. The
# helper flag is a constant here, never an argument, so this scanner's own
# command line never contains it as an element.
PROCESS = r'''
import json,os,select,signal,stat,sys,time
FLAG=b"--dex-funding-send-stdio-v1"
def refuse(reason):
    sys.exit(PREFIX+reason)
def owned_record(directory):
    record=os.path.join(directory,"app.pid")
    try:
        info=os.lstat(record)
    except FileNotFoundError:
        refuse("no app.pid")
    if not stat.S_ISREG(info.st_mode) or info.st_uid!=os.getuid() or info.st_size>64:
        refuse("app.pid is not an owned regular file")
    with open(record,"rb") as f:
        fields=f.read(65).split()
    if len(fields)!=2 or not all(f.isdigit() and len(f)<=10 for f in fields):
        refuse("malformed app.pid")
    group,app=map(int,fields)
    if group<=1 or app<=1 or group==app:
        refuse("malformed app.pid")
    return group,app
def proc_fields(pid):
    with open("/proc/%d/stat"%pid,"rb") as f:
        return f.read().rsplit(b")",1)[1].split()
def funding_helpers(binary):
    """Every live process of this user running binary with the send flag."""
    real=os.path.realpath(binary)
    found=[]
    for entry in os.listdir("/proc"):
        if not entry.isdigit() or int(entry)==os.getpid():
            continue
        pid=int(entry)
        try:
            if os.stat("/proc/%d"%pid).st_uid!=os.getuid():
                continue
            if os.path.realpath("/proc/%d/exe"%pid)!=real:
                continue
            with open("/proc/%d/cmdline"%pid,"rb") as f:
                argv=f.read(65536).split(b"\0")
            fields=proc_fields(pid)
        except OSError:
            continue
        if FLAG in argv and fields[0]!=b"Z":
            found.append({"pid":pid,"ppid":int(fields[1]),"group":int(fields[2]),"start":int(fields[19])})
    return found
'''

HELPER = 'PREFIX="Refusing to interrupt relay: "\n' + PROCESS + r'''
directory,binary=sys.argv[1:]
group,app=owned_record(directory)
try:
    if os.getpgid(app)!=group:
        refuse("app is outside the recorded owned group")
except ProcessLookupError:
    refuse("stale process group")
found=funding_helpers(binary)
if len(found)!=1:
    refuse("expected exactly one owned funding helper, found %d"%len(found))
helper=found[0]
if helper["group"]!=group or helper["ppid"]!=app:
    refuse("funding helper is not a child of the resident app in its owned group")
print(json.dumps({"pid":helper["pid"],"start":helper["start"],"group":group,"app":app}))
'''

KILL = 'PREFIX="Refusing to signal resident app: "\n' + PROCESS + r'''
directory,binary,fixture=sys.argv[1:4]
expected=sys.argv[4:]  # in flight: helper binary, pid, start time
if expected and (len(expected)!=3 or not all(f.isdigit() and len(f)<=20 for f in expected[1:])):
    refuse("malformed funding helper identity")
group,app=owned_record(directory)
handles=[]
try:
    if hasattr(os,"pidfd_open"):
        handles=[os.pidfd_open(group),os.pidfd_open(app)]
    for pid in (group,app):
        if os.getpgid(pid)!=group or os.stat("/proc/%d"%pid).st_uid!=os.getuid():
            refuse("process is outside the recorded owned group")
    if os.path.realpath("/proc/%d/exe"%app)!=os.path.realpath(binary):
        refuse("executable differs")
    with open("/proc/%d/cmdline"%app,"rb") as f:
        if b"actualLitecoinAppSetup" not in f.read(65536).split(b"\0"):
            refuse("command differs")
    with open("/proc/%d/environ"%app,"rb") as f:
        if ("MONZERO_LTC_APP_FIXTURE="+fixture).encode() not in f.read(1048576).split(b"\0"):
            refuse("fixture environment differs")
    if expected:
        helper,start=int(expected[1]),int(expected[2])
        if hasattr(os,"pidfd_open"):
            handles.append(os.pidfd_open(helper))
        found=funding_helpers(expected[0])
        if len(found)!=1 or found[0]!={"pid":helper,"ppid":app,"group":group,"start":start}:
            refuse("funding helper changed since it was verified")
except (ProcessLookupError,FileNotFoundError):
    refuse("stale process group")
except PermissionError:
    refuse("process is not readable by this user")
if handles and select.select(handles,[],[],0)[0]:
    refuse("process exited during verification")
os.killpg(group,signal.SIGKILL)
def members():
    found=0
    for entry in os.listdir("/proc"):
        if entry.isdigit():
            try:
                with open("/proc/"+entry+"/stat","rb") as f:
                    fields=f.read().rsplit(b")",1)[1].split()
            except OSError:
                continue
            if int(fields[2])==group and fields[0]!=b"Z":
                found+=1
    return found
deadline=time.monotonic()+20
while members():
    if time.monotonic()>deadline:
        sys.exit("Resident app group survived SIGKILL")
    time.sleep(0.05)
for handle in handles:
    os.close(handle)
print(json.dumps(dict(killed=True,**({"helper":int(expected[1])} if expected else {}))))
'''

PUBLISHED = 'import os,sys; print("published" if os.path.lexists(sys.argv[1]) else "absent")'

PREPARE = r'''
import hashlib,json,os,stat,sys
root,previous,fixture,start=sys.argv[1:]
def read(path,limit):
    info=os.lstat(path)
    if not stat.S_ISREG(info.st_mode) or info.st_uid!=os.getuid() or info.st_size>limit:
        sys.exit("Resume source is not an owned regular file")
    with open(path,"rb") as f:
        return f.read()
def write(path,data):
    with os.fdopen(os.open(path,os.O_WRONLY|os.O_CREAT|os.O_EXCL,0o600),"wb") as f:
        f.write(data);f.flush();os.fsync(f.fileno())
request=json.loads(read(fixture,16384))
if request.get("residentRole")!="bob" or request.get("residentDirectory")!=previous:
    sys.exit("Resume source names a different resident")
for generation in range(int(start),int(start)+64):
    directory=os.path.join(root,"resident-resumed-%d"%generation)
    session=os.path.join(root,"session-resume-%d.json"%generation)
    if os.path.lexists(session):
        continue
    try:
        os.mkdir(directory,0o700)
    except FileExistsError:
        continue
    break
else:
    sys.exit("No free resume generation")
request.update(resumeResident=True,residentDirectory=directory)
write(session,json.dumps(request,separators=(",",":")).encode())
ledger=os.path.join(previous,"bob-started-trades.json")
copied=os.path.lexists(ledger)
if copied:
    data=read(ledger,16777216)
    write(os.path.join(directory,"bob-started-trades.json"),data)
    if hashlib.sha256(read(os.path.join(directory,"bob-started-trades.json"),16777216)).digest()!=hashlib.sha256(data).digest():
        sys.exit("Copied start ledger differs")
elif "controlPublicKey" in request:
    sys.exit("Testnet start ledger is missing; refusing to start without it")
secret=os.path.join(previous,"light-password")
moved=os.path.lexists(secret)
if moved:
    info=os.lstat(secret)
    if not stat.S_ISREG(info.st_mode) or info.st_uid!=os.getuid():
        sys.exit("Light password is not an owned regular file")
    os.link(secret,os.path.join(directory,"light-password"));os.unlink(secret)
for path in (directory,previous,root):
    handle=os.open(path,os.O_RDONLY|os.O_DIRECTORY)
    try:
        os.fsync(handle)
    finally:
        os.close(handle)
print(json.dumps({"generation":generation,"directory":directory,"fixture":session,"ledgerCopied":copied,"passwordMoved":moved}))
'''


def snapshot(workspace, light_wallet, trade):
    value = json.loads(workspace.run(['python3', '-I', '-c', SNAPSHOT, workspace.path('app/app.json'),
        light_wallet.setup['launcher'], trade], timeout=300))
    require(set(value) == {'wallet', 'metadata'}, 'Unexpected VM snapshot report')
    for part in value.values():
        require(set(part) == {'digest', 'count'} and isinstance(part['digest'], str) and DIGEST.match(part['digest'])
            and type(part['count']) is int and part['count'] > 0, 'Unexpected VM snapshot report')
    return value


def find_helper(workspace, part, helper):
    """The one live send helper on the VM: a child of the recorded app in its owned group."""
    value = json.loads(workspace.run(['python3', '-I', '-c', HELPER, str(part.directory), str(helper)], timeout=60))
    require(set(value) == {'pid', 'start', 'group', 'app'} and all(type(v) is int and v > 0 for v in value.values()),
        'Unexpected VM funding helper report')
    return value


def kill_owned(workspace, part, helper=None):
    """helper (in flight): {'binary', 'pid', 'start'} from find_helper; re-verified before the kill."""
    argv = ['python3', '-I', '-c', KILL, str(part.directory), str(part.binary), str(part.fixture)]
    expected = {'killed': True}
    if helper is not None:
        argv += [str(helper['binary']), str(helper['pid']), str(helper['start'])]
        expected['helper'] = helper['pid']
    answer = json.loads(workspace.run(argv, timeout=60))
    require(answer == expected, 'VM did not confirm the resident app group stopped')


def published(workspace, path):
    answer = workspace.run(['python3', '-I', '-c', PUBLISHED, str(path)], timeout=60)
    require(answer in (b'published\n', b'absent\n'), 'Unexpected VM response check')
    return answer == b'published\n'


def stop_supervisor(part):
    """Retire the old local ssh supervisor. Its remote group is already gone."""
    part.process.release()
    try:
        part.process.wait(timeout=30)
    except Exception:
        part.process.popen.kill()
        part.process.wait(timeout=10)
    part.close()


def prepare(workspace, part):
    value = json.loads(workspace.run(['python3', '-I', '-c', PREPARE, str(workspace.root), str(part.directory),
        str(part.fixture), str(part.generation + 1)], timeout=60))
    require(set(value) == {'generation', 'directory', 'fixture', 'ledgerCopied', 'passwordMoved'}
        and type(value['generation']) is int and value['generation'] > part.generation
        and value['directory'] == workspace.path(f"resident-resumed-{value['generation']}")
        and value['fixture'] == workspace.path(f"session-resume-{value['generation']}.json")
        and type(value['ledgerCopied']) is bool and type(value['passwordMoved']) is bool,
        'Unexpected VM resume preparation report')
    return value


def reopen(part, prepared, function_timeout_ms):
    """Same host alias, binary, TLS root, preferences and local transport settings."""
    local = part.local_directory / f"remote-resumed-{prepared['generation']}"
    local.mkdir(mode=0o700)
    return RemoteLitecoinAppFixture(part.host, part.binary, prepared['fixture'], prepared['directory'], part.processes,
        local, tls_root=part.tls_root, preferences=part.preferences, generation=prepared['generation'],
        function_timeout_ms=function_timeout_ms, ssh=part.ssh, multiplex=part.control is not None,
        ready_timeout=part.ready_timeout, poll_seconds=part.poll_seconds, response_limit=part.response_limit,
        request_limit=part.request_limit, connect_timeout=part.connect_timeout,
        term_grace=getattr(part, 'term_grace', 10), transport_retries=part.transport_retries)


def idle_boundary(funding):
    require(funding.get('configured') and not funding.get('busy'), 'Light funding is not idle')
    require(funding.get('state') == 'complete' and funding.get('operation') in BOUNDARIES,
        'Restart covers only idle signed or acknowledged-broadcast light funding')
    txid = funding.get('retainedTxid')
    require(isinstance(txid, str) and TXID.match(txid) and funding.get('retainedPayment')
        and funding.get('retainedReview'), 'No retained signed light funding to resume')
    result = funding.get('result') or {}
    require(result.get('txid') == txid, 'Last light funding result differs from the retained transaction')
    if funding['operation'] == 'sign':
        require(result.get('signed') is True and result.get('broadcast') is False, 'Signed boundary already broadcast')
    return {field: funding[field] for field in RETAINED}


def pending_send(funding, commitment):
    """The idle fresh send approval for the retained signed bytes, and their digest."""
    require(funding.get('configured') and not funding.get('busy'), 'Light funding is not idle')
    require(funding.get('state') == 'complete' and funding.get('operation') == 'send-review'
        and funding.get('reviewOperation') == 'send-review',
        'Relay interruption covers only an idle fresh send approval')
    txid = funding.get('retainedTxid')
    require(isinstance(txid, str) and TXID.match(txid) and funding.get('retainedPayment')
        and funding.get('retainedReview'), 'No retained signed light funding to send')
    review = funding.get('review') or {}
    require(isinstance(commitment, str) and commitment and review.get('commitment') == commitment,
        'Send approval is stale or names a different commitment')
    details = review.get('details') or {}
    digest = details.get('signedDigest')
    require(details.get('action') == 'send' and details.get('txid') == txid
        and isinstance(digest, str) and DIGEST.match(digest), 'Send approval does not name the retained signed bytes')
    return {field: funding[field] for field in RETAINED}, digest


def _parts(resident, light_wallet):
    alice, bob = resident.parts['alice'], resident.parts['bob']
    require(hasattr(bob, 'signal_remote') and not hasattr(alice, 'signal_remote'),
        'Restart needs a local Alice and a VM-resident Bob')
    require(light_wallet.setup is not None, 'VM light wallet is not provisioned')
    alice_pid = alice.process.pid

    def ping():
        state = alice.call('alice', 'status')
        require(alice.process.pid == alice_pid and alice.process.poll() is None, 'Alice process changed or stopped')
        return state
    return alice, bob, ping


def _idle_states(alice, bob, ping, message):
    states = {'alice': ping(), 'bob': bob.call('bob', 'status')}
    for state in states.values():
        require(not state['app']['busy'] and not state['peer']['busy'], message)
    return states


def _resume(resident, workspace, light_wallet, bob, before, saved, ping, alice_reconnects, function_timeout_ms):
    """Shared by both entry points. Bob's verified group is already dead. Only the
    explicit swap recovery is resumed: nothing here sends, retries or signs."""
    alice = resident.parts['alice']
    stop_supervisor(bob)
    require(bob.process.poll() is not None, 'Old Bob supervisor still running')
    ping()
    prepared = prepare(workspace, bob)
    light_wallet.resident_password_path = prepared['directory'] + '/light-password'
    ping()
    reopened = reopen(bob, prepared, function_timeout_ms)
    resident.parts['bob'] = reopened
    ping()  # Startup may consume most of Alice's command timeout.

    def step(operation, accepted=True, **fields):
        value = reopened.call('bob', operation, accepted=accepted, **fields)
        resident._merge(reopened.last['accepted'])
        ping()
        return value

    step('journal')
    step('wait-negotiation')
    alice.call('alice', 'wait-reconnected', accepted=None, reconnects=alice_reconnects)
    require(alice.last['accepted'] and alice.last['alice']['reconnects'] > alice_reconnects,
        'Alice did not rebuild her session with the reopened Bob')
    after_reconnects = alice.last['alice']['reconnects']
    step('reserve', accepted=False)
    step('light-resume-wallet', helper=light_wallet.helper, digest=light_wallet.digest,
        launcher=light_wallet.setup['launcher'])
    restored = step('light-select')['lightFunding']
    for field in RETAINED:
        require(restored[field] == before[field], 'Restart changed retained local funding: ' + field)
    require(not restored['review'] and not restored['reviewOperation'], 'Restart reused an old approval')
    require(not restored.get('busy'), 'Reopened light funding is busy')
    step('light-resume-recovery')
    step('wait-recovery')
    final = ping()
    require(final['reconnects'] >= after_reconnects, 'Alice reconnect count went backwards')
    require(snapshot(workspace, light_wallet, before['tradeId']) == saved,
        'Bob restart changed saved wallet, signatures, allocations or protected trade metadata')
    return prepared, final


def restart_bob(resident, workspace, light_wallet, function_timeout_ms=3600000):
    alice, bob, ping = _parts(resident, light_wallet)
    states = _idle_states(alice, bob, ping, 'Restart requires both roles idle')
    before = idle_boundary(states['bob']['lightFunding'])
    alice_reconnects = states['alice']['reconnects']
    saved = snapshot(workspace, light_wallet, before['tradeId'])

    kill_owned(workspace, bob)
    prepared, final = _resume(resident, workspace, light_wallet, bob, before, saved, ping, alice_reconnects,
        function_timeout_ms)
    resident._merge(True)
    return {'schema': 'monzero-ltc-remote-light-restart-v1', 'role': 'bob', 'generation': prepared['generation'],
        'tradeId': before['tradeId'], 'retainedTxid': before['retainedTxid'], 'boundary': states['bob']['lightFunding']['operation'],
        'walletFiles': saved['wallet'], 'protectedMetadata': saved['metadata'],
        'ownedGroupVerifiedBeforeKill': True, 'startLedgerCopied': prepared['ledgerCopied'],
        'lightPasswordMovedOnVm': prepared['passwordMoved'], 'retainedFundingUnchanged': True,
        'oldApprovalAbsent': True, 'recoveryResumedExplicitly': True,
        'alicePidUnchanged': True, 'aliceReconnectsBefore': alice_reconnects, 'aliceReconnectsAfter': final['reconnects'],
        'executable': False}


def _disarm(peer):
    with getattr(peer, 'broadcast_lock', None) or contextlib.nullcontext():
        peer.hold_next_broadcast_reply = False


def _wait_barrier(workspace, bob, peer, response, timeout, poll_seconds):
    """True once the peer holds a node-accepted broadcast. False if Bob answered
    first (a stale or refused approval), stopped, or the bound expired."""
    deadline = time.monotonic() + timeout
    while not peer.broadcast_held.wait(max(0, min(poll_seconds, deadline - time.monotonic()))):
        if time.monotonic() >= deadline or bob.process.poll() is not None or published(workspace, response):
            return peer.broadcast_held.is_set()
    return True


def interrupt_bob_after_acceptance(resident, workspace, light_wallet, peer, commitment, function_timeout_ms=3600000,
        barrier_timeout=180, poll_seconds=5, release_timeout=30):
    """Submit one fresh send approval and kill Bob while the node-accepted reply is held.

    The caller passes the commitment of the current send review and the owned
    peer with the staged hooks (hold_next_broadcast, accepted/held digests,
    holding). If the send never reaches the barrier, this fails closed and Bob
    is not killed. Afterwards the funding outcome is deliberately uncertain;
    the driver reconciles it with fresh approvals.
    """
    alice, bob, ping = _parts(resident, light_wallet)
    require(callable(getattr(peer, 'hold_next_broadcast', None)) and hasattr(peer, 'held_broadcast_digests')
        and hasattr(peer, 'holding'), 'Relay interruption needs the owned peer broadcast hold hooks')
    states = _idle_states(alice, bob, ping, 'Relay interruption must start with both roles idle')
    before, expected = pending_send(states['bob']['lightFunding'], commitment)
    alice_reconnects = states['alice']['reconnects']
    accepted_from, held_from = len(peer.accepted_broadcast_digests), len(peer.held_broadcast_digests)
    dropped_from = getattr(peer, 'dropped_broadcast_replies', 0)

    peer.hold_next_broadcast()
    reached = killed = False
    try:
        response = bob.enqueue('bob', 'light-send', commitment=commitment)
        reached = _wait_barrier(workspace, bob, peer, response, barrier_timeout, poll_seconds)
        if reached:
            require(bob.process.poll() is None, 'Bob supervisor stopped at the broadcast barrier')
            require(peer.accepted_broadcast_digests[accepted_from:] == [expected]
                and peer.held_broadcast_digests[held_from:] == [expected],
                'Held node-accepted broadcast is not the retained signed transaction')
            require(not published(workspace, response), 'Bob published a relay response before interruption')
            helper = find_helper(workspace, bob, light_wallet.helper)
            saved = snapshot(workspace, light_wallet, before['tradeId'])  # durable broadcast intent included
            require(peer.holding == 1 and not peer.release_broadcast_reply.is_set(),
                'Held broadcast reply was released or timed out before interruption')
            kill_owned(workspace, bob, dict(helper, binary=light_wallet.helper))
            killed = True
            require(peer.holding == 1, 'Held broadcast reply timed out before the kill was confirmed')
            require(not published(workspace, response), 'Interrupted Bob published a completed relay response')
    finally:
        _disarm(peer)
        peer.release_broadcast_reply.set()  # the staged peer then closes without replying
    if not reached:
        late = len(peer.accepted_broadcast_digests) - accepted_from
        raise RuntimeError('Bob light-send did not reach the node-accepted broadcast barrier (stale or refused '
            'approval, or timeout); Bob was not interrupted; %d acceptance(s) observed at the bound. '
            'The pending send may still complete; wait for completion and reconcile before any retry' % late)
    require(killed, 'Bob was not interrupted')
    deadline = time.monotonic() + release_timeout
    while peer.holding:
        require(time.monotonic() < deadline, 'Held broadcast reply was not dropped after release')
        time.sleep(0.05)
    require(getattr(peer, 'dropped_broadcast_replies', dropped_from + 1) == dropped_from + 1,
        'Owned peer did not drop exactly the held reply')

    prepared, final = _resume(resident, workspace, light_wallet, bob, before, saved, ping, alice_reconnects,
        function_timeout_ms)
    require(not published(workspace, response), 'Old Bob relay response appeared after reopen')
    require(len(peer.accepted_broadcast_digests) == accepted_from + 1, 'Recovery broadcast the funding again')
    resident._merge(True)
    return {'schema': 'monzero-ltc-remote-light-inflight-v1', 'role': 'bob', 'generation': prepared['generation'],
        'tradeId': before['tradeId'], 'retainedTxid': before['retainedTxid'], 'boundary': 'node-accepted-broadcast',
        'heldSignedDigest': expected, 'acceptedBroadcasts': 1, 'replyWithheld': True,
        'walletFiles': saved['wallet'], 'protectedMetadata': saved['metadata'],
        'ownedGroupVerifiedBeforeKill': True, 'fundingHelperVerifiedBeforeKill': True,
        'replyReleasedAfterKill': True, 'oldResponsePublished': False,
        'startLedgerCopied': prepared['ledgerCopied'], 'lightPasswordMovedOnVm': prepared['passwordMoved'],
        'retainedFundingUnchanged': True, 'oldApprovalAbsent': True, 'recoveryResumedExplicitly': True,
        'resubmitted': False, 'reconcileRequired': True,
        'alicePidUnchanged': True, 'aliceReconnectsBefore': alice_reconnects, 'aliceReconnectsAfter': final['reconnects'],
        'executable': False}


def release_password(workspace, light_wallet):
    """release_password for the current resident, wherever restarts moved it."""
    path = getattr(light_wallet, 'resident_password_path', None) or workspace.path('resident/light-password')
    workspace.run(['python3', '-I', '-c', 'import os,sys; os.unlink(sys.argv[1])', path])
