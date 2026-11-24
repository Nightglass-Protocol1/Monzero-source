# Monzero v1.3.9 release notes

Release label: v1.3.9

Monzero v1.3.9 fixes the Exchange's Funds tab and brings the full wallet with its exchange to 64-bit
ARM (Raspberry Pi). It changes no consensus rule: the chain, blocks, coins, wallets, seeds and
addresses are the same as in v1.0.0 to v1.3.8, and nobody has to upgrade to stay on the network.

## What is fixed

- **Funds tab on XMZ pairs**: with an XMZ pair selected (XMZ/SOL, XMZ/BNB, …) the Monero trading wallet
  took the place beside the XMZ wallet and pushed the pair's own trade account (SOL, BNB, …) onto a
  second row, out of view. Funds now shows the wallet the pair trades (XMZ on XMZ pairs, XMR on Monero
  pairs, both on XMR/XMZ) beside the pair's trade account.

## What is new

- **64-bit ARM wallet** (`linux-aarch64-gui`): the graphical wallet with the Exchange (XMZ and XMR
  markets) for Raspberry Pi OS trixie / Debian 13, with Monero's official monero-wallet-rpc for 64-bit
  ARM. It uses the system's Qt 5.15 and Boost 1.83; the Monzero Box for Raspberry Pi installs them.
- **Monzero Box for Raspberry Pi** runs the wallet with its exchange on the Pi's own screen or in your
  browser, as the virtual machine and Docker boxes do.

Everything else is as in v1.3.8, below.

## Upgrading

Close the old wallet, unpack v1.3.9 into a new folder and start it; it uses the same wallet files,
node data, trading keys and Monero trading wallet. A Monzero Box updates itself within a day.

---

# Monzero v1.3.8 release notes

Release label: v1.3.8

Monzero v1.3.8 adds **Monero (XMR) trading** to the wallet's Exchange. It changes no consensus rule:
the chain, blocks, coins, wallets, seeds and addresses are the same as in v1.0.0 to v1.3.7, and nobody
has to upgrade to stay on the network.

## What is new

- **XMR/BNB, XMR/USDT (BNB Chain), XMR/SOL and XMR/USDT (Solana)**: buy or sell Monero for BNB, SOL or USDT
  (BEP-20 or SPL) with an atomic
  swap, from the same Exchange page as the XMZ markets. Both were tested with real coins on the main
  networks before this release, in both directions for USDT and SOL (22 and 23 November 2026: BNB for XMR,
  XMR for USDT, USDT for XMR, SOL for XMR, XMR for SOL, and USDT on Solana for XMR).
- **A Monero wallet for trading**, run by the app with Monero's own official `monero-wallet-rpc`
  (v0.18.5.4, unchanged, in the package). It syncs from the project's Monero node; every XMR lock must
  also be seen by at least two independent public Monero nodes before the other side pays.
- **XMR/XMZ** shows as one tile: it is two swaps (XMZ for BNB or USDT, then that for XMR), with a stop
  between them.
- XMR against ETH, and the direct coin-for-coin pairs (ETH/BNB, ETH/SOL, BNB/SOL and their USDT
  pairs), appear as "coming soon" until their own real-funds tests are done.
