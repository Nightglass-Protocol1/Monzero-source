# Monzero Assets V1 product and privacy specification

Status: pre-activation consensus implementation; independent review required
Native coin: XMZ
Proposed feature: fixed-function private assets

This document records product decisions, implemented consensus rules, and
unresolved review questions. Hard-fork version 17 is intentionally unscheduled;
this document does not authorize activation or claim that the construction is
safe for mainnet.

## 1. Objective

Allow users to create publicly identifiable, fixed-supply assets whose owners,
recipients, balances, and transfer amounts receive privacy protections from the
Monzero protocol.

XMZ remains the only native coin used for network fees, asset-creation fees,
mining rewards, and consensus security.

## 2. V1 scope

V1 is limited to:

- issue a fixed-supply asset;
- transfer an issued asset;
- burn an issued asset;
- scan, restore, and display asset balances;
- voluntary disclosure through explicitly designed view functionality; and
- off-chain metadata committed by an on-chain hash.

V1 deliberately excludes:

- reissuable supply or retained mint authority;
- arbitrary smart contracts;
- transfer taxes, freezes, blacklists, clawbacks, or pausing;
- native automated market makers or order books;
- cross-chain bridges;
- wrapped assets;
- on-chain images or executable metadata; and
- claims that low-activity assets have the same practical anonymity as XMZ.

Fixed supply is mandatory in V1 because it removes an authority lifecycle and
substantially reduces supply-validation and wallet-warning complexity.

## 3. Privacy contract

| Property | V1 policy |
| --- | --- |
| Asset identifier | Public |
| Issuance transaction | Public |
| Initial and maximum supply | Publicly verifiable policy |
| Supply mutability | Fixed; no reissuance |
| Sender | Private under the reviewed transaction model |
| Recipient | Private under the reviewed transaction model |
| Individual balance | Confidential |
| Transfer amount | Confidential |
| Asset activity | Observable at asset level |
| Metadata | Public and untrusted |

The exact meaning of “private” must be replaced by testable cryptographic and
traffic-analysis claims before implementation. A thin asset with few outputs
may have a much weaker practical anonymity set than XMZ.

## 4. Asset identity

- Every asset has a cryptographic asset ID.
- Names, symbols, logos, and websites are never identifiers.
- Duplicate names and symbols are permitted.
- Wallets must show a shortened asset ID anywhere confusion is possible and
  provide the complete ID on demand.
- The asset-ID derivation domain must include the Monzero mainnet identity and
  an issuance-specific commitment.

The exact derivation remains a cryptographic design item. It must avoid circular
transaction-hash definitions, collisions, cross-network replay, and malleability.

### 4.1 Inactive descriptor prototype

The source tree contains a research-only issuance descriptor and asset-ID
derivation in `src/cryptonote_basic/asset_types.*`. It is deliberately absent
from transaction serialization, the mempool, blocks, RPC, and wallets. Public
nodes therefore continue to reject every transaction version above v2 and
cannot issue or transfer assets.

The provisional V2 canonical byte sequence is:

1. ASCII domain `MonzeroAssetIssuanceV2` without a terminator;
2. one-byte descriptor version;
3. 16 raw network-UUID bytes;
4. one-byte asset class;
5. 32-byte issuer public key;
6. 32-byte issuance nonce;
7. eight-byte unsigned atomic supply, little-endian;
8. one-byte display precision;
9. 32-byte metadata content hash;
10. 32-byte collection asset ID, or the zero hash;
11. two-byte metadata-reference length, little-endian; and
12. the metadata-reference bytes.

The provisional asset ID is `cn_fast_hash(canonical_descriptor)`. Version 2
requires a valid issuer public key, a public Monzero network, non-zero supply,
display precision from 0 through 11, no NUL in the metadata reference, and a
maximum metadata-reference length of 256 bytes. Consensus assigns no meaning
to the reference contents.

The asset classes and their additional rules are:

| Class | Supply | Decimals | Collection reference |
| --- | ---: | ---: | --- |
| Fungible | Greater than zero | 0–11 | Forbidden |
| Non-fungible | Exactly 1 | 0 | Optional |
| Collection | Exactly 1 | 0 | Forbidden |
| Edition | Greater than zero | 0 | Optional |

An NFT or edition must commit to a non-zero metadata content hash. A collection
reference alone does not prove legitimate membership. The inactive prototype
therefore defines two domain-separated authorization messages:

- `MonzeroAssetIssuanceAuthorizationV1 || canonical_descriptor`, signed by the
  issuer key committed in the descriptor; and
