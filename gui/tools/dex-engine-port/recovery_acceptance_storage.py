"""Keep cold-boot evidence on explicitly selected disk-backed storage."""
import pathlib,subprocess

def require_recovery_storage(parent,keep):
    if not keep or parent is None:
        raise RuntimeError('Power-loss acceptance requires --keep and explicit --temp-parent')
    path=pathlib.Path(parent).resolve(strict=True)
    if not path.is_dir():raise RuntimeError('Recovery storage parent is not a directory')
    kind=subprocess.check_output(['stat','-f','-c','%T','--',str(path)],text=True).strip()
    if kind not in ('btrfs','ext2/ext3','ext4','xfs','f2fs','zfs'):
        raise RuntimeError('Power-loss acceptance requires persistent storage, not memory-backed storage')
    return path