- Relays: an offer in USDT is no longer measured against the cap meant for the chain's own coin.
- Relays: an offer that pays in USDT is backed by the maker's fee coin, as direct offers already were
  (the relay compared the maker's BNB with the USDT amount and refused every USDT offer that buys).
- A trade reads the Monero wallet's height only once the wallet has synced: a wallet made seconds
  before a trade said height 0 and the trade's Monero wallet set out to scan the whole chain.

## Before you trade

- The swap programs and contracts are open source and were tested with real funds on each open
  pair, but they have **not been independently audited**. Trade amounts you can afford to lose.
- Keep the wallet open until a trade is finished. If it closes, open it again: the trade resumes,
  or refunds after its time limit. See https://monzero.org/dex/recovery/.
- A Monero trade takes longer than an XMZ one: the XMR lock needs 10 Monero blocks (about 20 minutes)
  and the witnesses' confirmation. The real-funds trades took 37 to 52 minutes.

## Packaged platforms

| Package | Intended compatibility scope |
| --- | --- |
| Linux x86-64 wallet (GUI), node and tools | 64-bit PC, glibc 2.35 or newer (Ubuntu 22.04, Debian 12 and later), X11 or XWayland |
| Windows x64 wallet (GUI, no mining), node and tools (unsigned beta) | 64-bit Windows 10 or 11 |

Both packages contain Monero's `monero-wallet-rpc` v0.18.5.4 exactly as Monero publishes it (checked
against Monero's signed hash list when packaged).

## Upgrading

Close the old wallet, unpack v1.3.8 into a new folder and start it; it uses the same wallet files,
node data and trading keys. The Monero trading wallet is made on first start.

---

# Monzero v1.3.7 release notes (Windows)

Release label: v1.3.7 (Windows wallet, unsigned beta)

Monzero v1.3.7 is a fix for the **Windows** wallet's Exchange. It changes no consensus rule: the chain,
blocks, coins, wallets, seeds and addresses are the same as in v1.0.0 to v1.3.6, and nobody has to
upgrade to stay on the network. Linux stays at v1.3.6: nothing changed for Linux.

## What is fixed

- **Trading starts on Windows.** In the Windows wallets v1.3.4 to v1.3.6 the Exchange never got past
  "Trading is still starting" (and the web page with Phantom or MetaMask said "The trading wallet is
  still starting"): a few file-safety checks for the trading wallet had a Linux version only, so on
  Windows they always failed and the trading wallet never started. They now have Windows versions with
  the same protections (no shortcuts or links in place of the app's own files, no file changed while it
  is read).
- **All five markets on Windows**, as on Linux: ETH, BNB, SOL, USDT on Solana and USDT on BNB Chain.
  The two USDT markets appear once trading has started.
- Everything from v1.3.6 is included: SOL and USDT withdrawals from the trade account, and Withdraw
  shows what happened.

## Before you trade

- The swap programs and contracts are open source and were tested with real funds on each open
  pair, but they have **not been independently audited**. Trade amounts you can afford to lose.
- Keep the wallet open until a trade is finished. If it closes, open it again: the trade resumes,
  or refunds after its time limit. See https://monzero.org/dex/recovery/.
- The Windows wallet is an unsigned beta: Windows SmartScreen may warn when you start it.

## Packaged platforms

| Package | Intended compatibility scope |
| --- | --- |
| Windows x64 wallet (GUI, no mining), node and tools | 64-bit Windows 10 or 11 |

The DEX helper `monzero-dex-evm-helper.exe` is the v1.3.6 one (unchanged).

## Upgrading

Close the old wallet, unpack v1.3.7 into a new folder and start it; it uses the same wallet files,
node data and trading keys. If an earlier version left a half-made trading wallet folder, v1.3.7
finishes setting it up.

---

# Monzero v1.3.6 release notes

Release label: v1.3.6

Monzero v1.3.6 is an urgent wallet fix for the exchange. It changes no consensus rule: the chain, blocks,
coins, wallets, seeds and addresses are the same as in v1.0.0 to v1.3.5, and **nobody has to upgrade to
stay on the network** — but anyone who trades SOL or USDT on Solana should.

## What is fixed

- **Withdrawing SOL or USDT from the trade account works again.** On 19 November 2026 the public Solana
  node the wallet uses began refusing one kind of request (token balances looked up "by index") unless
  the caller pays for a key. A SOL withdrawal also sends any USDT in the same account, so it asked for
  that balance first and failed, and the Exchange page's USDT balance stopped loading. The wallet now
  reads token balances straight from the token account, which every node answers. Trades were not
  affected.
- **Withdraw tells you what happened.** The result now appears under the Withdraw box: sending, sent
  (with the transaction id), or the reason it was not sent. Before, a refused withdrawal showed nothing.

## Before you trade

- The swap programs and contracts are open source and were tested with real funds on each open
  pair, but they have **not been independently audited**. Trade amounts you can afford to lose.
- Keep the wallet open until a trade is finished. If it closes, open it again: the trade resumes,
  or refunds after its time limit. See https://monzero.org/dex/recovery/.

## Packaged platforms

| Package | Intended compatibility scope |
| --- | --- |
| Linux x86-64 wallet (GUI), node and tools | 64-bit PC, glibc 2.35 or newer (Ubuntu 22.04, Debian 12 and later), X11 or XWayland |
| Complete source | Exact repository commit and recursive pinned submodules |

The DEX helper programs are the v1.1.0 ones except `monzero-dex-evm-helper`, rebuilt from
`gui/dex-contracts/engine` in the complete source.

## Upgrading

Close the old wallet, unpack v1.3.6 and start it; it uses the same wallet files, node data, trading
keys and trading wallet. A trade running in v1.3.5 carries on in v1.3.6.

---

# Monzero v1.3.5 release notes

Release label: v1.3.5

Monzero v1.3.5 is a wallet release for the exchange. It changes no consensus rule: the chain, blocks,
coins, wallets, seeds and addresses are the same as in v1.0.0 to v1.3.4, and **nobody has to upgrade
to stay on the network**.

## What is fixed

- **An order you took that the other trader withdrew no longer hangs around.** If the maker withdraws
  or refuses an offer after you took it, the wallet now shows the take as refused and stops it for
  good, instead of waiting until the offer expires. A take that is still waiting for the maker can be
  stopped with **Stop waiting** (Exchange page, and the DEX web page through the wallet). Nothing is
  ever locked before you approve a trade, so stopping a take never touches your coins.

## What is new

- **A clearer price chart on the Exchange page.** Each completed swap is a dot sized by the XMZ traded
  (candles once there is enough trading), in US dollars across every pair by default or in the pair's
  own coin, on a time axis to scale. NoirTrade's XMZ/USDT market is shown as an amber line for
  comparison, the best offers as dashed lines, and a tiny swap no longer squashes the price axis.
- The Assets page says plainly that native assets are not coming to the Monzero network and points to
  Monzero Studio, which mints on Solana.

## Before you trade

- The swap programs and contracts are open source and were tested with real funds on each open
  pair, but they have **not been independently audited**. Trade amounts you can afford to lose.
- Keep the wallet open until a trade is finished. If it closes, open it again: the trade resumes,
  or refunds after its time limit. See https://monzero.org/dex/recovery/.

## Packaged platforms

| Package | Intended compatibility scope |
| --- | --- |
| Linux x86-64 wallet (GUI), node and tools | 64-bit PC, glibc 2.35 or newer (Ubuntu 22.04, Debian 12 and later), X11 or XWayland |
| Complete source | Exact repository commit and recursive pinned submodules |

The DEX helper programs are the v1.1.0 ones except `monzero-dex-evm-helper`, rebuilt from
`gui/dex-contracts/engine` in the complete source.

## Upgrading

Close the old wallet, unpack v1.3.5 and start it; it uses the same wallet files, node data, trading
keys and trading wallet. A trade running in v1.3.4 carries on in v1.3.5.

---

# Monzero v1.3.4 release notes

Release label: v1.3.4

Monzero v1.3.4 is a Linux wallet fix release. It changes no consensus rule: the chain, blocks, coins,
wallets, seeds and addresses are the same as in v1.0.0 to v1.3.3, and **nobody has to upgrade to stay
on the network**.

## What is fixed

- **"Transaction sanity check failed" when sending a large amount or "Send all".** A wallet holding
  many small amounts (pool or mining payouts) could not send them in one go: a transaction spending
  more than about 130 of them was refused before it was sent. Each spent amount hides among outputs
  picked from the chain's recent ones, and on today's chain (a few outputs a block) the picks of
  different amounts repeated too often for the wallet's own privacy check. The wallet now keeps
  every amount's picks apart from the others in the same transaction, so these sends go out (split
  into several transactions when there are very many amounts). Nothing changes for the network:
  nodes accept these transactions as they are.

## Before you trade

- The swap programs and contracts are open source and were tested with real funds on each open
  pair, but they have **not been independently audited**. Trade amounts you can afford to lose.
- Keep the wallet open until a trade is finished. If it closes, open it again: the trade resumes,
  or refunds after its time limit. See https://monzero.org/dex/recovery/.

## Packaged platforms

| Package | Intended compatibility scope |
| --- | --- |
| Linux x86-64 wallet (GUI), node and tools | 64-bit PC, glibc 2.35 or newer (Ubuntu 22.04, Debian 12 and later), X11 or XWayland |
| Complete source | Exact repository commit and recursive pinned submodules |

The DEX helper programs are the v1.1.0 ones except `monzero-dex-evm-helper`, rebuilt from
`gui/dex-contracts/engine` in the complete source (unchanged since v1.3.3).

## Upgrading

Close the old wallet, unpack v1.3.4 and start it; it uses the same wallet files, node data, trading
keys and trading wallet. A trade running in v1.3.3 carries on in v1.3.4.

---

# Monzero v1.3.3 release notes

Release label: v1.3.3

Monzero v1.3.3 is a Linux wallet fix release for the DEX. It changes no consensus rule: the chain,
blocks, coins, wallets, seeds and addresses are the same as in v1.0.0 to v1.3.2, and **nobody has to
upgrade to stay on the network**. Update before trading on the XMZ/USDT markets.

## What is fixed

- **USDT trades and network fees.** v1.3.2 counted on the project's relays to pay the network fees
  of every USDT trade. The relays do not pay them yet, so a USDT trade in v1.3.2 would have stopped
  at the lock. v1.3.3 asks the relays at each step and uses only one that offers to pay now; until
  then a USDT trader keeps a little SOL (about 0.015) or BNB in the trade account for fees, as on
  the other pairs, and the buyer's fee advance covers the seller's claim. When the relays start
  paying fees, v1.3.3 uses them without an update (then the fee is a little USDT instead).
- A relay that does not answer no longer slows a trade step: the relays are asked together, for a
  few seconds at most.

The XMZ/USDT markets were not open while v1.3.2 was current, so no trade was affected.

## Before you trade

- The swap programs and contracts are open source and were tested with real funds on each open
  pair, but they have **not been independently audited**. Trade amounts you can afford to lose.
- Trades stay inside the limits of the project's signed control documents (about $25 per trade and
  $250 a day per pair while the DEX opens; 25 USDT and 250 USDT on the USDT markets).
