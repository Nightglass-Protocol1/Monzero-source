# DEX settlement protocol compatibility gate

Status: research and test gate. No protocol reviewed here is authorized for live
fund custody, signing, transaction broadcast, or mainnet trading.

## Decision

The first atomic-settlement target is BTC/XMZ in the isolated Bitcoin-regtest and
Monzero-fakechain lab. COMIT's XMR/BTC swap and Farcaster are useful protocol and
state-machine references, but neither is a drop-in Monzero adapter. Importing one
unchanged, relabelling XMR as XMZ, or enabling its mainnet path is prohibited.

External/external pairs and a mandatory XMZ success fee require a new reviewed
protocol construction. Three independent transfers are not atomic and must never
be presented as atomic settlement.

## Pinned research inputs

- Monzero source under `monero/`, currently commit
  `a99f85b357ee687b524bb06ea3cc933b8047a4a0`.
- COMIT `xmr-btc-swap`, commit
  `dc6ba84bbb1fe5ecc69581fec7dd8529567c4e32`. Its README marks the repository
  unmaintained, warns that the cryptography was not formally audited, and states
  that its client supports sending BTC to receive XMR.
  The initial verifier wire profile additionally pins its lockfile dependencies
  `ecdsa_fun` 0.10.0, `sigma_fun` 0.7.0 and `bincode` 1.3.3. Its encrypted ECDSA
  signature serializes to exactly 162 bytes; the cross-curve proof is a transcript-
  dependent structure with 252 paired commitments, not an interchangeable blob.
- Farcaster node, commit
  `48dad68fd432d3b54cf5a60a96a2f3448bfa4f6a`. Its README describes an evolving
  Bitcoin/Monero system and warns that mainnet use is at the user's own risk.
- COMIT's referenced protocol paper: arXiv 2101.12332, section 3.

Any later evaluation must pin the replacement source commit and repeat every gate
below. A passing upstream audit does not establish Monzero compatibility.

## Known incompatibilities

1. **Atomic denomination:** Monzero defines
   `CRYPTONOTE_DISPLAY_DECIMAL_POINT` as 11. COMIT defines one XMR as
   `1,000,000,000,000` atomic units (12 decimals). Reusing its conversion would
   overstate an XMZ amount by a factor of ten.
2. **Network identity:** Monzero mainnet uses address prefix 86 and its own genesis
   transaction/nonce. Testnet and stagenet have distinct prefixes and genesis
   settings. A generic `Monero::Mainnet` selector is not adequate evidence that an
   RPC endpoint or address belongs to Monzero.
3. **Protocol shape:** COMIT uses a two-chain BTC/XMR protocol with cross-curve
   secp256k1/ed25519 DLEQ proofs, ECDSA adaptor signatures, and Bitcoin relative
   CSV cancel/punish timelocks. These primitives and transaction assumptions must
   be checked against the exact Monzero fork, wallet RPC, and current Bitcoin
   policy before reuse.
   Its pinned shared Bitcoin output is the ordered Miniscript descriptor
   `c:and_v(v:pk(A),pk_k(B))`, not a hashlock script. Monzero now has a read-only
  byte/curve/P2WSH validator for this shape and a BIP143 `SIGHASH_ALL` calculator
  matching the published native-P2WPKH vector. Its ordinary ECDSA signature is
  also verified under strict-DER, compressed-key and low-S policy. Adaptor
  signature handling and the wider signing protocol remain unimplemented and
  unaudited.
4. **Fee semantics:** the reference two-chain protocol does not enforce our XMZ
   success fee, including for BTC/XMZ acquisition. Recovered full spend-key control
   does not constrain the subsequent spend to the quoted fee output. For an
   external/external trade, a separately broadcast XMZ payment can succeed
   while the trade fails, or be withheld after the trade succeeds; therefore it
   does not satisfy the signed V6 success-only fee requirement.
5. **Maintenance and assurance:** neither inspected implementation supplies the
   independent Monzero-specific security review required for production.

## Required BTC/XMZ compatibility evidence

All items must pass with reproducible vectors and persisted recovery state before
the `executable` guard may even be reconsidered:

- exact 11-decimal XMZ parsing and formatting, including boundary, overflow, and
  rejection tests that detect accidental 12-decimal conversion;
- mainnet/testnet/stagenet/fakechain identity checks using genesis, address prefix,
  chain height semantics, and transaction rules rather than a name alone;
- ed25519 spend/view-key aggregation and cross-curve DLEQ vectors generated and
  independently verified by both counterparties;
- Bitcoin adaptor-signature vectors, script bytes, transaction IDs, sighashes,
  CSV sequence interpretation, standardness, and fee-bump behavior;
- Monzero lock output construction, wallet detection, spendability/unlock rules,
  destination proof, exact atomic value, and key recovery on the current fork;
- byte-exact commitments for every lock, redeem, cancel, punish, refund, and fee
  object, plus both parties' adaptor/Monzero public keys and DLEQ proof hashes,
  bound by `SettlementTerms` V6 before either party signs;
- conservative confirmation and deadline policy tested across reorgs, delayed
  blocks, fee spikes, restarts, clock changes, RPC loss, and either party going
  offline at every state transition;
- an append-only settlement journal fsynced before every signature or broadcast,
  with idempotent recovery and no state inferred solely from the GUI;
- adversarial fakechain/regtest tests, then public test networks, independent
  protocol/cryptography review, and an explicitly authorized limited release.

## XMZ fee constructions to evaluate

For BTC-to-XMZ acquisition, the most promising design is a single Monzero success
transaction whose committed outputs pay both the buyer's net XMZ and the disclosed
fee recipient. The sender-wallet/block evidence boundary verifies that a supplied
transaction did pay both outputs; it does **not** force that transaction to be
chosen by someone controlling the spend key. Thus it is observation, not
cryptographic fee enforcement. See [the fee-bypass analysis](DEX_FEE_ENFORCEMENT.md).
Safe
transaction construction, adaptor-secret release and all refund paths still need
to be proved and tested before this design is executable.

For an external/external pair, an XMZ fee leg adds a third chain. A protocol must
cryptographically make the trade and fee succeed or refund under one coherent
state machine. Alternatively, route the user through two clearly disclosed
BTC/XMZ-style swaps; that route has sequential price, liquidity, and partial-
completion risk and must not be labelled a single atomic trade.

## Activation rule

Keep `executable=false` and all signing/broadcast paths absent until every relevant
gate is backed by tests and an independent review. UI availability, a valid quote,
confirmed fixture transactions, or sufficient reserved balance is not activation
authority.
