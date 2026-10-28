# Monzero: An Independent Privacy-Preserving Digital Currency

**Protocol paper, version 1.5 — 17 October 2026 (Monzero v1.0.0)**

## Abstract

Monzero is an independent proof-of-work digital-currency network derived from
the Monero codebase. Its native currency, XMZ, is designed for private,
permissionless electronic payments: one-time destination keys obscure the
recipient, ring signatures make the spent output ambiguous, confidential
commitments hide transferred amounts, and key images prevent double spending.
RandomX proof of work orders transactions and secures the ledger without a
premine or stake allocation.

Monzero has a distinct genesis block, network identifiers, address prefixes,
ports, monetary policy, and chain history. It targets 100 million XMZ through a
smooth primary-emission curve and then maintains a permanent subsidy of 3.26
XMZ per two-minute block. This paper describes the protocol of Monzero v1.0.0,
the first stable release, including the LWMA difficulty rule that activates at
block 21,000, together with its security assumptions and limitations. The
implementation has not been independently audited; security review continues
with public use. This document is not an investment prospectus.

## 1. Motivation and design goals

A public ledger is useful for independent verification, but a transparent
transaction graph exposes balances and commercial relationships. Monzero aims
to preserve public verification of the rules without making every payment
publicly traceable.

The protocol has five primary goals:

1. **Transaction privacy.** Avoid directly publishing the sender, recipient,
   or amount of an ordinary payment.
2. **Permissionless verification.** Allow any participant to run a node and
   independently validate the entire chain.
3. **Permissionless issuance.** Distribute new XMZ through proof of work, with
   no hard-coded premine.
4. **Fungibility.** Minimize transaction-history information that could cause
   otherwise equal units to be treated differently.
5. **Sustainable security.** Continue paying a block subsidy after primary
   emission approaches its target.

Privacy is not binary. Network observations, wallet compromise, user behavior,
small anonymity sets, malicious peers, and external information can all weaken
it. The protocol reduces information exposed by the ledger; it does not make
operational security unnecessary.

## 2. System model

Monzero uses an unspent-output ledger. Wallets construct transactions that
consume prior outputs and create new ones. Full nodes independently check each
transaction, maintain the set of spent key images, validate proof of work, and
select the valid chain with the greatest cumulative difficulty.

The principal participants are:

- **wallets**, which hold private keys, discover owned outputs, construct
  transactions, and sign spends;
- **full nodes**, which relay and validate transactions and blocks;
- **miners**, which assemble valid candidate blocks and search for RandomX
  proofs of work; and
- **light or remote-wallet users**, who delegate some chain access to a node
  and consequently adopt additional privacy and availability assumptions.

No central party assigns accounts or approves payments. Consensus is defined
by deterministic validation in the node software. Human-readable documents
explain that implementation but do not override it.

## 3. Transaction privacy

Monzero inherits the mature CryptoNote and Monero transaction model. The
mechanisms below provide complementary protections; none should be evaluated
in isolation.

### 3.1 One-time destination keys

A standard address contains public spend and view keys. For each payment, the
sender derives a unique one-time output key using an ephemeral transaction
secret and the recipient's public keys. The recipient uses the private view
key to scan the chain and the private spend key to authorize spending.

Consequently, outputs sent to the same published address do not contain that
address or a reusable destination key on chain. Subaddresses let a wallet
present unlinkable receiving identities for different contexts. Integrated
addresses additionally carry a short payment identifier, but subaddresses are
generally the safer separation mechanism for new integrations.

### 3.2 Ring signatures and key images

To spend an output, a wallet forms a ring containing the real output and
decoys selected from the chain. A Concise Linkable Spontaneous Anonymous Group
(CLSAG) signature proves that one member is authorized without identifying
which one. At the active protocol version, a normal ring contains 16 members:
one real input and 15 decoys.

Each real output deterministically produces a key image. The image does not
reveal which ring member was spent, but a second use of the same output
produces the same image. Nodes reject repeated key images, providing
double-spend detection without revealing the consumed member.

Ring signatures provide plausible ambiguity, not a guarantee that all members
are equally likely. Decoy-selection quality, output age, unusual spending
patterns, chain forks, and information learned elsewhere can reduce the
effective anonymity set.

### 3.3 Confidential amounts