- Keep the wallet open until a trade is finished. If it closes, open it again: the trade resumes,
  or refunds after its time limit. See https://monzero.org/dex/recovery/.

## Packaged platforms

| Package | Intended compatibility scope |
| --- | --- |
| Linux x86-64 wallet (GUI), node and tools | 64-bit PC, glibc 2.35 or newer (Ubuntu 22.04, Debian 12 and later), X11 or XWayland |
| Complete source | Exact repository commit and recursive pinned submodules |

The DEX helper programs are the v1.1.0 ones except `monzero-dex-evm-helper`, rebuilt from
`gui/dex-contracts/engine` in the complete source.

## Upgrading

Close the old wallet, unpack v1.3.3 and start it; it uses the same wallet files, node data, trading
keys and trading wallet. A trade running in v1.3.2 carries on in v1.3.3.

---

# Monzero v1.3.2 release notes

Release label: v1.3.2

Monzero v1.3.2 is a Linux wallet update for the DEX. It changes no consensus rule: the chain,
blocks, coins, wallets, seeds and addresses are the same as in v1.0.0 to v1.3.1, and **nobody has to
upgrade to stay on the network**.

## What is new

- **XMZ/USDT.** Two new markets on the Exchange page: **USDT (SOL)**, Tether on Solana (SPL), and
  **USDT (BNB)**, Tether on BNB Smart Chain (BEP-20). Pay in USDT, or sell XMZ for USDT, with the
  same atomic swaps as the other pairs. **Network fees are paid in USDT**: the project's relays pay
  the Solana or BNB Smart Chain fee and take a small USDT fee instead (on Solana about 0.5 USDT to
  buy, most of it coming back as SOL after the trade, and about 0.2 USDT from what you receive when
  you sell; on BNB Smart Chain about 0.05 to 0.2 USDT when you sell). On Solana neither side needs
  SOL; on BNB Smart Chain a buyer still needs a little BNB. If no relay answers, the wallet asks for
  a little SOL or BNB instead. There is no ERC-20 USDT on Ethereum (its fees are too high). Each USDT
  market opens when its signed limits document is published.
