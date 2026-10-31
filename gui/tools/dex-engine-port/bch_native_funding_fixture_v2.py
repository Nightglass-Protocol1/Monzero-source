"""Owned XMZ fakechain and authenticated source wallet for BCH paired tests."""
import json
import urllib.request
import pathlib
import subprocess
from test_wallet_recovery import GENESIS, port, ready, rpc

class NativeFundingFixture:
    def __init__(self, root, daemon, wallet_rpc, occupied_port):
        self.root=root;self.processes=[]
        self.daemon_port,self.wallet_port,self.observer_port,p2p=port(),port(),port(),port()
        assert len({self.daemon_port,self.wallet_port,self.observer_port,p2p,occupied_port})==5
        self.login=root/f'monzero-wallet-rpc.{self.wallet_port}.login'
        try:
            node=subprocess.Popen([str(daemon),'--regtest','--keep-fakechain','--fixed-difficulty','1','--offline',
                '--no-igd','--hide-my-port','--no-zmq','--non-interactive','--data-dir',str(root/'native-chain'),
                '--rpc-bind-ip','127.0.0.1','--rpc-bind-port',str(self.daemon_port),
                '--p2p-bind-ip','127.0.0.1','--p2p-bind-port',str(p2p),'--log-file',str(root/'native-node.log')],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
            self.processes.append(node);info=ready(node,self.daemon_port,'get_info')
            assert info['nettype']=='fakechain' and info['offline'] and info['incoming_connections_count']==info['outgoing_connections_count']==0
            assert rpc(self.daemon_port,'get_block',{'height':0})['block_header']['hash']==GENESIS
            (root/'native-wallets').mkdir(mode=0o700)
            wallet=subprocess.Popen([str(wallet_rpc),'--wallet-dir',str(root/'native-wallets'),'--daemon-address',f'127.0.0.1:{self.daemon_port}',
                '--trusted-daemon','--allow-mismatched-daemon-version','--rpc-bind-ip','127.0.0.1','--rpc-bind-port',str(self.wallet_port),
                '--non-interactive','--max-concurrency','2','--log-file',str(root/'native-wallet.log')],cwd=root,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
            self.processes.append(wallet);ready(wallet,self.wallet_port,'get_version',self.login)
            self.observer_login=root/f'monzero-wallet-rpc.{self.observer_port}.login'
            (root/'native-observer').mkdir(mode=0o700)
            observer=subprocess.Popen([str(wallet_rpc),'--wallet-dir',str(root/'native-observer'),'--daemon-address',f'127.0.0.1:{self.daemon_port}',
                '--trusted-daemon','--allow-mismatched-daemon-version','--rpc-bind-ip','127.0.0.1','--rpc-bind-port',str(self.observer_port),
                '--non-interactive','--max-concurrency','2','--log-file',str(root/'native-observer.log')],cwd=root,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
            self.processes.append(observer);ready(observer,self.observer_port,'get_version',self.observer_login)
            addresses={}
            for name in ['mining','payout','refund','source']:
                self.wallet('create_wallet',{'filename':name,'password':'','language':'English'})
                addresses[name]=self.wallet('get_address')['address'];self.wallet('close_wallet')
            self.addresses=addresses;self.wallet('open_wallet',{'filename':'source','password':''})
            rpc(self.daemon_port,'generateblocks',{'wallet_address':addresses['source'],'amount_of_blocks':131})
            self.wallet('refresh',{'start_height':0})
            username,password=self.login.read_text().strip().split(':',1)
            observer_username,observer_password=self.observer_login.read_text().strip().split(':',1)
            self.context=dict(observerPort=self.observer_port,observerUsername=observer_username,observerPassword=observer_password,genesis=GENESIS,daemonPort=self.daemon_port,walletPort=self.wallet_port,username=username,password=password,
                source=addresses['source'],payout=addresses['payout'],refund=addresses['refund'])
        except BaseException:
            self.close();raise
    def wallet(self, method, params=None):return rpc(self.wallet_port,method,params,self.login)
    def confirm(self, txid):
        transport=urllib.request.build_opener(urllib.request.ProxyHandler({}))
        request=urllib.request.Request(f'http://127.0.0.1:{self.daemon_port}/get_transaction_pool',data=b'{}',headers={'Content-Type':'application/json'})
        with transport.open(request,timeout=30) as response:pool=json.load(response)
        assert pool['status']=='OK' 
        assert [v['id_hash'] for v in pool.get('transactions',[])]==[txid]
        rpc(self.daemon_port,'generateblocks',{'wallet_address':self.addresses['mining'],'amount_of_blocks':11})
        self.wallet('refresh',{'start_height':0})
        rpc(self.observer_port,'refresh',{'start_height':0},self.observer_login)
        transfer=self.wallet('get_transfer_by_txid',{'txid':txid})['transfer']
        assert transfer['txid']==txid and transfer['type']=='out' and transfer['amount']==200000000000
        assert 0<transfer['fee']<=10000000000
        return transfer['fee']
    def close(self):
        for process in reversed(self.processes):
            if process.poll() is None:
                process.terminate()
                try:process.wait(timeout=20)
                except subprocess.TimeoutExpired:process.kill();process.wait()
