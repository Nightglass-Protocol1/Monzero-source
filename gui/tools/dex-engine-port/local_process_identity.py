"""Linux process identity retained across a VM-only recovery operation."""
import os
import pathlib


def local_process_identity(process):
    pid=process.pid
    if type(pid) is not int or pid<=1 or process.poll() is not None:
        raise RuntimeError('Host trader is not a live process')
    proc=pathlib.Path('/proc')/str(pid)
    fields=(proc/'stat').read_text().rsplit(')',1)[1].split()
    if fields[0]=='Z' or proc.stat().st_uid!=os.getuid():
        raise RuntimeError('Host trader process identity is invalid')
    result={'pid':pid,'starttime':fields[19],
            'bootId':pathlib.Path('/proc/sys/kernel/random/boot_id').read_text().strip()}
    if process.poll() is not None:
        raise RuntimeError('Host trader exited during identity capture')
    return result


def require_local_process(process, expected):
    if local_process_identity(process)!=expected:
        raise RuntimeError('Host trader differs from retained checkpoint')


def require_local_process_dead(expected):
    """Prove the retained process is gone; unreadable state is not proof."""
    import uuid
    if (not isinstance(expected,dict) or set(expected)!={'pid','starttime','bootId'}
            or type(expected['pid']) is not int or expected['pid']<=1
            or not isinstance(expected['starttime'],str) or not expected['starttime'].isdigit()
            or str(uuid.UUID(expected['bootId']))!=expected['bootId']):
        raise RuntimeError('Invalid retained host process identity')
    boot=pathlib.Path('/proc/sys/kernel/random/boot_id').read_text().strip()
    if boot!=expected['bootId']:return
    try:
        fields=(pathlib.Path('/proc')/str(expected['pid'])/'stat').read_text().rsplit(')',1)[1].split()
    except FileNotFoundError:return
    if fields[19]!=expected['starttime'] or fields[0]=='Z':return
    raise RuntimeError('Original host trader is still alive')