- **Keep open.** An order can renew itself: shortly before it expires the wallet posts it again at
  the same price for the same time, while the wallet runs. Switch it on in the order form, or with
  "Keep open" on an open order.
- **Orders you can no longer fill are withdrawn.** If the coins behind an open order are spent or
  moved, the wallet takes the order off the board and refuses anyone who tries to take it, instead
  of letting a buyer lock coins for nothing.
- **Fixed:** orders open longer than a day (a week, a month, custom) could not be posted from the
  Exchange page ("The offer could not be posted now"). They can now, up to 31 days.

## Before you trade

- The swap programs and contracts are open source and were tested with real funds on each open
  pair, but they have **not been independently audited**. Trade amounts you can afford to lose.
- Trades stay inside the limits of the project's signed control documents (about $25 per trade and
  $250 a day per pair while the DEX opens; 25 USDT and 250 USDT on the USDT markets).
- Keep the wallet open until a trade is finished. If it closes, open it again: the trade resumes,
  or refunds after its time limit. See https://monzero.org/dex/recovery/.

## Packaged platforms

| Package | Intended compatibility scope |
| --- | --- |
| Linux x86-64 wallet (GUI), node and tools | 64-bit PC, glibc 2.35 or newer (Ubuntu 22.04, Debian 12 and later), X11 or XWayland |
| Complete source | Exact repository commit and recursive pinned submodules |

