# Monzero v1.1.0 release notes

Release label: v1.1.0

Monzero v1.1.0 is the first release whose wallet can trade on the Monzero DEX. It changes no
consensus rule: the chain, blocks, coins, wallets, seeds and addresses are the same as in v1.0.0,
and **nobody has to upgrade to stay on the network**. Upgrade if you want to trade.

## What is new

- **Trading from the DEX page.** With the wallet open on your computer, the page at
  https://monzero.org/dex/ can post and take offers for XMZ against **ETH, BNB and SOL**. The page
  is only the screen: MetaMask (ETH, BNB) or Phantom (SOL) pays the other coin in, and the wallet
  holds the XMZ, runs the atomic swap and asks you to approve each step.
- **Prompts, not a trading page.** The wallet shows a prompt when the page asks to connect (compare
  the code), when it needs your trading password, when a trade needs your approval, and when XMZ
  is to move into or out of the trading wallet.
- **Trading keys and a trading wallet.** The first connection asks for a new trading password. It
  protects the trading keys, which are separate from your main wallet's keys. XMZ you want to
  trade is moved to the wallet's own trading wallet with "Add XMZ" and back with "Move back";
  your main wallet's keys are never used by the page.
- **Limits while the DEX is new.** The wallet starts a new trade only inside the limits of a
  control document signed by the project (per trade and per day, per network), fetched from
  monzero.org and checked against a key compiled into the wallet. Without a valid document no new
  trade starts. A trade that has started always finishes or refunds, whatever the document says.
- **Offers through open relays.** Signed offers are published to relays anyone can run
  (`relay1.monzero.org`, `relay2.monzero.org`). A relay cannot forge or alter an offer.

Not in this release: Bitcoin and Litecoin trading (being finished), trading from the Android
wallet, and an in-wallet order book.

## Before you trade

- The swap programs and contracts are open source and were tested with real funds on each open
  pair, but they have **not been independently audited**. Trade amounts you can afford to lose.
- Keep the wallet open until a trade is finished. If it closes, open it again: the trade resumes,
  or refunds after its time limit. See https://monzero.org/dex/recovery/.
- Do not leave coins in the trading accounts. The trading keys are stored on this computer,
  protected by your trading password, and this release does not show recovery words for them.
  After a trade, send the other coin back to MetaMask or Phantom and move XMZ back to your wallet.

## Packaged platforms

| Package | Intended compatibility scope |
| --- | --- |
| Linux x86-64 wallet (GUI), node and tools | 64-bit PC, glibc 2.31 or newer (Ubuntu 20.04, Debian 11 and later) |
| Complete source | Exact repository commit and recursive pinned submodules |
| DEX helper source | The adapted swap engine the bundled helper programs are built from (GPL-3.0) |

The Linux package contains the graphical wallet, `monzerod`, the command-line wallet, wallet RPC
and, in `monzero-dex/` and `monzero-light/`, three helper programs the wallet runs for swaps. The
wallet runs a helper only if its SHA-256 matches the value compiled into the wallet. Other
platforms keep their v1.0.0 packages, which cannot trade.

## Upgrading

Replace the v1.0.0 files with the v1.1.0 ones, or unpack v1.1.0 next to them; both use the same
wallet files and node data. See `UPGRADE.md`.

---

# Monzero v1.0.0 release notes

Monzero v1.0.0 is the first stable release. It continues the same chain as the
Genesis pre10–pre16 prereleases: no reset, no new genesis block, and every
existing block, coin and wallet stays valid. It schedules one consensus
change, a hard fork to a faster difficulty algorithm at block **21,000**.

**Every node operator must upgrade before block 21,000.** This includes solo
miners, pool operators, exchanges, explorers, and anyone running their own
`monzerod`. A node still running pre16 or older stops following the main chain
at that block.

**Every wallet must also be updated to v1.0.0 before block 21,000** (GUI, CLI
and wallet RPC), including wallets that use a public node. A pre16 wallet
checks each block's version against its own fork table and stops syncing at
the first version-17 block with "Unexpected hard fork version v17". No coins
are at risk: open the same wallet file, or restore the same seed, in v1.0.0
and it syncs normally. Miners on a pool need to do nothing; their payout
address and balance do not change.

## Packaged platforms

| Package | Intended compatibility scope |
| --- | --- |
| Linux x86-64 CLI | glibc 2.35 or newer (verified on Ubuntu 24.04) |
| Windows x64 CLI | 64-bit Windows command-line environment |
| Windows x64 GUI | 64-bit Windows graphical environment |
| Complete source | Exact repository commit and recursive pinned submodules |

