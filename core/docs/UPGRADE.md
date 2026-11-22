# Monzero upgrade and rollback guide

Back up wallet files, mnemonic seeds and the node data directory before
replacing any binary. Never enter a Monero seed into Monzero or a Monzero seed
into Monero.

## v1.3.8: Monero (XMR) trading

No consensus change. Close the old wallet and node, unpack v1.3.8 and start it: same wallet files,
node data, trading keys and trading wallet. A running trade carries on. To go back, start the v1.3.7
(Windows) or v1.3.6 (Linux) files again; a Monero trade must be finished (or refunded) first, as older
versions do not run the Monero trading wallet.

## v1.3.7: trading starts on Windows (Windows only)

No consensus change; Linux stays at v1.3.6. Close the old Windows wallet, unpack v1.3.7 into a new
folder and start it: same wallet files, node data and trading keys. To go back, start the v1.3.6 files
again (its Exchange could not trade on Windows).

## v1.3.6: SOL/USDT withdrawals fix (Linux)

No consensus change. Close the old wallet and node, unpack v1.3.6 and start it: same wallet files,
node data, trading keys and trading wallet. A running trade carries on; to go back, start the v1.3.5
files again.

## v1.3.5: optional, exchange fixes and new price chart (Linux)

No consensus change. Close the old wallet and node, unpack v1.3.5 and start it: same wallet files,
node data, trading keys and trading wallet. A running trade carries on; to go back, start the v1.3.4
files again.

## v1.3.4: optional, large sends fix (Linux)

No consensus change. Close the old wallet and node, unpack v1.3.4 and start it: same wallet files,
node data, trading keys and trading wallet. A running trade carries on; to go back, start the v1.3.3
files again.

## v1.3.3: optional, USDT trade fee fix (Linux)

No consensus change. Close the old wallet and node, unpack v1.3.3 and start it: same wallet files,
node data, trading keys and trading wallet. A running trade carries on; to go back, start the v1.3.2
files again (do not trade USDT with v1.3.2).

## v1.3.2: optional, USDT markets and order upkeep (Linux)

No consensus change. Close the old wallet and node, unpack v1.3.2 and start it: same wallet files,
node data, trading keys and trading wallet. Finish or refund a running trade first; to go back,
start the v1.3.1 files again (v1.3.1 does not show USDT offers).

## v1.3.1: optional, DEX fixes (Linux)

No consensus change. Close the old wallet and node, unpack v1.3.1 and start it: same wallet files,
node data, trading keys and trading wallet. Finish or refund a running trade first; to go back,
start the v1.3.0 files again.

## v1.3.0: optional, longer offers and your own look (Linux)

No consensus change. Close the old wallet and node, unpack v1.3.0 and start it: same wallet files,
node data, trading keys and trading wallet. Finish or refund a running trade first; to go back,
start the v1.2.0 files again (v1.2.0 does not show offers open longer than a day).

## v1.2.0: optional, adds the exchange page to the Linux wallet

Monzero v1.2.0 changes no consensus rule. Install it on Linux to trade from the wallet's own
Exchange page: close the old wallet and node, unpack v1.2.0 and start it. It uses the same wallet
files, node data, trading keys and trading wallet. Finish or refund a running trade before
switching versions; to go back, start the v1.1.0 files again.

## v1.1.0: optional, adds DEX trading

Monzero v1.1.0 changes no consensus rule. A v1.0.0 node or wallet keeps working and nothing has
to be done before any block height. Install v1.1.0 if you want the wallet to trade on the DEX
page: unpack it, close the v1.0.0 wallet and node, and start the v1.1.0 ones. They use the same
wallet files and the same node data directory. To go back, start the v1.0.0 files again; a trade
that is still running must be finished or refunded in v1.1.0 first.

## v1.0.0: upgrade before block 21,000

Monzero v1.0.0 schedules hard fork 17 at block 21,000, which changes how
difficulty is calculated (see `RELEASE_NOTES.md`). It does not reset the chain:
blocks, coins, wallets, seeds and addresses are unchanged.

| You run | What to do |
| --- | --- |
| A wallet (GUI, CLI or wallet RPC), on any node | Update the wallet to v1.0.0 before block 21,000. A pre16 wallet stops syncing at that block ("Unexpected hard fork version v17"); open the same wallet file in v1.0.0 to continue. No coins are lost. |
| Your own `monzerod` (including solo mining) | Upgrade to v1.0.0 before block 21,000. |
| XMRig on a pool | Nothing; the pool operator upgrades its node. |
| A pool, explorer, exchange or service | Upgrade every node before block 21,000. |

A node that is not upgraded stops following the main chain at block 21,000:
its wallets show balances as of that block and cannot send. Upgrading the node
fixes this; no coins are lost. Keep the node's clock synchronized (NTP): from
block 21,000 a block may be dated at most 360 seconds ahead of a node's clock.

## Supported migration path

v1.0.0 upgrades in place from any Monzero Genesis prerelease from pre10 on (the
same network identifiers and genesis block). It does not support migrating
Monero mainnet data, wallets or keys. The asset hard fork (now version 18) is
not scheduled on any public network; `--regtest-asset-hard-fork` is only for
disposable local testing.

1. Stop the GUI, wallet RPC, wallet CLI, miners and daemon cleanly.
2. Copy wallet files and the `.monzero` data directory to offline backup
   storage. Keep the previous verified binaries and their checksums.
3. Verify the release signature, the archive SHA-256 and its internal
   `SHA256SUMS` before use.
4. Extract into a new directory rather than overwriting the previous release.
5. Start `monzerod` and confirm it reports version `1.0.0`, the expected
   network, height and peers. `hard_fork_info` lists version 17 at block 21,000.
6. Open the wallet, let it synchronize, and confirm the primary address and
   balances before sending funds.

No database migration is intentionally introduced. An existing database may
still be upgraded by inherited database code and must therefore be treated as
non-downgradable without restoring its backup.

## Rollback

Rolling back to a prerelease is only possible before block 21,000; after it, a
prerelease node cannot follow the main chain. Stop all Monzero processes,
then restore both the previous binaries and the matching pre-upgrade data
directory backup. Do not run an older binary against a database that a newer
binary has opened.

Report suspected security issues privately to `security@monzero.org`. Do not
disclose wallet seeds, private keys or sensitive logs in public reports.
