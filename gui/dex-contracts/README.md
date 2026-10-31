# XMZ ↔ BNB / ETH swap contract

`contracts/XmzSwap.sol` is the EVM side of the XMZ ↔ BNB / ETH atomic swap designed in
[`../DEX_XMZ_EVM_SWAP_DESIGN.md`](../DEX_XMZ_EVM_SWAP_DESIGN.md). It runs unchanged on
Ethereum and BNB Chain. **Not audited; deployed only on BNB testnet** (`deployments/`). Mainnet use stays behind
the audit gate in the design.

```
npm ci          # pinned Hardhat 2.22.17, ethers 6.13.4, chai 4.5.0
npx hardhat test
```

## Choices made while building it

- **Written from scratch** (BSD-3-Clause, like the rest of the repo) rather than adapting
  AthanorLabs' LGPLv3 `SwapCreator.sol`. Same idea; about 200 lines.
- **Secrets are committed as an Ethereum address**: the address of `s·G` on secp256k1.
  A revealed secret is checked with one `ecrecover(0, 27, Gx, s·Gx mod n)` call, which
  returns the address of `s·G` (about 3,000 gas). The app derives the commitment from
  the same scalar as its ed25519 key share and proves that link to the other trader
  with the cross-curve DLEQ proof the engine already has.
- **Rules from the design that the contract enforces:** `setReady` only before
  `timeout1`; fee-on-transfer tokens rejected by checking the balance change; revealed
  secrets in the `Claimed` / `Refunded` events; no owner, admin, proxy or pause.
- **Relayed claims are included:** the claimer signs `(chain id, contract, swap id,
  fee, relayer)`, so only the named relayer can submit the claim and take the fee.
  Low-s signatures only. The team running a relayer is still an owner decision.

## Tests (27)

Secret checks (including the group edges), swap creation and validation, happy path,
the claim after `timeout1` when the owner is offline, every timeout boundary, wrong
secrets and callers, double claim and refund, changed swap data, standard tokens,
USDT-style tokens, fee-on-transfer tokens, non-contract assets, relayer front-running,
fee changes and high-s signatures, a claimer that re-enters, and a claimer that refuses
the native coin.

Mutation check (29 October 2026): each of these deliberate bugs made at least one test
fail, so the tests really check those rules:
- `setReady` allowed after `timeout1`;
- early refund allowed after READY;
- fee-on-transfer check removed;
- swap state not set before payment.

## Known limitation

A claim to an address that cannot receive the native coin reverts. The swap stays
open, but the claim transaction (and with it the claimer's secret) is still visible on
chain. The owner could then sweep the XMZ and refund after `timeout2`. The app must
always claim to a plain wallet address (an EOA) it controls, or claim a token.

## Engine crate: `engine/` (`monzero-dex-evm`)

The Rust side the swap machine will use (alloy 1.8, k256). It holds no keys and sends
nothing itself; it knows the contract byte for byte and gives the engine its checks:

- `check_lock`: the XMZ seller's check before locking XMZ. The owner's `New` event must
  come from the pinned contract on the agreed chain, hash to its swap id, match every
  agreed field, have `timeout1 = lock block time + duration1` exactly, and still be pending.
- `claim_window` / `refund_window`: the contract's timing rules, with a safety margin
  before `timeout2` for claims.
- `revealed_secret`: the other side's secret from `Claimed` / `Refunded` events, accepted
  only for this swap and only if it opens the expected commitment.
- `commitment`, `swap_id`, `relay_digest`, plus BNB mainnet (56) and testnet (97) settings.

```
cargo test                                              # 11 unit tests; chain tests skip
(cd .. && npx hardhat node) &                           # local chain
MONZERO_EVM_RPC=http://127.0.0.1:8545 cargo test --test local_chain
```

The unit tests compare against values the Solidity contract computed itself
(`scripts/vectors.js` writes `engine/tests/vectors.json`). The three chain tests deploy
the contract on a local node and drive both traders from Rust: a full swap, a refund
(including a refused refund between the timeouts), and a lock for less than agreed,
which `check_lock` refuses. All pass (29 October 2026).

### Swap states (`engine/src/machine.rs`)

`owner_step` and `claimer_step` are the XMZ <-> BNB swap states for each trader, with no
I/O: the engine's journal polls both chains into a `Snapshot` (EVM time, the owner's lock,
the contract stage, a revealed secret, the XMZ at the shared address, whether our sweep
confirmed) and gets back the next state and the actions to make sure of
(`SendNewSwap`, `SendSetReady`, `SendRefund`, `LockXmz`, `SendClaim`, `SweepXmz`). Actions
are idempotent: the journal signs each transaction once and rebroadcasts the same one.
States serialize (serde) so a restart resumes exactly.