The binary archives contain `monzerod`, the command-line wallet and wallet RPC.
The GUI is distributed separately with matching core tools. macOS, Linux ARM,
Windows ARM, mobile platforms and 32-bit systems are not covered by this release.

## Consensus change: LWMA difficulty from block 21,000 (hard fork 17)

Until now Monzero used Monero's original difficulty rule, which averages 720
blocks and ignores the newest 15. It suits a network with large, steady hash
power, but it follows a big change slowly: about 670 blocks after the hash
rate rises or falls a hundredfold. On Monzero this meant blocks every ~57
seconds for a day and a half after the pool opened (issuing coins twice as
fast as planned), and gaps of up to ten hours when large miners left.

From block 21,000 the next block's difficulty is LWMA-1 (zawy12), a linearly
weighted average of the newest 60 solve times, as used by many CryptoNote coins:

- Difficulty settles within about 106 blocks after a hundredfold rise in hash
  rate and 145 blocks after a hundredfold fall, instead of about 670.
- A solve time out of order counts as one second, and any single solve time
  counts as at most six target block times, so bad timestamps cannot swing
  difficulty far.
- From the same block, a block's timestamp may be at most 360 seconds ahead of
  a node's clock (previously two hours), so a block cannot claim a far-future
  time to lower difficulty. **Keep node clocks synchronized (NTP).**

Unchanged: the 120-second target, RandomX proof of work, the emission curve,
the 3.26 XMZ tail emission, address formats, ports and network identity.

The Monzero Assets research feature moves from hard fork 17 to 18 and remains
unscheduled on every public network.

## Other changes since Genesis pre16

- The core reports version `1.0.0` (previously Monero's base version
  `0.18.5.1`), and packages are named `monzero-v1.0.0-…`.
- All changes listed for Genesis pre16 are included, including the wallet fix
  for unmixable early outputs.

## Verification performed

- All 23 `ctest` suites pass on the Linux release workstation, run serially on
  17 October 2026, including `core_tests` (full consensus replay, 2,141
  seconds), `functional_tests_rpc` (18 of 18), `functional_tests_assets`,
  `check_missing_rpc_methods`, `unit_tests`, and the RandomX, hash-family,
  difficulty and block-weight suites.
- `unit_tests` runs 1,324 tests: 1,322 pass and 2 hardware-dependent `is_hdd`
  tests are skipped. Tests that need local ports or the internet were run
  where those were available (an isolated network namespace for port-bound
  tests, the normal network for DNS tests); the `mining` functional test used
  its fixed timeouts (`MINING_NO_MEASUREMENT`) because the workstation was
  also mining.
- New unit tests cover the LWMA rule: steady difficulty at a constant hash
  rate, settling after hundredfold rises and falls (and the old rule's slower
  response), out-of-order and far-future timestamps, empty windows, the
  version-17 switch, the 360-second limit and the protected fork schedule.
- On Monzero's private test network, two upgraded nodes mined through the fork
  at block 1,345. Every block from the fork on had version 17 and exactly the
  difficulty an independent LWMA calculation gives. A third node left on pre16
  stopped at block 1,345 and did not follow the upgraded chain, as an
  un-upgraded node will on mainnet.
- Release testing found and fixed one v1.0.0 issue: the daemon's version-string
  check required four-part versions and rejected `1.0.0`.

These are source and test-network results. They are not an independent
security review; see the limitations below.

## Known limitations and safety notices

- Consensus, cryptography and the wider implementation have not been
  independently audited. Security review continues with public use; report
  issues privately to `security@monzero.org`.
- **A prerelease wallet that had selected the atomic-XMZ display unit cannot
  be opened**, because that setting was also used by Monero wallets and does
  not prove the file was created by Monzero. Restore it from its recovery seed.
- The wallet file check cannot detect a Monero seed or private keys typed into
  Monzero's restore options. Never reuse them.
- The public network still has limited independent infrastructure and hash
  power.
- Windows SmartScreen may warn until the executables are code-signed.
- Never reuse a Monero or other CryptoNote-derived seed, keys, wallet file or
  data directory with Monzero.

## Upgrade and rollback

Read `UPGRADE.md` before replacing binaries, and upgrade before block 21,000.

## Verification and reporting

Verify the release signature and compare downloads with the SHA-256 values in
the signed release metadata and the adjacent `.sha256` files. Review
`BUILD-MANIFEST.txt` or `SOURCE-MANIFEST.txt` inside the archive before use.
Report security issues privately to `security@monzero.org`; never include
wallet seeds or private keys.
