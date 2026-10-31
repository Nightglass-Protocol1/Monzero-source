# BTC/XMZ on the DEX

Bitcoin trades against XMZ through the same contract, journal and light-wallet
funding paths as LTC/XMZ. Bitcoin and Litecoin share SegWit v0 scripts, CSV
timelocks and BIP143 signing, so only chain identities differ. The engine patch
is `upstream-bitcoin-swap-chain.patch`, the last patch in the chain.

## Roles

- **Bitcoin buyer of XMZ** ("litecoin-funder" in the code): funds the Bitcoin
  lock from a local Bitcoin light wallet. The wallet takes deposits at a normal
  `tb1…`/`bcrt1…` address. It keeps its keys on the device and syncs headers over
  Electrum TLS. If the trade is abandoned, the refund returns to that wallet.
- **XMZ seller** ("native-funder"): locks XMZ and redeems the Bitcoin with a
  Bitcoin Core node (v31 tested). Light-wallet redemption for the seller is not built.

## Deployments

| Stage (`--dex-ltc-network`) | Quote network | Bitcoin chain       | XMZ chain        |
|-----------------------------|---------------|---------------------|------------------|
| `btc-regtest`               | `btc-regtest` | owned regtest       | offline fakechain|
| `btc-testnet4`              | `btc-testnet4`| public testnet4     | XMZ testnet      |
| `btc-signet`                | `btc-signet`  | public signet       | XMZ testnet      |

Mainnet is unrepresentable in the app (`DexDeployment.h`), as it is for Litecoin.
Bitcoin testnet3 is not accepted for swaps. The light wallet uses network name
`testnet4`.

## What changed

- Engine (`swap-core/litecoin_address.rs`): `LtcNetwork` gains `BitcoinMainnet`,
  `BitcoinTestnet4` and `BitcoinRegtest` variants with `bc`/`tb`/`bcrt` prefixes and
  genesis hashes. Quote names are `btc-*`. Supply bounds are per chain.
- Node RPC: Bitcoin Core reports soft forks through `getdeploymentinfo`. CSV and
  SegWit activation is checked there, bound to the same tip.
- Light wallet: testnet4 header rules (BIP94 first-block retarget and the 600 s
  timewarp bound). There is also a pinned testnet4 checkpoint at 155,232, taken from
  our node after all 157,145 real headers validated from genesis. Its work was summed
  independently and matches the node. Signet and mainnet have pinned checkpoints too
  (below).
- App: Bitcoin stages, chain-bound address prefixes, the Bitcoin light wallet as
  the swap funding wallet, risk limits for `btc-*` networks, and trade-flow labels
  on the BTC / XMZ market.
- Driver: `test_litecoin_live_swap.py --chain btc` (regtest uses an owned
  bitcoind; `swap_chain.py` holds the chain profiles). Runner:
  `core/build/run-btc-live-swap.py STAGE MODE LABEL`.

## Evidence (core/build)

- Regtest success: `btc-regtest-success-3-20261020.json`. The light-wallet lock
  was redeemed for exactly 99,990,000 sats. The XMZ payout was 100000000000 atoms,
  with the reserve returned. Cleanup was verified.
- Regtest refund: `btc-regtest-refund-1-20261020.json` (2,520 s). After the 20-block
  timelock, cancel and refund returned 99,980,000 sats to the light wallet; its final
  balance was exactly 104,979,000. The seller's XMZ was refunded (199035520000 + fee
  964480000 = the locked 200000000000). The app binary on disk was rebuilt during this
  run (testnet4 profile change); the running process used the build from `6505c5921`.
- Testnet4 light-wallet smoke (no funds): `btc-testnet4-light-smoke-20261020.json`.
  It synced from the checkpoint to the live tip in 2 s and issued a receive address.
- Engine tests: swap-core 102 passed (1 ignored). For swap-machine, 540 results passed
  and 0 failed (`core/build/btc-engine-libtests-release.log`). The new testnet4 header
  tests are included, plus the full real-chain check (`MONZERO_BTC_TESTNET4_HEADERS`).
  Three BCH tests never finish, e.g. `admitted_bch_trade_verifies_exact_local_wallet_signed_funding`.
  The 15 Oct pre-change binary hangs on the same test, so the hang predates this work.
  The run was stopped.
- App tests: `dex_tests` 82 passed; QML 165 passed.
- Testnet4 refund after an app restart: `btc-testnet4-resume-3-20261023.json` (passed).
  The 200,000-sat lock `48408b74…` was cancelled (`60695b77…`, confirmed 25 Oct after
  testnet4 mined only empty blocks for two days) and refunded (`8974cd5f…`, 180,000
  sats to the light wallet). No XMZ was locked. Three test-tooling fixes came out of it:
  the fixture's command limit (keepalives over a long wait), skipping a recovery step
  already confirmed before a reopen, and tolerating an expired control document on a
  reopened trade (the app only checks it before new trades).

- Signet success: `btc-signet-success-1-20261025.json` (6,285 s). The light-wallet lock
  `ac360c01…` was redeemed for exactly 190,000 sats (`addf488a…`); the XMZ payout `7f9d06ba…`
  was 1 XMZ.
- Signet refund: `btc-signet-refund-1-20261025.json` (15,813 s). Lock `1057616c…`, cancel
  `06ae8b07…`, refund `66fee9dd…` of 180,000 sats to the light wallet; the seller's XMZ refund
  `04c10863…` (199,032,320,000 + fee 967,680,000 atoms). Cleanup verified.
