"""Private, durable fixture checkpoint; not an active-trade backup/export."""
import json
import os
import pathlib
import stat

CAPTURE = r'''
import hashlib,json,os,pathlib,stat,sys
root=pathlib.Path(sys.argv[1])
names=json.load(sys.stdin)
if not isinstance(names,list) or not names or len(names)!=len(set(names)):
    sys.exit("Invalid checkpoint input list")
result={}
for name in names:
    rel=pathlib.PurePosixPath(name)
    if rel.is_absolute() or not rel.parts or '..' in rel.parts:
        sys.exit("Invalid checkpoint path")
    path=root
    for part in ('',*rel.parts[:-1]):
        if part:path=path/part
        m=path.lstat()
        if not stat.S_ISDIR(m.st_mode) or m.st_uid!=os.getuid() or m.st_mode&0o077:
            sys.exit("Unsafe checkpoint directory")
    fd=os.open(root/rel,os.O_RDONLY|os.O_NOFOLLOW|os.O_NONBLOCK)
    with os.fdopen(fd,'rb') as f:
        before=os.fstat(f.fileno())
        forbidden=0o022 if name in ('kit-ltc/card.json','kit-ltc.sig') else 0o077
        if not stat.S_ISREG(before.st_mode) or before.st_uid!=os.getuid() or before.st_mode&forbidden or before.st_nlink!=1 or before.st_size>16777216:
            sys.exit("Unsafe checkpoint file")
        digest=hashlib.sha256(f.read(16777217)).hexdigest()
        after=os.fstat(f.fileno())
        if (before.st_size,before.st_mtime_ns,before.st_ctime_ns)!=(after.st_size,after.st_mtime_ns,after.st_ctime_ns):
            sys.exit("Checkpoint input changed during read")
    result[name]=digest
print(json.dumps(result,sort_keys=True))
'''


def capture_bindings(workspace, names):
    """Only SHA-256 bindings leave the VM; caller supplies the complete file set."""
    value = json.loads(workspace.run(['python3', '-I', '-c', CAPTURE, str(workspace.root)],
                                     json.dumps(names).encode(), timeout=60))
    if set(value) != set(names):
        raise RuntimeError('Incomplete remote checkpoint bindings')
    return value


def persist_checkpoint(path, value):
    """Publish a complete fsynced record atomically without replacing evidence.

    Linux renameat2(RENAME_NOREPLACE) avoids both torn final records and the
    extra hardlink left by link/unlink publication after an interruption.
    """
    import ctypes
    import secrets
    path = pathlib.Path(path)
    directory = os.open(path.parent, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW)
    pending = '.recovery-pending-' + secrets.token_hex(16)
    try:
        info = os.fstat(directory)
        if info.st_uid != os.getuid() or info.st_mode & 0o077:
            raise ValueError('Checkpoint parent must be a private owned directory')
        data = json.dumps(value, sort_keys=True, separators=(',', ':'), allow_nan=False).encode()
        fd = os.open(pending, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW,
                     0o600, dir_fd=directory)
        with os.fdopen(fd, 'wb') as stream:
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        libc = ctypes.CDLL(None, use_errno=True)
        rename = libc.renameat2
        rename.argtypes = [ctypes.c_int, ctypes.c_char_p, ctypes.c_int, ctypes.c_char_p, ctypes.c_uint]
        rename.restype = ctypes.c_int
        if rename(directory, os.fsencode(pending), directory, os.fsencode(path.name), 1) != 0:
            error = ctypes.get_errno()
            raise OSError(error, os.strerror(error), str(path))
        os.fsync(directory)
    finally:
        try: os.unlink(pending, dir_fd=directory)
        except FileNotFoundError: pass
        finally: os.close(directory)


def read_private_json(path, *, directory_fd=None):
    """Read bounded, owned, unlinked recovery evidence; reject ambiguous JSON."""
    def object_pairs(pairs):
        value = {}
        for key, item in pairs:
            if key in value:
                raise ValueError('Duplicate recovery JSON key')
            value[key] = item
        return value
    def invalid_constant(value):
        raise ValueError('Non-finite recovery JSON number')
    fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK, dir_fd=directory_fd)
    with os.fdopen(fd, 'rb') as stream:
        before = os.fstat(stream.fileno())
        if (not stat.S_ISREG(before.st_mode) or before.st_uid != os.getuid()
                or before.st_nlink != 1 or before.st_mode & 0o077
                or not 0 < before.st_size <= 16777216):
            raise ValueError('Unsafe recovery evidence file')
        raw = stream.read(16777217)
        after = os.fstat(stream.fileno())
        if (len(raw) != before.st_size or
                (before.st_size, before.st_mtime_ns, before.st_ctime_ns) !=
                (after.st_size, after.st_mtime_ns, after.st_ctime_ns)):
            raise ValueError('Recovery evidence changed during read')
    value = json.loads(raw, object_pairs_hook=object_pairs, parse_constant=invalid_constant)
    if not isinstance(value, dict):
        raise ValueError('Recovery evidence must be an object')
    return value


