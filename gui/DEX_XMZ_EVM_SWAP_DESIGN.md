# XMZ ↔ BNB / ETH atomic swap: contract design

Status: design draft, 29 October 2026. Nothing here is implemented or deployed.

## Goal and non-goals

Let a user trade XMZ for BNB, ETH or an allowlisted BEP-20/ERC-20 stablecoin
(USDT, USDC) on BNB Chain or Ethereum straight from their own wallets. Neither
the Monzero team nor the other trader can take the funds at any point.

- No wrapped XMZ token and no bridge. Every wrapped-token design needs someone
  to hold the real XMZ, which the DEX rules forbid.
- No Monzero hard fork. The XMZ side uses only what the chain already has:
  ordinary outputs sent to a shared key, the same way the BTC/XMZ swaps lock XMZ.
- No admin keys, upgrade proxy or pause switch in the contract. The app keeps
  its own operator pause and trade limits, as for the other pairs.

## Why this works without scripts on Monzero

All the lock, refund and timeout rules live in one small contract on the EVM
chain. The XMZ side only has to send coins to an address whose private spend key
is the sum of two secrets, one from each trader. Whoever learns both secrets can
spend the XMZ. The contract makes each trader reveal their secret on-chain at the
right moment:

- the XMZ seller reveals their secret to **claim** the EVM funds, which lets the
  buyer take the XMZ;
- the EVM buyer reveals their secret to **refund**, which lets the seller take
  their XMZ back.

AthanorLabs' open-source ETH↔XMR swap (`SwapCreator.sol`, LGPLv3) has already
shown this construction working between Ethereum and Monero. Monzero uses
Monero's key and address scheme, so the XMZ side is the same problem.

## Roles

The trader holding the EVM asset always deploys the swap, whichever direction the
order book shows.

| Role | Holds | Does |
| --- | --- | --- |
| **Owner** (EVM side) | BNB / ETH / stablecoin | Locks the EVM funds in the contract, confirms the XMZ lock with `setReady`, sweeps the XMZ after the claim, or refunds |
| **Claimer** (XMZ side) | XMZ | Locks XMZ to the shared address, claims the EVM funds by revealing their secret, or sweeps the XMZ back after a refund |

## Keys

Each trader generates fresh per-swap keys and never reuses them:

- an ed25519 spend-key share `s_o` (owner) or `s_c` (claimer);
- a view-key share `v_o` / `v_c`.

The shared XMZ address has the public spend key `(s_o + s_c)·G` and the private
view key `v_o + v_c`. Both traders know the view key, so both can watch the lock
output, but neither can spend it alone.

The contract runs on secp256k1, so each trader also publishes `P = s·G'` on
secp256k1, together with a **cross-group discrete-log-equality (DLEQ) proof**
that the ed25519 and secp256k1 public keys share the same secret `s`. Our engine
already contains this proof: eigenwallet's BTC↔XMR protocol uses the same
cross-curve DLEQ (`sigma_fun`) for exactly this purpose, so it is reused, not
reinvented. The secret must be below both group orders; the proof constrains
this bit by bit.

The contract stores `keccak256(P_c)` as the claim commitment and
`keccak256(P_o)` as the refund commitment. Revealing `s` on-chain lets the
contract check `s·G' = P` through its secp256k1 verifier (`mulVerify`).

## Contract

One immutable contract per chain. A swap is a struct, and its id is the hash of
that struct. Only stages are kept in storage.

```
struct Swap {
    address payable owner;      // EVM-side trader
    address payable claimer;    // XMZ-side trader's EVM address (receives the funds)
    bytes32 claimCommitment;    // keccak256(P_c)
    bytes32 refundCommitment;   // keccak256(P_o)
    uint256 timeout1;           // before this: owner can setReady or refund
    uint256 timeout2;           // after this: claimer can no longer claim, owner can refund
    address asset;              // address(0) = native BNB/ETH
    uint256 value;
    uint256 nonce;
}
enum Stage { INVALID, PENDING, READY, COMPLETED }
```