- Signet, two machines: `btc-signet-vm-success-2-20261025.json` (7,270 s). Bob (the BTC
  buyer) ran on the lab VM: his identity, Bitcoin light wallet and XMZ wallets were created
  there, and the host only held his public card. Lock `90b1342f…`, redeem `9055ab91…` for
  exactly 190,000 sats, XMZ payout `b6604f2b…` of 1 XMZ with the 0.9904 XMZ reserve returned.
  The first VM attempt stopped before funding: the remote light-wallet tooling only knew
  Litecoin (fixed in `459b557b1`).
- Signet, two machines with Bob interrupted: `btc-signet-vm-restart-signed-1-20261025.json`
  (4,325 s). Bob's app on the VM was stopped right after signing his BTC lock `d592d52b…`
  (56 minutes left on the quote) and reopened from its retained files; it sent the same
  signed bytes and the trade completed. Redeem `f39353b8…` paid exactly 190,000 sats, XMZ
  payout `762accc8…` 1 XMZ, reserve returned, cleanup verified.
- Signet, two machines with a hard power-off: `btc-signet-vm-powerloss-1-20261025.json`
  (5,938 s). Right after Bob signed his BTC lock `59a65c97…`, the lab VM was powered off
  without shutdown (boot id changed `8e84bc48…` → `6812c955…`) while Alice stayed running.
  After boot, Bob's wallet files and protected trade metadata matched their pre-power-off
  digests, the retained funding bytes were unchanged, the old approval was gone, and the same
  signed lock was sent. Redeem `24986431…` paid exactly 190,000 sats, XMZ payout `a1fe3fd6…`
  1 XMZ with 0.9903744 XMZ reserve returned, cleanup verified.
- Mainnet light-wallet checkpoint (no funds, no trading): all 973,097 Bitcoin mainnet
  headers, exported from a headers-only Bitcoin Core v31.1 node run for this, pass the light
  wallet's rules from genesis (ignored test `mainnet_real_headers_validate_from_genesis`,
  `MONZERO_BTC_MAINNET_HEADERS`). The checkpoint is pinned at block 971,712 (`00000000…e467`);
  its cumulative work, summed from every header, equals the node's.

A first regtest attempt failed safely. A block arrived between funding review and
signing, and the engine refused the stale approval. Nothing was signed or sent.
Regtest now uses a 90 s Bitcoin block clock, a realistic ratio to XMZ's 20 s.

## Public testnet4 run (needs coins)

Send about 0.008 testnet4 BTC (from a testnet4 faucet) to the node's `fixture`
wallet address. That covers a success run and a refund run. Then:

    python3 core/build/run-btc-live-swap.py testnet success btc-testnet4-success-1 \
        --ltc-atoms 200000 --light-funding-margin 120000 --ltc-confirmations 1 --cancel-blocks 36

The runner mines the private XMZ testnet during the run, and Bob funds the lock
from a fresh light wallet. The margin must leave no change or at least 100,000
sats of change after the 1,000-sat lock fee (light coin selection rule); 20,000
stalled the first testnet4 run at light-prepare. Testnet trade folders are kept, since they hold the
recovery keys.

## Public signet run

Testnet4 lets anyone mine a minimum-difficulty block after 20 minutes. Since
23 Oct it has produced only empty blocks of that kind, so the testnet4 refund's
cancel transaction has waited a day in the mempool. Signet has no such
exception: its blocks are signed by the signet operators, arrive about every
10 minutes and include transactions.

- Stage `btc-signet` (app `--dex-ltc-network btc-signet`, quote name `btc-signet`,
  light network `signet`). Addresses are `tb1…`, the same as testnet4; the genesis
  hash keeps the two chains apart in quotes, journals and wallet records.
- Light-wallet header rules: mainnet retargeting under signet's proof-of-work limit
  `0x1e0377ae`, no minimum-difficulty blocks. Headers do not carry the coinbase,
  so the block signature is not checked: signet proof of work is cheap, and the
  light wallet relies on the trusted Electrum view of our own node. Test coins only.
- All 327,823 signet headers from genesis pass these rules (ignored test
  `signet_real_headers_validate_from_genesis`, with `MONZERO_BTC_SIGNET_HEADERS`).
  The light wallet starts at a checkpoint pinned at block 326,592 (taken 2026-10-25);
  its cumulative work was summed from every header and equals the node's.
- Node: user unit `monzero-btc-signet`, datadir `testnet/btcsig`, rpc 48338,
  txindex, wallet `fixture`.

Send about 0.01 signet BTC to the `fixture` wallet, then:

    python3 core/build/run-btc-live-swap.py signet success btc-signet-success-1 \
        --ltc-atoms 200000 --light-funding-margin 120000 --ltc-confirmations 1 --cancel-blocks 36

## Not done

- Public testnet4 success swap: not repeated. The first run locked BTC and stopped on
  the app's command limit (fixed); that trade was then refunded through the app
  (below), and later runs moved to signet.
- Mainnet: app stage, production control key, a mainnet node for the XMZ seller,
  public Electrum endpoint selection, independent review. (The light wallet's mainnet
  checkpoint is pinned; mainnet stays unrepresentable in the app.)
- Light-wallet redemption for the XMZ seller: built 26 Oct; regtest success and refund and a signet success passed with the seller on two Electrum servers. Remaining items in `SELLER_LIGHT_REDEEM.md`.