The DEX helper programs are the v1.1.0 ones except `monzero-dex-evm-helper`, rebuilt from
`gui/dex-contracts/engine` in the complete source.

## Upgrading

Close the old wallet, unpack v1.3.2 and start it; it uses the same wallet files, node data, trading
keys and trading wallet. Finish or refund a running trade first.

---

# Monzero v1.3.1 release notes

Release label: v1.3.1

Monzero v1.3.1 is a Linux wallet fix release for the DEX. It changes no consensus rule: the chain,
blocks, coins, wallets, seeds and addresses are the same as in v1.0.0 to v1.3.0, and **nobody has to
upgrade to stay on the network**.

## What is new

- **Sell XMZ for SOL or BNB without holding any first.** The buyer's wallet now pays the small
  network fee you need to collect your coins (about 0.0015 SOL; a fraction of a cent on BNB Smart
  Chain), and sends it only after your XMZ is locked. If the buyer runs an older wallet that does
  not send it, your wallet asks you to pay that fee in to collect; if you do not, the buyer's coins
  go back to them after the time limit and your XMZ comes back to you. On Ethereum each side still
  pays its own fees.
- **Clearer trade limits.** An order above the opening limits now says so on the order form before
  you post it, with the limit in the coin you trade (for example "0.33 SOL per trade"). On the SOL
  pair the message wrongly named Litecoin; it now names SOL.

## Before you trade

- The swap programs and contracts are open source and were tested with real funds on each open
  pair, but they have **not been independently audited**. Trade amounts you can afford to lose.
- Trades stay inside the limits of the project's signed control document (about $25 per trade and
  $250 a day per pair while the DEX opens).
- Keep the wallet open until a trade is finished. If it closes, open it again: the trade resumes,
  or refunds after its time limit. See https://monzero.org/dex/recovery/.

## Packaged platforms

| Package | Intended compatibility scope |
| --- | --- |
| Linux x86-64 wallet (GUI), node and tools | 64-bit PC, glibc 2.35 or newer (Ubuntu 22.04, Debian 12 and later), X11 or XWayland |
| Complete source | Exact repository commit and recursive pinned submodules |

The DEX helper programs are the v1.1.0 ones except `monzero-dex-evm-helper`, rebuilt from
`gui/dex-contracts/engine` in the complete source.

## Upgrading

Close the old wallet, unpack v1.3.1 and start it; it uses the same wallet files, node data, trading
keys and trading wallet. Finish or refund a running trade first.

---

# Monzero v1.3.0 release notes

Release label: v1.3.0

Monzero v1.3.0 is a Linux wallet update. It changes no consensus rule: the chain, blocks, coins,
wallets, seeds and addresses are the same as in v1.0.0 to v1.2.0, and **nobody has to upgrade to stay
on the network**.

## What is new

- **Offers that stay open longer.** On the Exchange page a limit order can stay open for 1 hour,
  6 hours, a day, a week, a month, or a custom number of hours or days (up to 31 days). The review
  shows when it ends. An offer is answered only while your wallet runs, so keep it running when you
  can. The DEX page offers the same choices with this wallet.
- **Your own look.** Settings > Layout > Appearance: six ready-made colour sets (Monzero, Ocean,
  Sunset, Forest, Violet, Mono) or any of twelve colours by hand, and a picture of your choice on the
  wallet card, darkened as much as you like so the balance stays readable. One click returns to the
  standard look.
- **Partial fills.** The project's liquidity offers can now be taken in parts from 50 XMZ; this
  wallet takes and posts partial offers as in v1.2.0.