| Function | Who | Rule |
| --- | --- | --- |
| `newSwap` | owner | Locks `value`. Sets `timeout1 = now + d1` and `timeout2 = timeout1 + d2`. Stage becomes PENDING. |
| `setReady` | owner | PENDING → READY, **and only before `timeout1`** (the AthanorLabs contract states this rule in a comment but does not enforce it; ours enforces it). |
| `claim(s_c)` | claimer | Not COMPLETED. Before `timeout1` it requires READY. Must be before `timeout2`. Verifies `s_c` and pays the claimer. |
| `claimRelayer(...)` | anyone | Same rules. The claimer signs the claim, so a relayer can pay the gas for a fee. This lets an XMZ seller with no BNB or ETH claim. |
| `refund(s_o)` | owner | Allowed at or after `timeout2`, or before `timeout1` while not READY. Verifies `s_o` and returns the funds. |

Changes from `SwapCreator.sol`:

1. Enforce the `setReady` time window on-chain.
2. Reject fee-on-transfer and rebasing tokens. `newSwap` checks that the
   contract's balance grew by exactly `value`, and only allowlisted tokens are
   offered in the app. The upstream contract only warns about this case, and it
   can leave a claimer who has revealed their secret with nothing.
3. Emit the revealed secret in the `Claimed` / `Refunded` events, so the other
   side's watcher does not depend on reading calldata, which traces and relayers
   can make harder to find.
4. No owner, admin, proxy or self-destruct. A new version means a new address,
   and the app ships the addresses it trusts.

License: starting from `SwapCreator.sol` means our modified contract is LGPLv3.
That is compatible with publishing it, but keep it as a separate source file. A
clean-room rewrite is the alternative if that matters. Check for an existing
audit before relying on any upstream code. An independent audit of **our**
version is required either way (see Gates).

## Swap flow

1. **Agree terms.** This reuses the signed-quote and trade-kit flow from LTC/BTC:
   amounts, asset, chain id, contract address, both durations, XMZ amount, both
   addresses and an expiry. Keys and DLEQ proofs are exchanged and verified
   before anyone locks anything.
2. **Owner locks EVM funds** with `newSwap`.
3. **Claimer checks the lock.** The claimer waits for EVM finality and checks
   the emitted struct field by field against the agreed terms: value, asset,
   claimer address, both commitments, and both timeouts within agreed bounds. If
   anything differs, the claimer stops; the owner can refund before `timeout1`.
4. **Claimer locks XMZ** to the shared address.
5. **Owner checks the XMZ lock** with the shared view key: exact amount and
   enough confirmations (10 XMZ blocks, about 20 minutes, as in the BTC/XMZ
   swaps). Then the owner calls `setReady`, before `timeout1`.
6. **Claimer claims** with `claim(s_c)` (or through a relayer), with a safety
   margin before `timeout2`.
7. **Owner sweeps the XMZ.** The owner reads `s_c` from the `Claimed` event,
   computes `s_o + s_c`, and sweeps the shared output to their own wallet.

### Failure paths

| Situation | Outcome |
| --- | --- |
| Claimer never locks XMZ | Owner refunds before `timeout1` (not READY). This reveals `s_o`, but there is no XMZ to take. |
| XMZ lock is wrong or too small | Owner does not call `setReady` and refunds before `timeout1`. Revealing `s_o` lets the claimer recover whatever they sent. |
| Owner disappears after the XMZ lock, before `setReady` | After `timeout1`, the claimer can claim without READY. The owner can still sweep the XMZ later with the revealed `s_c`. Nobody loses. |
| Claimer disappears after READY | After `timeout2`, the owner refunds, which reveals `s_o`. The claimer can sweep their XMZ back whenever they return. Nobody loses. |
| **Both disappear for good after both locks** | Both sides stay stuck until one of them acts. Nobody else can take the funds, but nothing recovers them automatically either. AthanorLabs has the same limitation. The app must make the deadlines very visible and keep working recovery files. |
| Claimer claims too close to `timeout2` | The claim may land after `timeout2` and fail, revealing `s_c` while the owner refunds, so the owner would get both. The app refuses to claim after `timeout2 − margin`, where the margin is at least 30 minutes on BNB Chain and 1 hour on Ethereum, and rebroadcasts with a higher fee. |