Ring Confidential Transactions encode amounts in Pedersen commitments. A
commitment to amount \(a\) with mask \(x\) has the form

\[
C = xG + aH,
\]

where \(G\) and \(H\) are independent curve points. Commitments are additive,
so nodes can verify that inputs equal outputs plus the public transaction fee
without learning the hidden values.

Bulletproofs+ range proofs demonstrate that each committed output amount lies
in the permitted range. This prevents creating value through negative or
overflowing amounts while keeping the amounts confidential. Encrypted amount
data lets the intended recipient recover the value and commitment mask.

### 3.4 Transaction propagation

The peer-to-peer layer includes Dandelion++ stem-and-fluff relay behavior and
optional Tor/I2P connectivity. These measures seek to make source-IP inference
harder, but they are not an anonymity proof. A remote RPC operator can observe
requests, timing, and IP metadata; users with stronger threat models should run
their own node and use an appropriate anonymity network.

## 4. Consensus and proof of work

### 4.1 Blocks and chain selection

Miners gather valid transactions, construct a coinbase transaction paying the
block subsidy and fees, and search for a header hash below the current target.
Nodes accept only blocks whose transactions, timestamps, size, reward, and
proof of work satisfy consensus. Among competing valid histories, nodes follow
the chain with the greatest cumulative difficulty.

The target interval is 120 seconds. Difficulty adjusts from recent block
history to keep the interval near that target as hash rate changes (Section
4.4). Short-term block times remain stochastic.

### 4.2 RandomX

Protocol version 16 uses RandomX, a proof-of-work function optimized for
general-purpose 64-bit CPUs. RandomX combines randomized code execution and
memory-hard techniques to narrow, rather than eliminate, the efficiency gap
between commodity processors and specialized hardware.

Proof of work gives the chain an objective accumulated-work ordering and makes
history replacement costly in proportion to the honest network hash rate. It
does not protect a small network from an attacker able to obtain majority hash
power. Hash-rate concentration and the availability of independent miners are
therefore material security considerations.

### 4.3 Adaptive block weight and fees

Monzero inherits an adaptive block-weight mechanism. The full-reward zone at
the active protocol is 300,000 bytes, and recent block weights influence the
permitted weight and reward penalty. Blocks can grow when demand persists, but
miners incur a subsidy penalty for blocks above the effective median. Dynamic
per-byte fees discourage spam and compensate miners for including
transactions. If ordinary reward calculation fails while estimating a fee,
the implementation conservatively derives its fallback upper bound from the
maximum base subsidy permitted by the monetary parameters. This bound is tied
at compile time to the 120-second block target so a target change cannot
silently retain the old assumption.

### 4.4 Difficulty adjustment

Before block 21,000, Monzero uses the inherited Monero rule: the timestamps of
the last 735 blocks are sorted, the newest 15 are ignored (the lag), and the
60 earliest and latest of the remaining 720 are cut before total work is
divided by the remaining time span. That rule suits a large, steady network.
On a small one it follows a large change in hash rate slowly: in simulation it
needs about 670 blocks to settle after a hundredfold rise or fall. On Monzero
in October 2026 this produced blocks about every 57 seconds for a day and a
half after the public pool opened, issuing coins at twice the planned rate, and
earlier gaps of up to ten hours when large miners left.

From hard fork 17 (block 21,000), the difficulty of the next block is LWMA-1, a
linearly weighted moving average of the newest \(N = 60\) solve times
introduced for CryptoNote chains by zawy12. With target \(T = 120\) seconds,
solve times \(t_1, \dots, t_N\) from oldest to newest and \(\bar D\) the mean
difficulty of those \(N\) blocks,

\[
L = \max\left(\sum_{i=1}^{N} i \cdot \min(t_i, 6T),\; \frac{N^2 T}{20}\right),
\qquad
D_{next} = \left\lfloor \frac{\bar D \cdot 0.99 \cdot T \cdot N(N+1)}{2L} \right\rfloor .
\]

A block whose timestamp is not later than its predecessor's counts as a one
second solve, and no solve time counts for more than six targets, so a single
misdated block moves the result by only a few percent. Newer blocks carry more
weight, so difficulty settles within about 106 blocks after a hundredfold rise
in hash rate and about 145 blocks after a hundredfold fall. From the same
block, a block may be timestamped at most \(N T / 20 = 360\) seconds ahead of
the validating node's clock (previously two hours), which stops a miner from
claiming a far-future time to lower difficulty. Node operators should keep
their clocks synchronized.

