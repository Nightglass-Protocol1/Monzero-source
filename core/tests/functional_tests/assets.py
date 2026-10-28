#!/usr/bin/env python3
# Copyright (c) 2026, The Monzero Project

"""Activated-regtest asset issuance, multi-input transfer, burn, restore, and reorg."""

from framework.daemon import Daemon
from framework.wallet import Wallet

SEEDS = [
    'velvet lymph giddy number token physics poetry unquoted nibs useful sabotage limits benches lifestyle eden nitrogen anvil fewest avoid batch vials washing fences goat unquoted',
    'peeled mixture ionic radar utopia puddle buying illness nuns gadget river spout cavernous bounced paradise drunk looking cottage jump tequila melting went winter adjust spout',
]


class AssetTest:
    def run_test(self):
        daemon = Daemon()
        height = daemon.get_height().height
        daemon.pop_blocks(height - 1)
        daemon.flush_txpool()

        wallets = [Wallet(idx=0), Wallet(idx=1)]
        for index, wallet in enumerate(wallets):
            try:
                wallet.close_wallet()
            except Exception:
                pass
            wallet.restore_deterministic_wallet(seed=SEEDS[index])
            wallet.auto_refresh(enable=False)

        issuer_address = wallets[0].get_address().address
        recipient_address = wallets[1].get_address().address
        daemon.generateblocks(issuer_address, 100)
        for wallet in wallets:
            wallet.refresh()

        # Asset transitions ride in ordinary native transactions, so the
        # recipient needs a small mature XMZ output to pay for its later burn.
        funding = wallets[0].transfer([
            {'address': recipient_address, 'amount': 1_000_000_000_000}
        ])
        assert len(funding.tx_hash) == 64, funding
        daemon.generateblocks(issuer_address, 10)
        for wallet in wallets:
            wallet.refresh()

        fork = daemon.hard_fork_info(17)
        assert fork.enabled, fork

        issued = wallets[0].create_asset(
            issuer_address, 'nft', 1, 0, '22' * 32,
            'ipfs://activated-regtest-nft', '', do_not_relay=False)
        assert len(issued.asset_id) == 64, issued
        asset_id = issued.asset_id
        daemon.generateblocks(issuer_address, 1)
        for wallet in wallets:
            wallet.refresh()

        holdings = wallets[0].get_assets(asset_id)
        assert len(holdings.outputs) == 1, holdings
        assert holdings.outputs[0].amount == 1, holdings
        assert not holdings.outputs[0].spent, holdings

        transfer = wallets[0].transfer_asset(
            asset_id, recipient_address, amount=1, do_not_relay=False)
        assert len(transfer.tx_hash) == 64, transfer
        daemon.generateblocks(issuer_address, 1)
        for wallet in wallets:
            wallet.refresh()

        issuer_outputs = wallets[0].get_assets(asset_id, include_spent=True).outputs
        recipient_outputs = wallets[1].get_assets(asset_id).outputs
        assert len(issuer_outputs) == 1 and issuer_outputs[0].spent, issuer_outputs
        assert len(recipient_outputs) == 1 and recipient_outputs[0].amount == 1, recipient_outputs

        burn = wallets[1].transfer_asset(asset_id, amount=0, burn_amount=1,
                                         do_not_relay=False)
        assert len(burn.tx_hash) == 64, burn
        daemon.generateblocks(issuer_address, 1)
        for wallet in wallets:
            wallet.refresh()
        assert wallets[1].get_assets(asset_id).get('outputs', []) == []
        burned = wallets[1].get_assets(asset_id, include_spent=True).outputs
        assert len(burned) == 1 and burned[0].spent, burned

        # Reconstruct the recipient wallet from its seed and prove chain-only
        # restoration reproduces the spent state.
        wallets[1].close_wallet()
        wallets[1].restore_deterministic_wallet(seed=SEEDS[1])
        wallets[1].auto_refresh(enable=False)
        wallets[1].refresh()
        restored = wallets[1].get_assets(asset_id, include_spent=True).outputs
        assert len(restored) == 1 and restored[0].spent, restored

        # Detach the burn block: the restored wallet must roll the NFT back to
        # unspent rather than retaining a phantom burn.
        daemon.pop_blocks(1)
        daemon.flush_txpool()
        # Replace the detached height so the wallet observes a competing tip;
        # a merely shorter daemon chain is not itself a synchronizable block.
        daemon.generateblocks(issuer_address, 1)
        wallets[1].refresh()
        rolled_back = wallets[1].get_assets(asset_id).outputs
        assert len(rolled_back) == 1 and rolled_back[0].amount == 1, rolled_back

        # Split a fungible asset into two one-unit outputs owned by one wallet,
        # then burn both together. No single output can satisfy the burn, so
        # this transaction exercises wallet selection and two ownership proofs.
        wallets[0].refresh()
        fungible = wallets[0].create_asset(
            issuer_address, 'fungible', 2, 0, '33' * 32,
            'ipfs://activated-regtest-multi-input', '', do_not_relay=False)
        assert len(fungible.asset_id) == 64, fungible
        multi_asset_id = fungible.asset_id
        daemon.generateblocks(issuer_address, 1)
        wallets[0].refresh()

        split = wallets[0].transfer_asset(
            multi_asset_id, issuer_address, amount=1, do_not_relay=False)
        assert len(split.tx_hash) == 64, split
        daemon.generateblocks(issuer_address, 1)
        wallets[0].refresh()
        split_outputs = wallets[0].get_assets(multi_asset_id).outputs
        assert len(split_outputs) == 2, split_outputs
        assert sorted(output.amount for output in split_outputs) == [1, 1], split_outputs

        multi_burn = wallets[0].transfer_asset(
            multi_asset_id, amount=0, burn_amount=2, do_not_relay=False)
        assert len(multi_burn.tx_hash) == 64, multi_burn
        daemon.generateblocks(issuer_address, 1)
        wallets[0].refresh()
        assert wallets[0].get_assets(multi_asset_id).get('outputs', []) == []
        spent_multi = wallets[0].get_assets(
            multi_asset_id, include_spent=True).outputs
        selected_multi = [output for output in spent_multi if output.amount == 1]
        assert len(selected_multi) == 2 and all(
            output.spent for output in selected_multi), spent_multi
        assert selected_multi[0].spent_height == selected_multi[1].spent_height, spent_multi


if __name__ == '__main__':
    AssetTest().run_test()