def read_phase_journal(directory):
    """Read-only history validation, never permission to repeat a VM action.

    The caller must hold the exclusive VM lease. Unknown phases are preserved
    for the coordinator to reject rather than silently skipping evidence.
    """
    import re
    import math
    fd = os.open(directory, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW)
    try:
        info = os.fstat(fd)
        if info.st_uid != os.getuid() or info.st_mode & 0o077:
            raise ValueError('Unsafe recovery journal directory')
        def published_names():
            names=[]
            for name in os.listdir(fd):
                if re.fullmatch(r'\.recovery-pending-[0-9a-f]{32}',name):
                    info=os.stat(name,dir_fd=fd,follow_symlinks=False)
                    if (not stat.S_ISREG(info.st_mode) or info.st_uid!=os.getuid()
                            or info.st_mode&0o077 or info.st_nlink!=1):
                        raise ValueError('Unsafe unpublished recovery file')
                    continue  # A crash before atomic publication issued no action.
                names.append(name)
            return sorted(names)
        names = published_names()
        if len(names) > 1000:
            raise ValueError('Recovery journal is too large')
        entries = []
        for sequence, name in enumerate(names):
            match = re.fullmatch(r'([0-9]{3})-([a-z]+(?:-[a-z]+)*)\.json', name)
            if match is None or int(match[1]) != sequence:
                raise ValueError('Recovery journal sequence is incomplete or ambiguous')
            value = read_private_json(name, directory_fd=fd)
            if (set(value) != {'schema', 'phase', 'sequence', 'recordedAt', 'evidence'}
                    or value['schema'] != 'monzero-vm-recovery-phase-v1'
                    or value['phase'] != match[2]
                    or type(value['sequence']) is not int or value['sequence'] != sequence
                    or type(value['recordedAt']) not in (int, float)
                    or not math.isfinite(value['recordedAt']) or value['recordedAt'] < 0
                    or not isinstance(value['evidence'], dict)):
                raise ValueError('Invalid recovery journal entry')
            entries.append(value)
        if published_names() != names:
            raise ValueError('Recovery journal changed during read')
        return entries
    finally:
        os.close(fd)


def phase_journal_digest(entries):
    import hashlib
    return hashlib.sha256(json.dumps(entries, sort_keys=True,
        separators=(',', ':'), allow_nan=False).encode()).hexdigest()


class RecoveryPhaseJournal:
    """Immutable per-phase records; never overwrite a previous run's evidence."""
    def __init__(self, directory):
        self.directory = pathlib.Path(directory)
        self.directory.mkdir(mode=0o700)
        parent = os.open(self.directory.parent, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW)
        try: os.fsync(parent)
        finally: os.close(parent)
        self.sequence = 0
        self.history_digest = phase_journal_digest([])

    @classmethod
    def attach(cls, directory, *, expected_digest=None):
        value = cls.__new__(cls)
        value.directory = pathlib.Path(directory)
        entries = read_phase_journal(directory)
        value.sequence = len(entries)
        value.history_digest = phase_journal_digest(entries)
        if expected_digest is not None and value.history_digest != expected_digest:
            raise RuntimeError('Recovery journal changed after observation')
        return value

    def record(self, phase, fields):
        import re
        import time
        if not re.fullmatch(r'[a-z]+(?:-[a-z]+)*', phase):
            raise ValueError('Invalid recovery phase')
        value = dict(schema='monzero-vm-recovery-phase-v1', phase=phase,
                     sequence=self.sequence, recordedAt=time.time(), evidence=fields)
        import fcntl
        fd = os.open(self.directory, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW)
        try:
            fcntl.flock(fd, fcntl.LOCK_EX)
            entries = read_phase_journal(self.directory)
            if len(entries) != self.sequence or phase_journal_digest(entries) != self.history_digest:
                raise RuntimeError('Recovery journal advanced; reload before continuing')
            persist_checkpoint(self.directory / ('%03d-%s.json' % (self.sequence, phase)), value)
            self.sequence += 1
            self.history_digest = phase_journal_digest(entries + [value])
        finally:
            os.close(fd)


NATIVE_WALLETS = r'''
import hashlib,json,os,pathlib,stat,sys
root=pathlib.Path(sys.argv[1]);records=[]
for role in ('claim','observer','recipient'):
    directory=root/'native-wallets'/role/'wallets'
    for parent in (root,root/'native-wallets',directory.parent,directory):
        m=parent.lstat()
        if not stat.S_ISDIR(m.st_mode) or m.st_uid!=os.getuid() or m.st_mode&0o077:
            sys.exit('Unsafe native wallet directory')
    for path in sorted(directory.rglob('*')):
        m=path.lstat()
        if stat.S_ISDIR(m.st_mode):
            if m.st_uid!=os.getuid() or m.st_mode&0o077:sys.exit('Unsafe nested wallet directory')
            continue
        fd=os.open(path,os.O_RDONLY|os.O_NOFOLLOW|os.O_NONBLOCK)
        with os.fdopen(fd,'rb') as f:
            before=os.fstat(f.fileno())
            if not stat.S_ISREG(before.st_mode) or before.st_uid!=os.getuid() or before.st_mode&0o077 or before.st_nlink!=1:
                sys.exit('Unsafe native wallet file')
            digest=hashlib.sha256()
            for chunk in iter(lambda:f.read(1048576),b''):digest.update(chunk)
            after=os.fstat(f.fileno())
            if (before.st_size,before.st_mtime_ns,before.st_ctime_ns)!=(after.st_size,after.st_mtime_ns,after.st_ctime_ns):
                sys.exit('Native wallet changed during snapshot')
        records.append((role,str(path.relative_to(directory)),digest.hexdigest()))
if not any(role=='recipient' and name=='payout.keys' for role,name,digest in records):
    sys.exit('Native payout wallet keys are missing')
print(json.dumps({'count':len(records),'digest':hashlib.sha256(json.dumps(records,separators=(',',':')).encode()).hexdigest()}))
'''


def capture_native_wallets(workspace):
    value=json.loads(workspace.run(['python3','-I','-c',NATIVE_WALLETS,str(workspace.root)],timeout=60))
    if set(value)!={'count','digest'} or type(value['count']) is not int or value['count']<=0:
        raise RuntimeError('Invalid native wallet snapshot')
    return value