## 5. Monetary policy

One XMZ is divided into \(10^{11}\) atomic units. Eleven decimal places are the
maximum precision compatible with the 100,000,000-XMZ primary-emission target
and the unsigned 64-bit amount representation used by this implementation.

Let \(M = 10^{19}\) atomic units be the primary-emission target and \(A_n\) the
cumulative generated amount before block \(n\). With a two-minute target, the
base subsidy is

\[
R_n = \max\left(\left\lfloor\frac{M-A_n}{2^{19}}\right\rfloor,
326{,}000{,}000{,}000\right)
\]

atomic units, before any block-weight penalty. The second term is 3.26 XMZ.
The first subsidy is approximately 190.73486328125 XMZ and declines smoothly
with issuance. The permanent floor eventually replaces the declining term.

At the target interval, tail emission is approximately 856,728 XMZ per
365-day year. Because the denominator (circulating supply) continues to grow,
the percentage issuance rate trends toward zero, while the absolute subsidy
continues funding proof-of-work security. Transaction fees are paid in XMZ and
are added to the miner reward.

There is no hard-coded premine. Coins produced during the private bootstrap and
test period are nevertheless real chain issuance. On 16 October 2026 the
founder published their mined holdings (about one fifth of the XMZ mined by
then), how they were mined, and binding commitments on their use at
[monzero.org/founder](https://monzero.org/founder/). The current circulating
supply, which equals the total mined, is published at
[monzero.org/api/supply](https://monzero.org/api/supply/).

## 6. Network identity and parameters

Monzero is not a Monero test network and does not share Monero's ledger.

| Parameter | Mainnet value |
| --- | --- |
| Currency / ticker | Monzero / XMZ |
| Target block interval | 120 seconds |
| Atomic units per XMZ | 100,000,000,000 |
| Primary-emission target | 100,000,000 XMZ |
| Tail subsidy | 3.26 XMZ per block |
| Proof of work | RandomX at protocol v16 |
| Difficulty | Inherited rule to block 20,999; LWMA-1 over 60 blocks from block 21,000 |
| Future timestamp limit | 2 hours; 360 seconds from block 21,000 |
| Transaction version | 2 |
| Ring size | 16 at the active protocol |
| Coinbase unlock | 60 blocks (about 120 minutes) |
| Normal output spendable age | 10 blocks (about 20 minutes) |
| Address prefixes | 86 / 87 / 88 |
| P2P / RPC / ZMQ ports | 6174 / 6175 / 6176 |
| Network UUID | `94834264-d0b2-41dd-b0f2-0ada675c7710` |
| Genesis nonce | 2271206363 |
| Genesis block hash | `84f9ebdac8924806f037482ec16fd59b271e954d3e00363dd6c7e4ce9dd659e4` |

Separate testnet and stagenet identities are defined in the machine-readable
consensus manifest. Wallet seeds and private keys should never be reused
between Monzero, Monero, or any other CryptoNote-derived chain. Cross-chain key
reuse can turn activity on one ledger into information about another.

## 7. Protocol activation and upgrades

The inherited protocol revisions are scheduled at heights 1 through 16, one
version per block. This compressed bootstrap schedule means that the intended
steady-state public protocol is version 16 rather than a replay of Monero's
historical activation timeline. Public mining should begin only after those
bootstrap revisions have activated.

Monzero v1.0.0 schedules the first post-bootstrap upgrade: hard fork 17 at
block 21,000 on mainnet changes only the difficulty rule and the future
timestamp limit (Section 4.4). It is not a chain reset; every earlier block,
output and wallet remains valid. Nodes must run v1.0.0 or later before that
height; a node that does not upgrade stops following the main chain there.
Wallets must be updated as well: the inherited wallet checks each block's
version against its own fork table and stops syncing at the first version-17
block until it runs v1.0.0. No funds are affected; the same wallet file or seed
syncs normally in the updated software.

Consensus upgrades require a new rule version, implementation review, tests,
release coordination, and broad operator adoption. Nodes that enforce
incompatible rules can split into separate networks. The source code and
`config/monzero-consensus.json` are authoritative for exact validation
parameters; changes to this paper alone do not alter consensus.

## 8. Monzero Assets research

The repository contains an inactive prototype for fixed-supply private assets.
Its intended model would make an asset identifier, issuance, and maximum
supply public while seeking to hide owners, recipients, balances, and transfer
amounts. XMZ would remain the only currency for fees and mining rewards.

This feature is **not active**. Asset transactions are not serialized or
accepted by current public consensus, wallets do not provide active asset
support, transaction version remains 2, and proposed hard-fork version 18 has
no activation height. (It was version 17 until v1.0.0 assigned that version to
the difficulty upgrade.) Descriptor, authorization, conservation, registry, and
ownership-proof code exists only as a reviewable research scaffold.

Activation must not occur without Phase 0 stabilization, an independently
reviewed cryptographic construction, complete consensus and wallet
integration, adversarial testing, an isolated development network, and an
extended public testnet. Thinly traded assets would also have smaller
practical anonymity sets than XMZ; cryptography cannot manufacture unrelated
activity.

## 9. Peer-to-peer exchange research

The graphical-wallet source contains an opt-in DEX research build, separate
from published wallet packages. BTC/XMZ is the existing foundation. The market
registry also represents LTC, BCH, XMR, DOGE, ETH and native THORChain RUNE;
selecting a coin or displaying a quote does not authorize execution. All public
market activation gates remain closed.

The initial application trading fee is zero, separate from network fees. The
current exact-payment policy delivers the signed XMZ principal in full: a
separate buyer-funded reserve covers its network fee and unused reserve returns
to the agreed address. Earlier proposals to deduct fees from quoted proceeds
are superseded by this policy. Future application-fee rules, price references,
cancellation-fee responsibility and reserve funding for buyers without XMZ
still require decisions and implementation. A quote cannot guarantee liquidity
or impose prices or fees on activity outside this protocol.

### 9.1 Signed market evidence and message recovery

The market-facing prototype implements non-executable security evidence. It includes canonical Ed25519-signed offers, cancellations
and settlement quotes; a validating persistent order book; deterministic
crossing and crash-safe fill reservations; replay-resistant quote and relay
stores; read-only chain and wallet observations; Bitcoin SegWit-v0 sighash,
PSBT, fee, recovery, and confirmation evidence; typed XMZ success and refund
evidence; and a crash-safe settlement journal. Settlement evidence binds the
external redeem, XMZ principal delivery, and XMZ protocol-fee payment to the
same accepted quote and fill. Refund evidence instead requires the confirmed
external refund and two distinct XMZ refund legs, and cannot mark a protocol
fee earned. Sticky reorganization tracking invalidates outcomes when an
observed transaction disappears or changes block.

Market messages can be wrapped in canonical, length-prefixed relay envelopes
that bind the artifact commitment, sender identity, deployment domain,
lifetime, type, and sequence. A locked owner-only replay store re-verifies each
signature and accepts a new sender only at sequence one, followed by exact
increments. Exact retries are idempotent; skipped, stale, conflicting, expired,
cross-network, or unauthenticated messages fail closed. This is an
authentication and persistence boundary only: peer discovery, transport,
availability, admission control, and liquidity do not arise merely from
having a chain adapter or signed message.

The local ingress coordinator independently re-verifies the relay wrapper and
the maker-signed offer or cancellation, requires an exact artifact commitment
and message-type match, persists replay state first, and then applies an
idempotent order-book mutation. An exact retry can therefore recover an
interruption between those two durable writes without accepting a conflicting
message. Relay peers may differ from makers because the inner artifact retains
its independent maker signature.

The current development tree persists the signed wrapper and pending inner
artifact together in replay-store schema version 4. Local recovery reconstructs
the stored offer or cancellation and re-verifies it before applying the
order-book mutation. Only an exact, previously pending wrapper may recover
after its relay lifetime expires; the inner artifact must still pass its own
validation. Corrupted inner payloads leave the operation pending and prevent
sequence advancement. Completion clears the pending payload. There is no
automatic policy for skipping an invalid or expired pending artifact, and this
local recovery mechanism does not supply a peer transport.

Authenticated relay evidence has a deterministic binary wire frame with an
explicit version, fixed network-byte-order integers, bounded length-prefixed
fields, exact body length, and a 1,024-byte ceiling. Decoding rejects truncated,
extended, oversized, noncanonical, mutated, expired, or cross-deployment frames
before returning opaque evidence. The codec does not open a socket or discover a
peer.

### 9.2 Settlement evidence as of 26 September 2026

Native execution controllers now sign and relay transactions in controlled,
disposable test networks. This is distinct from the non-executable market
interface; it does not enable real-money trading. Test results remain
`executable=false`, and complete recovery coverage remains unverified.

| Market | Implemented and verified | Remaining scope |
| --- | --- | --- |
| BTC/XMZ | Existing isolated exact-payment and recovery foundation, including lost-reply and wrong-wallet checks | Complete GUI workflow, broad adversarial coverage and release acceptance |
| LTC/XMZ | Authenticated key exchange, signed payout/reserve terms, durable execution journals, complete isolated success path and cancellation/refund path | Broader interruption and reorganisation coverage, GUI execution and independent review |
| DOGE | Eight-decimal preview; actual isolated P2SH timelock and refund primitive on two Linux hosts | Dogecoin-compatible cross-chain protocol, production signing and complete settlement/recovery |
| ETH | Exact 18-decimal previews with 256-bit wei amounts | Contract settlement, gas/nonce handling, recovery and contract tests |
| XMR | Twelve-decimal preview; explicit sequential routing in scope | Route execution, separate per-leg fees, failure handling and recovery |
| Native RUNE | Eight-decimal preview for native THORChain RUNE | Native-chain route integration, execution and recovery; no ERC-20 substitution |
| BCH | Retained market representation | Its own settlement and recovery acceptance before activation |

The Litecoin success fixture uses the production controllers with isolated
Litecoin regtest and XMZ fakechain nodes. It verifies both funding legs,
Litecoin redemption, adaptor-based native key recovery, restored spend-wallet
access, exact native principal delivery, unused reserve return and funding-fee
accounting. Complete cancellation/refund fixtures passed on both Linux hosts:
premature Litecoin cancellation is rejected, a mature cancellation/refund is
confirmed, and the recovered XMZ balance is paid to the signed refund address
within its fee ceiling. In the recorded refund fixture, a 2 XMZ lock yields
1.9903552 XMZ to the recipient and a 0.0096448 XMZ network fee. These amounts
describe that test, not a fixed fee schedule.

Persistence retains exact prepared transactions and private proof material
before relay. Focused tests cover uncertain broadcast replies, deliberate
retransmission, missing or altered committed records, offline reloads,
confirmation loss and wallet-cache reconstruction. Subaddress payments,
coincident principal/reserve destinations, zero reserve returns and rejection
of a substituted watch-only wallet have isolated test evidence. Passing these
cases does not prove every crash, peer failure or chain reorganisation safe.

Dogecoin fixtures passed on both hosts using disposable Dogecoin 1.14.9 regtest
nodes. A 10 DOGE lock rejects early refund and an incorrect transaction
locktime; the mature refund pays 9.99 DOGE with a 0.01 DOGE fee. This validates
a timelock primitive only. No DOGE/XMZ swap, production signer, adaptor protocol
or mainnet compatibility claim follows from that result.

### 9.3 GUI boundary and unfinished validation

A native read-only Litecoin journal helper and a GUI saved-trade panel are
implemented. Full GUI builds, focused C++ checks and seven QML presentation
checks passed on both Linux hosts. The actual helper also read a completed
refund journal successfully. Saved journal state is explicitly not a fresh
chain confirmation and cannot grant execution authority.

The full actual Qt-to-helper handoff remains unverified. An initial integration
run correctly rejected a hardlinked build executable; the fixture now installs
a private single-link copy without weakening the executable check. Its rerun
was interrupted at the development stopping point before a result.

A new native refund-review API binds the reviewed destination, amount, fee,
trade, journal state and send-versus-retry action to the retained payment.
Focused commitment tests and compilation passed on both hosts. Its expanded
controller cases and complete actual-chain approval-bound refund reruns remain
unfinished; the latter were stopped before completion. A review token alone
does not prove user consent. Actual GUI send/retry controls and a deliberate
approval gesture are still required.

Explicit XMZ-to-BTC-to-XMR or other supported routes are intended as sequential
trades, with separate quotes, fees, intermediate holdings and recovery for each
leg. They are not an atomic all-or-nothing swap across the entire route. No XMR
or native RUNE route is executable at this snapshot.

Public peer discovery and transport, liquidity, pricing policy, complete
per-market recovery, platform validation, independent security review and
release acceptance remain prerequisites for activation. No new downloadable
DEX release or completed audit is announced by this paper.

### 9.4 Status as of 17 October 2026

LTC/XMZ is the first pair; work on other pairs is paused until it runs as a
capped test with invited participants. On the public Litecoin test network
(against a private XMZ test network), complete swaps have succeeded, including
one funded from the wallet's built-in Litecoin light wallet with no Litecoin
node, and an interrupted trade was recovered through its timelocked refund.
The trading apps now reconnect by themselves when their connection drops,
including during settlement, and the light wallet reads Litecoin MWEB
transactions. Live trading remains disabled in released software; mainnet
trading requires its own approval, caps, an operator kill switch and review.

## 10. Security assumptions and limitations

Monzero's safety depends on more than correct cryptographic primitives:

- **Consensus majority.** An attacker controlling sufficient hash power can
  reorganize recent history, censor transactions, or double spend its own
  payments.
- **Implementation correctness.** Consensus, cryptography, wallet, database,
  and networking bugs may cause loss of funds, privacy failures, or chain
  splits. The Monzero-specific changes have not received an independent audit.
- **Endpoint security.** Malware, exposed seeds, weak backups, compromised
  build systems, and malicious remote nodes can defeat protocol protections.
- **Metadata.** Timing, IP observations, exchange records, payment context, and
  repeated user behavior can correlate otherwise private transactions.
- **Anonymity-set quality.** Ring membership is not equivalent to a uniform
  posterior probability. Statistical and temporal information may distinguish
  candidates.
- **Finality.** Proof-of-work settlement is probabilistic. Applications must
  choose confirmation requirements according to value, observed hash rate,
  and reorganization risk.
- **Supply auditing.** Confidential amounts make ordinary transaction values
  private; supply integrity relies on sound commitment and range-proof
  cryptography plus correct verification software.
- **Network maturity.** A small peer and miner population provides weaker
  resilience, censorship resistance, and practical privacy than a large,
  diverse network. In October 2026 most hash rate reaches the chain through
  one public pool, which is a point of concentration until more pools and
  solo miners take part.
- **Clocks.** From block 21,000 a node rejects blocks dated more than 360
  seconds after its own clock. A node with a badly wrong clock can fall
  behind until its clock is corrected.

Users should verify release hashes and signatures when available, keep seeds
offline, use a newly generated Monzero-only wallet, and run a full node where
their threat model warrants it. No protocol can recover a disclosed spend key
or seed.

Binary distribution is a separate security boundary. The release verifier
pre-inspects tar and ZIP members before extraction, rejects links, special
files, path traversal, duplicate paths, excessive entry counts, and oversized
payloads, and only then checks the packaged manifest. These controls reduce
archive-extraction risk; they do not replace authenticated hashes,
maintainer signatures, reproducible builds, or independent review of the
binary itself.

## 11. Governance and development

Monzero has no on-chain governance mechanism. Protocol evolution occurs
through public source changes, review, release artifacts, and voluntary
adoption by node operators, miners, services, and users. This makes social
coordination explicit: maintainers can publish software, but cannot force
independent participants to run it.

Consensus-critical changes should include protected vectors, deterministic
tests, multi-node and reorganization coverage, reproducible packages, and a
documented migration path. Cryptographic changes require specialist review.
Network parameters and release claims should be verifiable from source and
signed artifacts rather than trusted from a website alone.

Release qualification regenerates and replays consensus fixtures against
Monzero's own money supply, decimal precision, block target, and resulting
coinbase denominations. The suite covers emission, integer-overflow rejection,
RingCT, Bulletproofs, Bulletproofs+, CLSAG, pruning races, transaction
propagation, node restart, and longer-chain reorganization. Passing these
tests is evidence about the tested implementation and platform, not a proof of
correctness or a substitute for independent cryptographic review.

Current correctness work also protects two implementation boundaries that are
easy to overlook during toolchain upgrades. Ref10 carry normalization uses
defined integer multiplication rather than signed left shifts, preserving the
same powers-of-two arithmetic without relying on compiler-specific behavior.
Binary wallet serialization converts enumeration values through their explicit
underlying unsigned type while retaining the established varint encoding. A
mined-reward regression constructs a modern view-tagged coinbase output and
proves that the destination wallet recognizes its full amount. These checks
improve portability and reward-accounting confidence; they do not replace an
independent cryptographic review or validate any particular live wallet cache.

The desktop GUI now uses a Monzero-specific `Monzero/wallets` directory for
new wallets. To avoid locking out users upgrading from earlier prereleases,
its wallet picker also discovers wallets in the legacy `Monero/wallets`
directory, and previously saved absolute wallet paths continue to open in
place. The application does not automatically move, rename, or delete wallet
files. This compatibility behavior is especially important when rebuilding a
wallet cache after a reorganization: the keys file must be preserved, and a
new wallet is not required merely because the default directory changed.

The GUI resource bundle uses Monzero-specific application,
title-bar, mining-status, and mined-transfer artwork on Linux, Windows, and
macOS packaging paths. Required upstream copyright notices, research names,
and compatibility identifiers remain unchanged because branding does not
erase provenance. These changes are included in the current GUI package.

## 12. Status and roadmap

Monzero v1.0.0 is the first stable release. It continues the chain used by the
Genesis pre10–pre16 prereleases (the network UUID, genesis nonce and genesis
hash in Section 6) and schedules hard fork 17 at block 21,000. Pre9 used a
superseded identity; its balances and history do not carry into the current
chain. Exact package names, hashes, signatures and limitations are recorded in
the release metadata and notes published at
[monzero.org](https://monzero.org/). Each release's test evidence (unit,
consensus-replay and functional suites, and a live fork test on the private
test network) is evidence about the tested implementation, not an independent
security review.

Public infrastructure in October 2026 comprises two public nodes (Europe and
the United States), a block explorer, a public RandomX mining pool
([pool.monzero.org](https://pool.monzero.org/)), a circulating-supply
endpoint, and a donation page ([monzero.org/fund](https://monzero.org/fund/))
whose balances are read live from the public blockchains of the donated coins.

Immediate work:

1. upgrade every public node and the pool to v1.0.0 well before block 21,000,
   and confirm the fork on mainnet;
2. obtain independent consensus, cryptography and implementation review, and
   independent reproduction of the release binaries;
3. broaden the network: more independent nodes, pools and solo miners;
4. complete the LTC/XMZ exchange's remaining gates (signed operator control,
   a relay for peers on different machines, caps) before a capped beta; and
5. keep the asset research isolated until its separate activation gates are
   satisfied.

The [public roadmap](https://monzero.org/roadmap/) distinguishes implemented
components, test evidence and unfinished gates. No roadmap item is a promise of
delivery or authorization to weaken a release gate.

## 13. References and provenance

Monzero is based on Monero and CryptoNote research and implementation work. In
particular, the design draws on:

1. Nicolas van Saberhagen, *CryptoNote v2.0* (2013).
2. Shen Noether, Adam Mackenzie, and the Monero Research Lab, *Ring
   Confidential Transactions* (2016).
3. Sarang Noether and Brandon Goodell, *Concise Linkable Ring Signatures and
   Forgery Against Adversarial Keys* (2019).
4. Benedikt Bünz et al., *Bulletproofs: Short Proofs for Confidential
   Transactions and More* (2018), and the subsequent Bulletproofs+
   construction used by the inherited implementation.
5. tevador et al., *RandomX: A Proof of Work Algorithm Based on Random Code
   Execution* (2019).
6. Giulia Fanti et al., *Dandelion++: Lightweight Cryptocurrency Networking
   with Formal Anonymity Guarantees* (2018).

Exact Monzero parameters are recorded in
[`MONZERO_CHAIN_SPEC.md`](MONZERO_CHAIN_SPEC.md) and
[`config/monzero-consensus.json`](config/monzero-consensus.json). The asset
research boundary is specified in
[`docs/MONZERO_ASSETS_V1_SPEC.md`](docs/MONZERO_ASSETS_V1_SPEC.md), while
current release evidence and unmet gates are tracked in
[`docs/RELEASE_STATUS.md`](docs/RELEASE_STATUS.md).

Monzero is not affiliated with or endorsed by the Monero Project. Copyright
belongs to the respective CryptoNote, Monero, and Monzero contributors. Source
licensing is described in [`LICENSE`](LICENSE).
