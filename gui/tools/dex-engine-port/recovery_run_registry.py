"""Persistent designated-VM recovery ownership; caller holds the VM lease."""
import os
import pathlib
import re
import stat
from remote_recovery_checkpoint import persist_checkpoint,read_private_json
from vm_power_loss import VM_UUID

class RecoveryRunRegistry:
    def __init__(self,directory):
        self.directory=pathlib.Path(directory)
        # Persist newly created parent entries as well as the eventual record.
        for path in reversed([self.directory,*self.directory.parents]):
            try:info=path.lstat()
            except FileNotFoundError:
                path.mkdir(mode=0o700)
                fd=os.open(path.parent,os.O_RDONLY|os.O_DIRECTORY|os.O_NOFOLLOW)
                try:os.fsync(fd)
                finally:os.close(fd)
            else:
                if not stat.S_ISDIR(info.st_mode):raise RuntimeError('Linked recovery registry ancestor')
        info=self.directory.lstat()
        if not stat.S_ISDIR(info.st_mode) or info.st_uid!=os.getuid() or info.st_mode&0o077:
            raise RuntimeError('Unsafe persistent recovery registry')
        self.active=self.directory/'active.json'
    def binding(self,path,digest):
        path=pathlib.Path(path)
        if not path.is_absolute() or '..' in path.parts or not isinstance(digest,str) or not re.fullmatch('[0-9a-f]{64}',digest):
            raise ValueError('Invalid recovery run binding')
        return {'schema':'monzero-recovery-run-v1','vmUuid':VM_UUID,'checkpoint':str(path),'sha256':digest}
    def current(self):
        try:value=read_private_json(self.active)
        except FileNotFoundError:return None
        if value!=self.binding(value.get('checkpoint',''),value.get('sha256','')):
            raise RuntimeError('Invalid active recovery record')
        return value
    def completed(self,value):
        return self.directory/('completed-'+value['sha256']+'.json')
    def abandoned(self,value):
        return self.directory/('abandoned-'+value['sha256']+'.json')
    def quarantined(self,value):
        return self.directory/('quarantined-'+value['sha256']+'.json')
    def clear_completed(self):
        value=self.current()
        if value is None:return
        try:done=read_private_json(self.completed(value))
        except FileNotFoundError:
            try:done=read_private_json(self.abandoned(value))
            except FileNotFoundError:
                try:quarantine=read_private_json(self.quarantined(value))
                except FileNotFoundError:return
                if set(quarantine)!={'binding','observation'} or not isinstance(quarantine['observation'],dict):
                    raise RuntimeError('Invalid recovery quarantine marker')
                done=quarantine['binding']
        if done!=value:raise RuntimeError('Recovery completion binding mismatch')
        self.active.unlink()
        fd=os.open(self.directory,os.O_RDONLY|os.O_DIRECTORY|os.O_NOFOLLOW)
        try:os.fsync(fd)
        finally:os.close(fd)
    def begin(self,path,digest):
        value=self.binding(path,digest)
        self.clear_completed()
        if self.current() is not None:raise RuntimeError('An unfinished VM recovery already owns the lab')
        if self.completed(value).exists():raise RuntimeError('This recovery checkpoint was already completed')
        if self.abandoned(value).exists():raise RuntimeError('This recovery checkpoint was already abandoned')
        if self.quarantined(value).exists():raise RuntimeError('This recovery checkpoint was already quarantined')
        persist_checkpoint(self.active,value)
    def require(self,path,digest):
        if self.current()!=self.binding(path,digest):raise RuntimeError('Recovery run does not own the lab')
    def complete(self,path,digest):
        self.require(path,digest);value=self.binding(path,digest)
        try:persist_checkpoint(self.completed(value),value)
        except FileExistsError:
            if read_private_json(self.completed(value))!=value:raise RuntimeError('Recovery completion binding mismatch')
        self.clear_completed()

    def abandon_unissued(self,path,digest):
        """Caller proves no power action was issued; never use after uncertain action."""
        self.require(path,digest);value=self.binding(path,digest)
        try:persist_checkpoint(self.abandoned(value),value)
        except FileExistsError:
            if read_private_json(self.abandoned(value))!=value:raise RuntimeError('Recovery abandonment binding mismatch')
        self.clear_completed()

    def quarantine(self,path,digest,observation):
        self.require(path,digest);value=self.binding(path,digest)
        marker={'binding':value,'observation':observation}
        try:persist_checkpoint(self.quarantined(value),marker)
        except FileExistsError:
            prior=read_private_json(self.quarantined(value))
            if set(prior)!={'binding','observation'} or prior['binding']!=value or not isinstance(prior['observation'],dict):
                raise RuntimeError('Recovery quarantine binding mismatch')
        self.clear_completed()
