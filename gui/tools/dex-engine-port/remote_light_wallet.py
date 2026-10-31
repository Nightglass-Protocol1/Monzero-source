"""VM-local light wallet fixture. Only public paths, addresses and balances return."""
import hashlib
import json
import pathlib

from light_wallet_provision import COIN_NETWORKS


INSTALL = r'''
import hashlib,json,os,pathlib,subprocess,sys
source,target,expected=sys.argv[1:]
data=pathlib.Path(source).read_bytes()
if hashlib.sha256(data).hexdigest()!=expected:
    sys.exit("VM light helper digest mismatch")
with os.fdopen(os.open(target,os.O_WRONLY|os.O_CREAT|os.O_EXCL,0o700),"wb") as f:
    f.write(data);f.flush();os.fsync(f.fileno())
p=subprocess.run([target],input=b"",capture_output=True,timeout=15,env={"LANG":"C"})
if p.returncode!=1 or p.stderr!=b"Litecoin light-chain request failed; reload validated state before retrying\n":
    sys.exit("VM light helper CPU/loader preflight failed")
print(json.dumps({"helperSha256":expected}))
'''

CREATE = r'''
import json,sys
sys.path.insert(0,sys.argv[1])
from light_wallet_provision import provision,ProvisionError
try:
    result=provision(sys.argv[2],sys.argv[3],sys.argv[4],int(sys.argv[5]),int(sys.argv[6]),sys.argv[7],coin=sys.argv[8])
except ProvisionError as error:
    sys.exit("VM light wallet creation failed: "+str(error))
except Exception:
    sys.exit("VM light wallet creation failed")
print(json.dumps(result))
'''

REFRESH = r'''
import json,sys
sys.path.insert(0,sys.argv[1])
from light_wallet_provision import refresh,ProvisionError
try:
    result=refresh(sys.argv[2],sys.argv[3],sys.argv[4],maximum_download_headers=100000,coin=sys.argv[5])
except ProvisionError as error:
    sys.exit("VM light wallet observation failed: "+str(error))
except Exception:
    sys.exit("VM light wallet observation failed")
print(json.dumps(result))
'''


class RemoteLightWallet:
    def __init__(self, workspace, remote_helper, reference_helper, tls_directory, network, coin='ltc'):
        if network not in COIN_NETWORKS.get(coin, ()):
            raise ValueError('Only test chains are allowed')
        if not pathlib.PurePosixPath(remote_helper).is_absolute():
            raise ValueError('VM helper must be an absolute path')
        self.workspace, self.remote_helper = workspace, remote_helper
        self.reference_helper = pathlib.Path(reference_helper)
        self.tls_directory, self.network, self.coin = pathlib.Path(tls_directory), network, coin
        self.helper = workspace.path('light-helper')
        self.tls_root = workspace.path('light-tls/root.pem')
        self.directory = workspace.path('local-light-wallet')
        self.digest = hashlib.sha256(self.reference_helper.read_bytes()).hexdigest()
        self.setup = None

    def install(self):
        answer = json.loads(self.workspace.run(
            ['python3', '-I', '-c', INSTALL, self.remote_helper, self.helper, self.digest], timeout=60))
        if answer != {'helperSha256': self.digest}:
            raise RuntimeError('VM helper installation report mismatch')
        self.workspace.upload(self.tls_directory / 'root.pem', 'light-tls/root.pem')
        self.workspace.upload(pathlib.Path(__file__).with_name('light_wallet_provision.py'),
                              'light_wallet_provision.py')

    def create(self, peer_port, maximum_height=10_000_000):
        if type(peer_port) is not int or not 0 < peer_port < 65536:
            raise ValueError('Invalid Electrum port')
        if type(maximum_height) is not int or maximum_height <= 0:
            raise ValueError('Invalid maximum height')
        value = json.loads(self.workspace.run(['python3', '-I', '-c', CREATE, str(self.workspace.root),
            self.helper, self.directory, self.network, str(maximum_height), str(peer_port), self.tls_root, self.coin],
            timeout=600))
        if set(value) != {'address', 'helperSha256', 'configurationFile', 'launcher'} \
                or value['helperSha256'] != self.digest:
            raise RuntimeError('Unexpected VM wallet provisioning report')
        for field in ('configurationFile', 'launcher'):
            path = pathlib.PurePosixPath(value[field])
            if '..' in path.parts or not path.is_relative_to(self.directory):
                raise RuntimeError('VM wallet path escaped its private directory')
        if not isinstance(value['address'], str) or not 10 <= len(value['address']) <= 128:
            raise RuntimeError('Invalid VM receive address')
        self.setup = value
        return value

    def attach(self, resident):
        if self.setup is None:
            raise RuntimeError('VM wallet is not provisioned')
        # Move the secret within the VM. It is never read by the host driver.
        self.workspace.run(['python3', '-I', '-c',
            'import os,sys; os.link(sys.argv[1],sys.argv[2]); os.unlink(sys.argv[1])',
            self.directory + '/light-password', self.workspace.path('resident/light-password')])
        resident.call('bob', 'light-configure', helper=self.helper,
                      digest=self.digest, launcher=self.setup['launcher'])
        resident.call('bob', 'light-select')
        state = resident.call('bob', 'light-terms')['lightFunding']
        if state['state'] != 'complete' or state['terms']['refundAddress'] != self.setup['address']:
            raise RuntimeError('VM wallet refund address differs from signed terms')

    def release_password(self):
        self.workspace.run(['python3', '-I', '-c', 'import os,sys; os.unlink(sys.argv[1])',
                            self.workspace.path('resident/light-password')])

    def refresh(self):
        if self.setup is None:
            raise RuntimeError('VM wallet is not provisioned')
        value = json.loads(self.workspace.run(['python3', '-I', '-c', REFRESH, str(self.workspace.root),
            self.helper, self.setup['launcher'], self.tls_root, self.coin], timeout=600))
        if set(value) != {'observedConfirmedAtomic', 'pendingReceivedAtomic'} \
                or any(not isinstance(v, str) or not v.isascii() or not v.isdigit() for v in value.values()):
            raise RuntimeError('Unexpected VM wallet balance report')
        return value
