# Monzero upgrade and rollback guide

Back up wallet files, mnemonic seeds and the node data directory before
replacing any binary. Never enter a Monero seed into Monzero or a Monzero seed
into Monero.

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