Safety rules the states enforce, each covered by a test that a deliberate mutation broke:
- **No claim or refund on an unread contract stage.** A failed claim or refund still
  publishes its secret in the calldata, which could give the other side both secrets.
- The claimer never locks XMZ unless `check_lock` passes and at least `xmz_lock_budget`
  (1 h) is left before `timeout1`.
- The owner refunds early on a wrong XMZ amount, or when no confirmed XMZ lock exists
  `owner_refund_margin` (30 min) before `timeout1`; it never sends `setReady` that late.
- No claim within `claim_margin` (30 min) of `timeout2`; the claimer then waits for the
  owner's refund and sweeps its XMZ back.
- A revealed claim always leads the owner to sweep the XMZ, even if its own refund was
  racing; a revealed refund always leads the claimer to sweep back.

21 scenario tests cover every row of the design's failure table.

### Driver (`engine/src/driver.rs`, `chain.rs`, `secret.rs`)

`Driver::tick` runs one swap round: snapshot both chains, step the states, save, act.
- `EvmChain` is implemented over JSON-RPC by `AlloyChain`: the deadline clock is the newest
  block; locks and stages are read at the newest block with enough confirmations; revealed
  secrets at any depth.
- `XmzSide`: watch the shared address, make sure the lock is sent, make sure the sweep is
  sent (`WalletRpcXmz`, below).
- `Journal` keeps the state and every signed EVM transaction. `FileJournal` writes JSON
  atomically and holds no secret.
- Recovery: the next state is saved before acting. Each EVM transaction is signed once,
  saved, then (re)broadcast as the identical bytes.
- **Fee bumping:** a transaction still unmined `Chain::bump_after_blocks` after it was signed
  (40 blocks on BNB Chain) is replaced by the same call with the same nonce and gas limit
  and at least 12.5% more on both fees (or the current estimate, if higher), up to
  `Chain::max_fee_cap` (50 gwei on BNB Chain). Every version is kept before broadcast; the
  nonce makes sure only one is mined. Once the nonce is used there is nothing to replace.
- **Signing runs gas estimation first, which executes the call:** a claim or refund the
  contract would reject is never signed, so it can never leak our secret.
- `SecretShare` fixes the encoding: one number k with 1 <= k < l (the ed25519 order),
  big-endian `bytes32` for the contract and little-endian scalar for XMZ.
  `combined_spend_key` is the shared address's private spend key.

Two drivers trade against each other on a local node, with a stand-in XMZ side that only
accepts the exact combined key (`tests/driver_local.rs`):
1. a full swap with the owner rebuilt from its journal file on every tick (locks once,
   owner sweeps with the right key, claimer receives the BNB);
2. the owner refunds early when no XMZ arrives;
3. the claimer is offline until after `timeout2`: the owner refunds, then the returning
   claimer sweeps its XMZ back;
4. an early claim and a refund with the wrong secret are refused at signing;
5. a lock stuck behind a fee spike (blocks mined without it) is replaced once, with the
   same call and nonce and fees above the spike, then mined exactly once.

```
MONZERO_EVM_RPC=http://127.0.0.1:8545 cargo test --tests -- --test-threads=1
```

### XMZ side (`engine/src/xmz.rs`)

