"""Host-local cooperative lease for the sole designated DEX VM."""
import fcntl
import pwd
import os
import pathlib
import stat
from recovery_run_registry import RecoveryRunRegistry


def active_legacy_traders():
    """Detect pre-lease fixture processes too; never signal or change them."""
    found=[]
    for proc in pathlib.Path('/proc').iterdir():
        if not proc.name.isdigit() or int(proc.name)==os.getpid():continue
        try:
            if proc.stat().st_uid!=os.getuid():continue
            args=(proc/'cmdline').read_bytes().split(b'\0')
        except (FileNotFoundError,ProcessLookupError):continue
        if not any(pathlib.PurePosixPath(os.fsdecode(a)).name=='test_litecoin_live_swap.py' for a in args if a):continue
        if b'--remote-host' in args:
            i=args.index(b'--remote-host')
            if i+1<len(args) and args[i+1]==b'monzero-dexlab-vm':found.append(int(proc.name))
    return found


class LabVmLease:
    def __init__(self, exclusive, directory=None, legacy_probe=active_legacy_traders):
        self.exclusive=exclusive;self.fd=None;self.legacy_probe=legacy_probe
        self.directory=pathlib.Path(directory or ('/run/user/%d'%os.getuid()))
        self.recovery_directory=(self.directory/'recovery-runs') if directory is not None else pathlib.Path(pwd.getpwuid(os.getuid()).pw_dir)/'.local/state/monzero-dexlab-recovery'

    def __enter__(self):
        m=self.directory.lstat()
        if not stat.S_ISDIR(m.st_mode) or m.st_uid!=os.getuid() or m.st_mode&0o077:
            raise RuntimeError('VM lease directory is not private')
        fd=os.open(self.directory/'monzero-dexlab.lock',os.O_RDWR|os.O_CREAT|os.O_NOFOLLOW,0o600)
        try:
            m=os.fstat(fd)
            if not stat.S_ISREG(m.st_mode) or m.st_uid!=os.getuid() or m.st_mode&0o077 or m.st_nlink!=1:
                raise RuntimeError('Unsafe VM lease file')
            fcntl.flock(fd,(fcntl.LOCK_EX if self.exclusive else fcntl.LOCK_SH)|fcntl.LOCK_NB)
            if self.exclusive and self.legacy_probe():
                raise RuntimeError('Another VM trade is active; power-loss fixture refused')
        except BaseException:
            os.close(fd);raise
        try:
            self.registry=RecoveryRunRegistry(self.recovery_directory)
            if not self.exclusive:
                if self.registry.current() is not None:
                    raise RuntimeError('An unfinished VM recovery blocks new trades')
        except BaseException:
            os.close(fd);raise
        self.fd=fd
        return self

    def require_exclusive(self):
        if self.fd is None or not self.exclusive:
            raise RuntimeError('Power loss requires a held exclusive VM lease')
        if self.legacy_probe():
            raise RuntimeError('Another VM trade appeared; refusing power loss')

    def begin_recovery(self,path,digest):
        self.require_exclusive();self.registry.begin(path,digest)

    def require_recovery(self,path,digest):
        self.require_exclusive();self.registry.require(path,digest)

    def abandon_unissued_recovery(self,path,digest):
        self.require_exclusive();self.registry.abandon_unissued(path,digest)

    def complete_recovery(self,path,digest):
        self.require_exclusive();self.registry.complete(path,digest)

    def __exit__(self,*args):
        if self.fd is not None:os.close(self.fd);self.fd=None