- `MonzeroCollectionMembershipV1 || collection_id || member_asset_id`, signed
  by the collection controller.

The implementation verifies both signatures and rejects altered descriptors,
member IDs, invalid keys, and zero IDs. The future transaction and database
design must still define how a collection controller is resolved from its
on-chain issuance, whether it can rotate, and how it is permanently closed.
Until that complete path exists, wallets must not display a collection as
verified.

The implementation includes a strict decoder for this canonical descriptor. It
rejects truncated inputs at every byte boundary, unknown network UUIDs,
unsupported versions and classes, oversized references, embedded NUL bytes,
trailing bytes, mismatched lengths, and any encoding that does not reproduce
the canonical byte sequence exactly. The descriptor is carried inside the
versioned native transaction envelope described below.

An issuance-payload envelope binds the descriptor to its
issuer authorization and, when collection membership is claimed, requires an
explicit collection authorization slot. Its decoder is length-bounded,
canonical, verifies the issuer signature, rejects every truncated prefix,
rejects invalid signature flags and trailing bytes, and prevents collection
signatures from being silently omitted or attached to unrelated issuance.
Collection-controller verification resolves the collection from persistent
consensus state before accepting a member issuance.

The versioned transaction extension is
bound to one public network and a non-zero commitment to the carrier native
transaction prefix, then contains exactly one bounded operation payload.
Version 1 recognizes issuance only. Its canonical parser rejects unknown
operations, network disagreement, a missing carrier commitment, malformed
lengths, truncated prefixes, invalid nested signatures, and trailing data.
Changing the carrier commitment changes the extension ID. The extension has a
native `tx_extra` envelope with tag `0x7a`, but nodes reject that envelope below
hard-fork version 17. Version 17 is deliberately not present in any network's
hard-fork schedule, so the envelope remains inactive on every network.

The block adapter receives independently computed native
prefix commitments and validates each extension against its corresponding
carrier before applying any issuance. Count, network, or commitment mismatch
rejects the complete batch without changing registry state. This separates the
future native transaction parser from asset operation validation and gives
tests an explicit boundary for carrier-binding failures.

### 4.2 Inactive registry and reorganisation model

The implementation uses the same validated registry model when rebuilding
state from persistent LMDB issuance records. An issuance is inserted only after its canonical ID and
issuer signature validate. A claimed collection must already exist on the same
network, have the collection class, precede the member issuance, and authorize
the exact member asset ID. Duplicate IDs and unexpected membership signatures
are rejected.

The registry records issuance height and deterministically removes every asset
issued at or above a detached height. Tests demonstrate removal and subsequent
valid reissuance after a simulated reorganisation. This is an executable state
model, not the final database schema or consensus integration.

The inactive registry also has a deterministic, network-separated snapshot
format. Records retain issuer and optional collection signatures, are ordered
by height with collections before same-height members, and are fully
revalidated into temporary state before replacing the active registry.
Corruption, cross-network input, noncanonical order, duplicate issuance,
trailing data, and invalid signatures fail without modifying existing state.
This provides a recovery and migration model in addition to the authoritative
LMDB issuance, output, key-image, and height indexes.

The registry's inactive block adapter applies issuance payloads in transaction
order against temporary state and commits only if every issuance succeeds.
This makes same-block collection membership order explicit and prevents a
partially valid block from leaving partial asset state. Detaching at the block
height removes all of that block's issuances. A domain-separated hash of the
canonical snapshot provides a deterministic integrity commitment for testing
and future database migration checks; it is not currently committed in block
headers.

This encoding is a stable prototype vector for review, not an activation
decision. A cryptographic review may replace the hash construction or fields;
doing so must deliberately update the golden vector before any testnet fork.

## 5. Issuance policy

An issuance declares:

- asset ID construction data;
- atomic supply;
- decimal display precision;
- metadata content hash;
- metadata URI;
- protocol version; and
- an XMZ creation fee.

Issuance creates the complete lifetime supply. No key or authority can create
additional units after confirmation.

Limits requiring research include maximum atomic supply, decimal range,
metadata URI length, transaction size, and creation-fee amount.

## 6. Transfers and burns

Nodes must reject a transaction unless it proves, for each asset independently,
that valid inputs equal valid outputs plus an explicitly burned amount.

XMZ fees form a separate balance domain and may never be paid with an issued
asset. An asset transaction therefore needs sufficient XMZ inputs without
allowing the asset proof to create, destroy, or transform XMZ accidentally.

