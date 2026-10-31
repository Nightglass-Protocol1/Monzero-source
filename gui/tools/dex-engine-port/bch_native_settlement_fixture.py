"""Dedicated recovered wallet and independent balances for owned BCH/XMZ tests."""
import json
import subprocess
import urllib.request
from bch_native_funding_fixture_v2 import NativeFundingFixture
from test_wallet_recovery import port, ready, rpc

class NativeSettlementFixture(NativeFundingFixture):
    def __init__(self, root, daemon, wallet_rpc, occupied_port):
        super().__init__(root,daemon,wallet_rpc,occupied_port)
        try:
            self.source_balance=self.wallet('get_balance')['balance']
            self.claim_port=port()
            assert self.claim_port not in {self.daemon_port,self.wallet_port,self.observer_port,occupied_port}
            self.claim_login=root/f'monzero-wallet-rpc.{self.claim_port}.login'
            (root/'native-claim').mkdir(mode=0o700)
            process=subprocess.Popen([str(wallet_rpc),'--wallet-dir',str(root/'native-claim'),
                '--daemon-address',f'127.0.0.1:{self.daemon_port}','--trusted-daemon',
                '--allow-mismatched-daemon-version','--rpc-bind-ip','127.0.0.1',
                '--rpc-bind-port',str(self.claim_port),'--non-interactive','--max-concurrency','2',
                '--log-file',str(root/'native-claim.log')],cwd=root,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
            self.processes.append(process);ready(process,self.claim_port,'get_version',self.claim_login)
            username,password=self.claim_login.read_text().strip().split(':',1)
            self.context.update(claimPort=self.claim_port,claimUsername=username,claimPassword=password)
        except BaseException:
            self.close();raise
    def confirm(self, txid):
        fee=super().confirm(txid)
        assert self.source_balance-self.wallet('get_balance')['balance']==200000000000+fee
        return fee
    def confirm_payout(self, event):
        txid=event['txid'];fee=event['feeAtoms'];reserve=event['reserveReturnAtoms']
        assert event['payoutAtoms']==100000000000 and 0<fee<=10000000000
        assert reserve+fee==100000000000
        transport=urllib.request.build_opener(urllib.request.ProxyHandler({}))
        request=urllib.request.Request(f'http://127.0.0.1:{self.daemon_port}/get_transaction_pool',data=b'{}',headers={'Content-Type':'application/json'})
        with transport.open(request,timeout=30) as response:pool=json.load(response)
        assert pool['status']=='OK' and [v['id_hash'] for v in pool.get('transactions',[])]==[txid]
        rpc(self.daemon_port,'generateblocks',{'wallet_address':self.addresses['mining'],'amount_of_blocks':11})
        rpc(self.claim_port,'refresh',{'start_height':0},self.claim_login)
        outgoing=rpc(self.claim_port,'get_transfer_by_txid',{'txid':txid},self.claim_login)['transfer']
        assert outgoing['type']=='out' and outgoing['fee']==fee and outgoing['amount']==200000000000-fee
        assert rpc(self.claim_port,'get_balance',None,self.claim_login)['balance']==0
        for name,amount in [('payout',100000000000),('refund',reserve)]:
            self.wallet('close_wallet');self.wallet('open_wallet',{'filename':name,'password':''})
            assert self.wallet('get_address')['address']==self.addresses[name]
            self.wallet('refresh',{'start_height':0})
            transfer=self.wallet('get_transfer_by_txid',{'txid':txid})['transfer']
            assert transfer['type']=='in' and transfer['amount']==amount
            balance=self.wallet('get_balance')
            assert balance['balance']==balance['unlocked_balance']==amount
        rpc(self.observer_port,'refresh',{'start_height':0},self.observer_login)
        return dict(txid=txid,payoutAtoms=100000000000,reserveReturnAtoms=reserve,feeAtoms=fee,recoveredWalletBalance=0)
