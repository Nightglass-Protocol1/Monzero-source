#!/usr/bin/env python3
# Copyright (c) 2026, The Monzero Project

"""Two connected nodes with the asset fork active: an issuance relayed over P2P and mined by
the other node, an issuance and a transfer rolled back by a reorganisation between the nodes,
and wallet restoration afterwards."""

import time

from framework.daemon import Daemon
from framework.wallet import Wallet

SEED = 'velvet lymph giddy number token physics poetry unquoted nibs useful sabotage limits benches lifestyle eden nitrogen anvil fewest avoid batch vials washing fences goat unquoted'


def wait_for(what, check, seconds=60):
    for _ in range(seconds * 2):
        if check():
            return
        time.sleep(0.5)
    assert False, 'timed out waiting for ' + what


def registry(daemon):
    res = daemon.get_assets(count=100)
    assert res.active, res
    return {a.asset_id: a for a in res.get('assets', [])}


class AssetP2PTest:
    def run_test(self):
        d2, d3 = Daemon(idx=2), Daemon(idx=3)
        daemons = (d2, d3)
        wallet = Wallet(idx=4)  # connected to daemon 2

        def same_tip():
            a, b = d2.get_info(), d3.get_info()
            return a.height == b.height and a.top_block_hash == b.top_block_hash

        def connect(on):
            for d in daemons:
                d.out_peers(8 if on else 0)
            if on:
                wait_for('both nodes on one chain', same_tip)

        connect(False)
        for d in daemons:
            d.pop_blocks(d.get_height().height - 1)
            d.flush_txpool()
        try:
            wallet.close_wallet()
        except Exception:
            pass
        wallet.restore_deterministic_wallet(seed=SEED)
        wallet.auto_refresh(enable=False)
        address = wallet.get_address().address

        d2.generateblocks(address, 110)
        connect(True)
        for d in daemons:
            assert d.hard_fork_info(18).enabled
            assert registry(d) == {}
        wallet.refresh()

        # 1. An issuance made on node 2 reaches node 3's pool and node 3 mines it
        print('Issuance relayed over P2P and mined by the other node')
        nft = wallet.create_asset(address, 'nft', 1, 0, '44' * 32, 'ipfs://p2p-nft', '', do_not_relay=False)
        wait_for('the issuance in node 3\'s pool', lambda: nft.tx_hash in d3.get_transaction_pool_hashes().get('tx_hashes', []))
        d3.generateblocks(address, 1)
        wait_for('both nodes on one chain', same_tip)
        for d in daemons:
            assert list(registry(d)) == [nft.asset_id], registry(d)
        wallet.refresh()

        # 2. Node 2 alone confirms a second issuance and a transfer of the NFT; node 3 builds a
        #    longer chain without them. After the nodes meet again node 2 must hold neither.
        print('Issuance and transfer rolled back by a reorganisation')
        connect(False)
        token = wallet.create_asset(address, 'fungible', 1000, 2, '55' * 32, 'ipfs://p2p-token', '', do_not_relay=False)
        d2.generateblocks(address, 1)
        wallet.refresh()
        moved = wallet.transfer_asset(nft.asset_id, address, amount=1, do_not_relay=False)
        d2.generateblocks(address, 1)
        wallet.refresh()
        assert sorted(registry(d2)) == sorted([nft.asset_id, token.asset_id])
        assert list(registry(d3)) == [nft.asset_id]
        spent = [o for o in wallet.get_assets(nft.asset_id, include_spent=True).outputs if o.amount == 1 and o.spent]
        assert len(spent) == 1, spent

        d3.generateblocks(address, 3)
        winning_tip = d3.get_info().top_block_hash
        connect(True)
        assert d2.get_info().top_block_hash == winning_tip
        assert list(registry(d2)) == [nft.asset_id], registry(d2)
        # Both transactions are back in node 2's pool, not lost
        pool = d2.get_transaction_pool_hashes().get('tx_hashes', [])
        assert token.tx_hash in pool and moved.tx_hash in pool, pool

        # The wallet follows the reorganisation: the NFT is unspent again
        wallet.refresh()
        outputs = wallet.get_assets(nft.asset_id).outputs
        assert len(outputs) == 1 and outputs[0].amount == 1, outputs

        # 3. Mined again on the winning chain, both take effect on both nodes
        print('Re-mined on the winning chain')
        wait_for('the returned transactions in node 3\'s pool',
                 lambda: {token.tx_hash, moved.tx_hash} <= set(d3.get_transaction_pool_hashes().get('tx_hashes', [])))
        d3.generateblocks(address, 1)
        wait_for('both nodes on one chain', same_tip)
        for d in daemons:
            assert sorted(registry(d)) == sorted([nft.asset_id, token.asset_id]), registry(d)
            assert d.get_transaction_pool_hashes().get('tx_hashes', []) == []
        assert registry(d2)[token.asset_id].height == registry(d3)[token.asset_id].height

        # 4. A wallet rebuilt from its seed alone sees the same holdings
        print('Wallet restored from seed')
        wallet.close_wallet()
        wallet.restore_deterministic_wallet(seed=SEED)
        wallet.auto_refresh(enable=False)
        wallet.refresh()
        nfts = wallet.get_assets(nft.asset_id).outputs
        assert len(nfts) == 1 and nfts[0].amount == 1, nfts
        assert sum(o.amount for o in wallet.get_assets(token.asset_id).outputs) == 1000


if __name__ == '__main__':
    AssetP2PTest().run_test()