A burn is permanent and must be distinguishable from an ordinary confidential
output without revealing unrelated private amounts.

### 6.1 Inactive transparent conservation model

`asset_types.*` also contains a test-only, transparent balance statement. It
exists to make the intended accounting rules executable before any
confidential commitment or proof system is selected. It is not serialized,
accepted by the mempool, exposed over RPC, or usable by a wallet.

The validator currently enforces these semantic invariants:

- XMZ inputs equal XMZ outputs plus an XMZ-denominated fee;
- issuance creates exactly the descriptor's complete fixed supply;
- an asset ID may be issued only once;
- every non-issuance asset ID must already be known;
- each asset balances independently as inputs equal outputs plus explicit
  burns, preventing conversion between asset IDs;
- zero IDs, zero-valued entries, and empty statements are noncanonical; and
- all aggregation and output-plus-fee calculations reject integer overflow.

These checks do not provide confidentiality, ownership authorization, range
proofs, double-spend protection, or a consensus transaction format. Those
properties remain prerequisites for an isolated prototype network. The
transparent statement must never be mistaken for a production transaction
design.

### 6.2 Inactive confidential conservation prototype

The source tree now contains an inactive per-asset Pedersen-commitment and
Bulletproof+ verifier. It reuses the inherited RingCT curve and range-proof
implementation, requires exact proof coverage of every output and explicit
burn commitment, and checks that pseudo inputs equal outputs plus burns inside
each public asset-ID domain. Fixed-supply issuance uses a zero-mask commitment
to the descriptor's public lifetime supply. Limits currently cap each balance
group at 16 pseudo inputs and 16 destinations.

Adversarial tests reject inflation, substituted commitments, malformed range
proofs, duplicate asset groups, unknown assets, cross-asset pseudo inputs, and
issuance commitments that disagree with the declared supply. Malformed curve
proofs are converted to deterministic validation failure rather than escaping
as exceptions.

The next inactive layer adds domain-separated CLSAG ownership proofs. A proof
commits to the network UUID, carrier transaction, asset ID, pseudo input, and
all ring output IDs, destination keys, and amount commitments. Rings contain
exactly 16 members. Verification rejects network/carrier replay, cross-asset
members, duplicate or zero output IDs, malformed points, key-image tampering,
and any pseudo input without exactly one matching proof. Key images must also
be unique inside one transaction.

The inactive database prototype now persists authoritative asset outputs and
spent asset key images in separate LMDB indexes. Ring claims can be resolved
against those records before CLSAG verification, and block detach removes
outputs and key images at or above the detached height. Restart, transaction
abort, duplicate-key-image, and chain-pop tests cover this storage layer.

HF17 block validation resolves ownership proofs against authoritative outputs,
rejects spent key images, and applies issuance, outputs, and key images in the
same LMDB batch as the native block. Mempool admission performs the same
read-only validation and rejects pending duplicate issuance IDs or asset key
images. The fork remains unscheduled, so these rules cannot make an asset
transaction valid on any currently configured Monzero network.

### 6.3 Inactive canonical transaction payload

The source tree contains a version-2 canonical binary payload and a native
transaction-envelope parser. The envelope uses `tx_extra` tag `0x7a`, is capped
at 256 KiB, must occur exactly once, and is rejected before hard-fork version
17. Its byte order and field order are fixed as follows:

1. one-byte payload version and one-byte network type;
2. 32-byte carrier-prefix hash;
3. one-byte issuance-present flag, followed when set by a two-byte
   little-endian issuance length and the canonical authenticated issuance;
4. one-byte balance-group count, then for each group: asset ID, counted pseudo
   inputs, counted destination-key/commitment pairs, counted burn commitments,
   and counted Bulletproof+ objects;
5. one-byte ownership-proof count, then each asset ID, pseudo commitment, key
   image, exactly 16 ring members, and the canonical CLSAG fields (`c1`, `D`,
   and exactly 16 responses). The redundant CLSAG `I` field is reconstructed
   from the separately encoded key image and is not serialized.

All integer lengths and output indexes use explicit little-endian encoding;
all point, hash, UUID, and signature values use their fixed byte arrays. The
decoder rejects truncation, trailing bytes, unsupported versions, invalid
flags, noncanonical ring sizes, and counts over the per-group and aggregate
limits. The payload is capped at 256 KiB, eight asset groups, 64 total inputs,
64 total destinations, and 64 ownership proofs. A fixed 589-byte test vector
has canonical fast-hash
`130a808631ad221e009ec3b22e4ef00eb20e8afec48a41b820251962867d1f2b`.