### Timeouts

These are proposed defaults, to be tuned on testnet. `d1` must cover the XMZ
lock and its 10 confirmations with room to spare. `d2` gives the claimer time to
claim.

| Chain | `d1` | `d2` |
| --- | --- | --- |
| BNB Chain | 3 h | 6 h |
| Ethereum | 3 h | 12 h |

## Chains and assets

- **BNB Chain:** mainnet chain id 56, testnet 97. It has fast finality (about 2
  blocks). The app still waits for its configured confirmation count.
- **Ethereum:** mainnet 1, Sepolia 11155111. It waits for finalized blocks.
- **Assets:** native BNB/ETH plus an allowlist per chain (USDT and USDC to
  start). Token addresses are pinned in the app and never taken from a quote.
- **Gas:** the owner pays for `newSwap`, `setReady` and a refund. The claimer
  either keeps a small gas balance or uses `claimRelayer`. Relayer fees are
  shown separately before approval, like every other fee.

## Engine and app integration

- **XMZ side:** reuse the existing XMZ lock, view-key watch and sweep code from
  the BTC/XMZ engine. It is chain-agnostic about what is on the other side.
- **EVM side:** a new chain module in the swap machine, using the `alloy` Rust
  crates for RPC, transaction signing, event logs and receipts. It needs at
  least two independent RPC endpoints, which are cross-checked the same way the
  light wallet cross-checks Electrum servers.
- **Wallet:** an in-app EVM key derived from the existing seed vault, or an
  external wallet over WalletConnect. Start with the in-app key: it keeps the
  recovery files complete.
- **Recovery journal:** the same retain → check → retry rules as the other
  pairs. It stores every signed EVM transaction before broadcast, the swap struct
  and id, and the counterparty secret once seen. After a restart, the app reads
  the contract stage and the events before doing anything.
- **Market registry:** `ETH` already exists as an 18-decimal preview market. Add
  `BNB` and the stablecoins. They use 256-bit atomic amounts like ETH; the
  preview arithmetic already supports this.

## Tests before any public use

1. **Contract unit tests** (Foundry or Hardhat) for every branch above,
   including fee-on-transfer rejection, `setReady` after `timeout1`, claims at
   `timeout2 − 1` and `timeout2`, relayer signature replay, a wrong secret, and
   reentrancy through a malicious token.
2. **Property and fuzz tests** on stage transitions and timeouts.
3. **Local paired runs:** a local EVM node plus an XMZ fakechain. Cover success,
   both refunds, owner offline at `timeout1`, claimer offline until after
   `timeout2`, and a kill and restart at every step.
4. **Public testnets:** BNB testnet (97) plus the XMZ testnet, the same gates as
   BTC/XMZ: success, refund, two machines, interrupted app, power loss.
5. **Independent contract audit** before mainnet, plus the existing outside
   review gate for the app.

## Gates

Mainnet stays disabled in code until the audit is done and its findings are
closed, the testnet gates have passed, and the owner signs off. Launch caps and
the operator pause apply, as for LTC/BTC.

## Rough effort

| Step | Estimate |
| --- | --- |
| Review this design | 1–2 weeks |
| Contract and contract tests | 2–3 weeks |
| Engine EVM module, journal and app UI | 4–6 weeks |
| Testnet acceptance | 2–4 weeks |
| External audit | Depends on the auditor |

## Open questions for the owner

1. Which chain comes first, BNB Chain or Ethereum? BNB Chain is recommended for
   its lower fees and PancakeSwap users.
2. Which assets: native coin only at first, or USDT/USDC from the start?
3. Should we start from the LGPLv3 contract or write our own from scratch?
4. Should the team run a relayer for gasless claims? It would never hold user
   funds, only pay gas for a fee.