Offers open longer than a day are written in the newer offer format; wallets from before v1.2.0
never see them. They need the updated relays, which the project runs.

## Before you trade

- The swap programs and contracts are open source and were tested with real funds on each open
  pair, but they have **not been independently audited**. Trade amounts you can afford to lose.
- Trades stay inside the limits of the project's signed control document. A long-lived offer can
  be taken only while a valid document is published.
- Keep the wallet open until a trade is finished. If it closes, open it again: the trade resumes,
  or refunds after its time limit. See https://monzero.org/dex/recovery/.

## Packaged platforms

| Package | Intended compatibility scope |
| --- | --- |
| Linux x86-64 wallet (GUI), node and tools | 64-bit PC, glibc 2.35 or newer (Ubuntu 22.04, Debian 12 and later), X11 or XWayland |
| Complete source | Exact repository commit and recursive pinned submodules |

The DEX helper programs are the v1.1.0 ones except `monzero-dex-evm-helper`, rebuilt from
`gui/dex-contracts/engine` in the complete source.

## Upgrading

Close the old wallet, unpack v1.3.0 and start it; it uses the same wallet files, node data, trading
keys and trading wallet. Coming from a separate folder, the Exchange page asks once to let this copy's
key helper take over. Finish or refund a running trade first.

---

# Monzero v1.2.0 release notes

Release label: v1.2.0

Monzero v1.2.0 puts a full exchange inside the Linux wallet. It changes no consensus rule: the
chain, blocks, coins, wallets, seeds and addresses are the same as in v1.0.0 and v1.1.0, and
**nobody has to upgrade to stay on the network**. Upgrade if you want to trade from the wallet.

## What is new

- **Exchange page in the Linux wallet.** A new "Exchange" entry trades XMZ against **ETH, BNB and
  SOL** without opening the DEX website: pair tabs with the last price and 24 h change, a price
  chart (candles or line, 1H/4H/1D, volume, best bid and ask), an order book with depth, and a
  buy/sell form.
- **Market and limit orders.** A market order takes the best offer that fills your amount now; a
  limit order posts your own offer at your price for 1 to 24 hours. Every order is shown in plain
  words before it is sent, and each swap still asks for your approval before anything is locked.
- **Partial fills.** Offers may allow any part from a smallest amount; the wallet can take part of
  such an offer, and post its own with partial fills allowed. The rest stays on offer.
- **Your orders and funds on one page.** Open offers (with cancel), the running trade, recent
  market trades, the trading wallet (move XMZ in and out) and each coin's trade account (deposit
  address with QR code, and withdraw to your own wallet).
- The DEX website keeps working with this wallet as before.

The price chart, recent trades, balances and USD estimates are read from the open relays, the
chains' public RPC nodes the swaps already use, and monzero.org. The wallet's screens still cannot
reach the network themselves.

## Before you trade

- The swap programs and contracts are open source and were tested with real funds on each open
  pair, but they have **not been independently audited**. Trade amounts you can afford to lose.
- Trades stay inside the limits of the project's signed control document, as in v1.1.0.
- Keep the wallet open until a trade is finished. If it closes, open it again: the trade resumes,
  or refunds after its time limit. See https://monzero.org/dex/recovery/.
- Do not leave coins in the trading accounts longer than needed: withdraw them from Funds.

## Packaged platforms

| Package | Intended compatibility scope |
| --- | --- |
| Linux x86-64 wallet (GUI), node and tools | 64-bit PC, glibc 2.35 or newer (Ubuntu 22.04, Debian 12 and later), X11 or XWayland |
| Complete source | Exact repository commit and recursive pinned submodules |

The DEX helper programs are the v1.1.0 ones except `monzero-dex-evm-helper`, rebuilt for partial
offers from `gui/dex-contracts/engine` in the complete source. Other platforms keep their earlier
packages; the in-wallet exchange is for Linux.

## Upgrading

Replace the v1.1.0 files with the v1.2.0 ones, or unpack v1.2.0 next to them; both use the same
wallet files, node data, trading keys and trading wallet. Finish or refund a running trade first.

---

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
| Linux x86-64 wallet (GUI), node and tools | 64-bit PC, glibc 2.35 or newer (Ubuntu 22.04, Debian 12 and later), X11 or XWayland |
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