Wire version 2 replaces the earlier inactive v1 recipient-opening semantics.
Asset builders must coordinate commitment masks so inputs equal outputs plus
burns. The inherited compact RingCT ECDH form discards a supplied mask and
derives another, making a constructible conserved issuance impossible. V2
therefore encrypts and carries the builder-selected reduced mask, and v1 is
rejected rather than reinterpreted.

Output identities are derived from a domain label, network UUID, carrier hash,
asset ID, global output index, destination key, and commitment. State
application validates the entire payload, resolves every ring member, checks
collection authority and collisions, then atomically writes issuance records,
spent key images, and outputs. The carrier is the fast hash of a transaction
prefix reconstructed from the parsed native prefix after removing the asset
envelope and canonically reserializing every remaining `tx_extra` field in its
original semantic order. Malformed or duplicate envelopes invalidate carrier
derivation. Public paginated RPC methods expose the authenticated registry and
output set for wallet restoration and explorer indexing; wallet transaction
construction and independent cryptographic review remain activation
prerequisites.

Each persistent asset output also carries the per-output transaction public
key, one-time destination, view tag, encrypted amount opening, commitment, and
global asset-output index. Standard addresses publish `rG`; subaddresses
publish `rD`, matching the established one-time-address construction. A
view-only wallet can reject unrelated outputs by view tag, derive the expected
destination, decrypt the amount and builder-selected commitment mask, and accept
it only when the reconstructed commitment matches consensus state. These
fields are included in the canonical transaction envelope and paginated output
restoration RPC.

## 7. Metadata

Consensus stores only bounded identity and commitment fields. Descriptions,
logos, project links, and social information remain off-chain.

Consensus must never depend on a registry, gateway, domain, image host, or
website. Wallets must treat all metadata as untrusted input and enforce content
type, size, rendering, and scripting restrictions.

A registry may label an asset as verified, but verification must be visibly
different from endorsement.

## 8. Wallet requirements

- Discover every supported asset during normal scanning.
- Restore all XMZ and asset balances from documented recovery material.
- Display asset ID, fixed-supply status, and metadata verification state.
- Prevent name, symbol, decimal, and logo spoofing from obscuring the asset ID.
- Build transactions that balance XMZ fees and asset values independently.
- Explain thin-asset anonymity limitations before first use.
- Support opt-in disclosure without silently broadening an existing view key.
- Refuse unknown asset transaction versions safely.

Hardware wallets are unsupported until their transaction review and signing
flows explicitly understand asset IDs, supply, fees, and burns.

## 9. Required cryptographic research

An external review must cover:

- per-asset commitment construction;
- prevention of cross-asset inflation;
- issuance and burn proofs;
- interaction with RingCT and existing output types;
- ring-member compatibility and selection;
- thin-asset anonymity sets;
- range proofs and balance proofs;
- XMZ fee separation;
- transaction malleability;
- scanning cost and false positives;
- verification time and transaction size; and
- malformed or adversarial asset transactions.

Public asset IDs with confidential amounts are the preferred V1 research path.
Confidential asset IDs are deferred to a possible later protocol version.

## 10. Activation policy

Asset rules require a versioned hard fork after:

1. completion of the Phase 0 stabilization gates;
2. publication of a complete binary consensus specification;
3. independent cryptographic and implementation review;
4. an isolated prototype network;
5. automated inflation and reorganisation tests;
6. an extended public testnet;
7. resolution of audit findings; and
8. advance coordination with node, miner, wallet, and service operators.

Unknown asset transaction versions must be rejected. Mainnet must not contain a
privileged rollback, administrator mint, or emergency asset-editing key.

## 11. Deferred systems

Launchpad, trading, and bridging are separate projects. Their existence is not
required for consensus-level asset ownership or transfer.

If a bridge is later considered, its first scope should be XMZ to wrapped XMZ,
with explicit custody assumptions, strict caps, independent audits, reserve
monitoring, and warnings that transparent-chain activity can correlate users.

## 12. Open decisions

- Formal asset-ID derivation
- Commitment and proof construction
- Decoy eligibility and ring selection
- Maximum supply and decimal constraints
- Asset-creation fee and anti-spam policy
- Burn representation and supply reporting
- Asset-specific disclosure design
- Transaction weight limits
- Database schema and migration plan
- Reorganisation semantics for metadata indexes
- Testnet identifiers and activation schedule