`WalletRpcXmz` implements `XmzSide` over `monzero-wallet-rpc` (JSON-RPC with the RPC's
digest login, read from its `.login` file):
- `SharedXmz`: spend key = sum of both public spend shares (the other trader's point must be
  in the prime-order subgroup), view key = sum of both view shares; standard address in the
  Monero base58 + Keccak checksum format, checked against the core's address vectors.
- Two wallet RPCs per trader: `main` (the trader's funds wallet, already open) sends the
  lock and receives the sweep; `swap` holds the shared address, view-only until the
  combined spend key is known, then a spending wallet restored from that key.
- The lock and the sweep are built with `do_not_relay`, kept in the XMZ record file **before**
  relaying, and later ticks relay the identical transaction (same rule as the EVM side).
- The watcher counts only incoming transfers to the primary address with no unlock time:
  an output that could not be swept in time does not count as a lock.
- The sweep waits until the whole balance has unlocked, sweeps once, and is done after
  `sweep_confirmations`. A key that does not open the shared address is refused.

Tests against a real local XMZ chain (`monzerod --regtest` fakechain plus wallet RPCs, started
by `tests/common`):
- `tests/xmz_local.rs`: lock sent once and seen with its exact amount, wrong key refused,
  no sweep before unlock, sweep built once, swept after 10 confirmations;
- `tests/driver_local.rs`: a **full XMZ <-> BNB swap with both traders on real wallets**
  (the owner's own wallet receives the XMZ, the claimer the BNB), and a **refunded swap**
  where the returning claimer sweeps its XMZ back with both shares.

```
MONZERO_EVM_RPC=http://127.0.0.1:8545 MONZERO_XMZ_BIN=<dir with monzerod, monzero-wallet-rpc> \
  cargo test --tests -- --test-threads=1
```

### Key exchange (`engine/src/negotiation.rs`)

After both traders accept the same `Offer` (trade id, chain, pinned contract and asset,
amounts, durations, XMZ network, expiry), each sends one frame:
- k·G (secp256k1), whose address becomes its commitment in the contract; k·B (ed25519), its
  share of the shared XMZ spend key; its private view share;
- a **cross-curve DLEQ proof** that both points hide the same k (`sigma_fun`, same proof
  system and Pedersen generators as the BTC/LTC/BCH swaps). It stops a trader committing to
  a secret that does not open its XMZ share, and stops a rogue XMZ share chosen from the
  other trader's to take the shared address alone;
- bound to the offer digest and role, and signed (EIP-191) by that role's EVM account.

`KeyExchange` accepts both frames in any order (the same frame again is a no-op; a different
frame for a filled role is refused), checks our own frame carries our keys, and `finish`
gives the `AgreedTerms` with both commitments, the `SharedXmz` and a transcript hash. New
secret shares are below 2^252, the range the proof covers.

Tests: 8 unit tests (rogue key, mixed points, borrowed proof, zero view, small-order point,
wrong signer, role swap, equivocation, copied share, other offer, expiry, non-canonical
encoding, not our keys); 7 mutations of the checks are each caught. The two real-wallet
swaps in `driver_local.rs` now run on the terms and shared address from the key exchange.

### Per-trade keys from the swap seed (`engine/src/swap_seed.rs`)

The vault keeps one swap seed (HMAC-SHA256 of the wallet seed with its own label). Each
trade's secret share and view share come from it plus the offer digest and our role, and so
does the randomness of our cross-curve proof. Nothing secret is stored per trade, restoring
the wallet seed restores every trade's keys, and after a restart our frame is remade as the
same bytes, so the other trader never sees a second frame. Keys depend on the whole offer,
not just the trade id, so a secret revealed on chain never opens another offer's lock. A
pinned test vector guards against an upgrade silently changing the derivation. Tests: 4;
mutations caught: random proof, trade id only, role ignored, wallet seed used directly.

### In-app EVM account (`engine/src/evm_key.rs`)

`evm_account(wallet_seed, i)` derives the account at the standard Ethereum path
m/44'/60'/0'/0/i (BIP-32) from the wallet's BIP-39 seed, so the wallet's recovery words
also restore these accounts in MetaMask or any EVM wallet. BNB Chain uses the same
accounts. Checked against the accounts every EVM tool shows for the "test … junk" phrase;
4 mutations caught (unhardened account level, uncompressed parent key, missing 0x00
prefix, index bound). `SwapSeed::wallet_password` gives each trade's XMZ swap wallet its own
password, remade from the seed after a restart or restore.

### Key-exchange protocol (`engine/src/exchange.rs`)

libp2p request-response `/monzero/xmz-evm/key-exchange/1.0.0` (JSON), like the other pairs'
protocols. One round trip: the trader who starts sends its frame; the other answers with its
own frame only once the request passed every check (signed by the offer's account for that
role, valid proof, right trade, not expired, not our own frame reflected). A refusal never
carries a frame, so a private view share only reaches the account the offer names. Frames
come from the swap seed, so asking or answering again after a restart gives the same bytes.
Tests: 5, including a real exchange between two libp2p nodes over TCP; 5 mutations caught.

## BNB testnet deployment (chain 97)

`XmzSwap` at `0xf7c19f735C3494b356091eFBeFf3ab53617190b7`, deployed 30 October 2026 in tx
`0x83d87e99…8318` (block 139,790,203, 1,141,519 gas). Its runtime code on chain matches the
compiled artifact byte for byte (keccak `0xe2368d8f…1f1a`), checked on two RPC servers.
Record: `deployments/bsc-testnet.json`. Script: `scripts/deploy-testnet.js`, which refuses any
chain but 97. The test accounts' phrase lives outside the repository
(`~/.config/monzero-dex/bsc-testnet-accounts.json`, mode 600); first round is native BNB
only, no relayer.

### The app completes a BNB testnet trade (30 October 2026)

The Monzero GUI (DEX prototype build) ran the owner's side of a live BNB testnet trade through
`EvmTradeJob` and the helper: it showed 0.02 BNB for 20 XMZ, waited for the user's approval,
then locked the BNB, sent setReady and swept the XMZ on its own 20-second steps. Its wallet
received 19.9903 tXMZ; the claimer (a `bsc_trader` process) got the tBNB. Evidence and the
approval and final screenshots: `testnet-runs/bsc-testnet-gui-1*`. The run led to queued
approvals and to showing the saved stage after a failed step.

### Two machines and real power cuts on BNB testnet: passed (30 October 2026)

`engine/examples/bsc_trader.rs` runs one trader per process. A swap between two processes
passed (`testnet-runs/bsc-testnet-2proc-1.json`), then one with the claimer on the Debian VM:
its power was cut right after it sent its XMZ lock and again right after it sent its claim
(`testnet-runs/bsc-testnet-vm-powercut-1.json`). Journals and XMZ records survived both
(`durable_write` flushes file and directory); the first cut left the shared watching
wallet's file empty, so the engine now rebuilds such a wallet from the trade keys. Both
traders ended `Swapped`, each transaction once.

### Kill-and-resume swap on BNB testnet: passed (30 October 2026)

`engine/examples/bsc-testnet-killrun.sh` hard-killed the runner and its XMZ wallet RPCs 22
times (12 at the first appearance of each stage, 10 at random moments) and resumed each time
with `Driver::resume`. The swap completed with every transaction made exactly once (one BNB
lock, setReady, claim, XMZ lock and XMZ sweep); the owner's wallet holds 19.9903 tXMZ and the
claimer gained 0.019996 tBNB. Evidence `testnet-runs/bsc-testnet-kill-1.json`. This is
process-kill coverage, not a power cut; a hard power-off run on the VM is still to do.

### Refund on BNB testnet: passed (30 October 2026)

The claimer went offline after setReady; the owner refunded after timeout2 (`0x515cc69b…f302`)
and the claimer, back online, read the owner's revealed share from the chain and swept its
XMZ back (`ebf73a42…6c02`). Its own wallet holds 29.9806 tXMZ of the 30 it started with (two
network fees); the owner lost only gas. Evidence `testnet-runs/bsc-testnet-refund-1.json`.
The first attempt (`testnet-runs/bsc-testnet-swap-1.json`) also ended refunded: its claimer's
endpoint could not read logs, so it never locked, and its owner refunded early.

### First swap on BNB testnet: passed (30 October 2026)

XMZ <-> BNB with the real contract, the libp2p key exchange and fresh XMZ testnet wallets
(`engine/examples/bsc_testnet.rs`, evidence `testnet-runs/bsc-testnet-swap-2.json`):
BNB lock `0x3e1d9e81…e58f`, XMZ lock `27d2d746…fee5`, setReady `0xadee4c56…1c4f`, claim
`0x383ae8c0…303f`, owner sweep `9c9117dc…b317`. The claimer gained 0.019996 tBNB (0.02 less gas);
the owner's own XMZ wallet holds 19.9903 tXMZ (20 less the sweep fee; XMZ has 11 decimals). Both ended `Swapped`.

What the run found and fixed in the engine:
- **No HTTPS**: alloy was built without TLS, so every public endpoint failed. Now rustls.
- **Log queries**: BNB Chain's own testnet seeds refuse `eth_getLogs`; drpc's free tier serves
  only recent blocks; onfinality's rate-limits. Logs are now read in per-endpoint chunks,
  only new blocks each tick, with retries on HTTP 5xx/429, and `check_logs` tests history
  (a range ending 1,000 blocks back) before trading. publicnode served both traders.
- **The offer must be on disk before any lock**: keys come from the seed and the offer, so a
  trader that loses the offer cannot refund. The runner writes `run.json` first and has a
  `resume` mode; the app must keep the agreed offer in its journal the same way. (The
  first attempt, before this fix, is being refunded by its owner after the early-refund
  time.)

### Saved trades (`Driver::start` / `Driver::resume`)

Once keys are agreed, `Session::saved` gives a `SavedTrade`: the offer, our role, the other
trader's signed frame, timings, the EVM start block, the XMZ restore height and the agreed
transcript. `Driver::start` writes it into the swap journal before the driver can act, and
`Driver::resume` rebuilds the trade from that journal and the swap seed alone, also after the
offer has expired (`KeyExchange::restore` runs every frame check except expiry). A rebuild
must reproduce the saved transcript, so a different seed (a wrong wallet restored) is refused
instead of producing a different trade. The journal holds the other trader's private view
share and must stay private to the wallet. Tests: unit test plus a local-chain swap whose
owner is rebuilt from journal and seed every tick; 3 mutations caught (transcript check,
expiry on restore, their frame not kept).

Approval: a trade started with `Driver::start` does not lock our funds (the owner's
`newSwap`, the claimer's XMZ lock) until `Driver::approve` records the user's approval of that
saved trade's transcript; until then `tick` returns `AwaitingApproval` without advancing the
state. Steps that protect funds (setReady, claim, refund, sweeps) never wait for a click.
3 mutations caught (no gate, any transcript accepted, XMZ lock not gated).

### App helper (`monzero-dex-evm-helper`, `engine/src/helper.rs`)

One JSON request on stdin, one response on stdout, as the GUI runs the other pairs' helpers
(schemas `monzero-xmz-evm-helper-request-v1` / `-response-v1`). `status` reads the trade
folder (stage, approval, commitment, offer terms, deadlines, EVM and XMZ transaction ids; no
secrets, no network); `approve` records the approval only for the commitment `status` showed;
`tick` runs one step with the wallet seed and endpoints the app passes in, and reports
`awaitingApproval` instead of locking unapproved funds. Tests: 3 (status without secrets,
approval binding, malformed requests); `status` checked on the real two-process testnet trade.

## Next

- Gas measurements and a fuzz/property pass (Foundry, or Hardhat with random inputs).
- App: run `monzero-dex-evm-helper` from the GUI (hash-checked, like the other helpers),
  add the key-exchange protocol to the app's swarm, pass the vault seed, and the screens.
- Testnet deployment on BNB testnet (chain 97) for the paired XMZ testnet runs.
