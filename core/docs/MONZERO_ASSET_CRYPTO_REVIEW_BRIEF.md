# Monzero Assets V1 cryptographic review brief

Status: pre-implementation review input
Network activation: none
Native coin: XMZ
Descriptor prototype: version 2

## Review objective

Determine whether Monzero can safely support fixed-supply fungible assets,
NFTs, collections, and editions with public asset IDs, confidential amounts,
private recipients, and XMZ-only fees without weakening existing XMZ
transaction validation.

No asset transaction is currently serialized or accepted. The existing source
contains only canonical identity, authorization, transparent conservation, and
in-memory reorganisation reference models.

## Preferred V1 construction for review

Each asset output exposes a public 32-byte asset ID and otherwise uses the
existing one-time destination and confidential amount machinery. The asset ID
must be committed by the transaction prefix hash and every applicable
ownership/balance signature so it cannot be substituted after signing.

Amounts continue to use the existing commitment generator. Validation groups
commitments by public asset ID and verifies a separate balance equation for
each group. XMZ forms its own group and is the only group from which the
transaction fee may be subtracted.

This intentionally does not attempt confidential asset IDs. It minimizes new
cryptographic machinery, but outputs and activity are linkable at asset-class
level and rings must not mix assets in a way that permits an asset-substitution
proof.

## Required transaction operations

### Issuance

- Commit the complete canonical descriptor and its derived asset ID.
- Verify authorization by the descriptor issuer key.
- Create exactly the declared lifetime supply.
- Reject an asset ID already present in canonical chain state.
- Charge a consensus-defined XMZ creation fee.
- For a claimed collection, verify authorization by the controller recorded in
  the earlier collection issuance.

### Transfer

- Prove ownership of every real input without exposing which ring member it is.
- Bind each input and output to a public asset ID.
- Prove confidential input amounts equal confidential output amounts plus any
  explicit burn for each asset independently.
- Pay the network fee only from the XMZ balance equation.

### Burn

- Permanently remove units without creating a spendable output.
- Bind the burn to the asset ID and transaction signature.
- Decide during review whether burn amounts are public or confidential. Public
  burns simplify circulating-supply accounting; confidential burns protect
  amounts but complicate public supply reporting.

## NFT and collection rules

- NFT and collection descriptors have supply one and zero decimals.
- Editions are indivisible and have supply greater than zero.
- Metadata is off-chain and untrusted; consensus commits only its content hash
  and bounded reference bytes.
- Collection membership signs
  `MonzeroCollectionMembershipV1 || collection_id || member_asset_id`.
- Collection controller rotation and permanent closure are deliberately
  unresolved. V1 should prefer immutable controllers or an explicit signed
  closure operation over an administrator override.
- Royalties are metadata hints only and are not consensus-enforced.

## Questions requiring an external answer

1. Can existing RingCT/CLSAG and Bulletproofs+ components be safely reused when
   balance equations are partitioned by a public asset ID?
2. Must all decoys in an input ring share the real input's public asset ID, and
   how should wallets sample rings for thin assets?
3. Which exact transcript elements must include the asset ID to prevent
   substitution, replay, or cross-asset inflation?
4. What issuance pseudo-input or equivalent proof safely establishes the
   declared fixed supply without revealing initial recipient amounts?
5. How should explicit burns be represented and proven?
6. Can batching proofs across asset groups introduce cancellation between
   groups or malicious generator relationships?
7. What bounds are required for asset groups, inputs, outputs, metadata, and
   verification cost per transaction and block?
8. Does public asset grouping weaken XMZ privacy when XMZ and issued assets are
   moved together?
9. Which changes are required in hardware-wallet transaction review?
10. Are there safer established constructions or maintained libraries that
    should replace this proposal?

## Threat model and mandatory negative tests

- Inflation within one asset.
- Cancellation or conversion between two asset IDs.
- Paying XMZ fees with an issued asset.
- Duplicate issuance across the main chain and alternate branches.
- Descriptor, asset-ID, collection-ID, or metadata-hash substitution.
- Forged issuer or collection-controller authorization.
- Replay across mainnet, testnet, and stagenet.
- Duplicate key images and repeated inputs.
- Malformed points, torsion points, noncanonical scalars, and zero commitments.
- Integer and parser length overflow.
- Proof batching failures that pass individually invalid transactions.
- Reorganisation rollback leaving phantom assets or deleting canonical assets.
- Denial of service from many asset groups or pathological proof shapes.
- Wallet restoration missing assets, NFTs, burns, or reorganised history.

## Activation requirements

1. Written review of the construction and transcript.
2. A complete byte-level transaction specification and golden vectors.
3. Strict parsing and verification implemented behind a test-only feature gate.
4. Persistent state with atomic connect/detach behavior.
5. Inflation, malformed-proof, fuzz, and multi-node reorg tests.
6. Wallet restoration and hardware-wallet impact assessment.
7. A disposable isolated network followed by a resettable public testnet.
8. Independent implementation audit and remediation.
9. A separately coordinated hard fork; no silent mainnet activation.

## Explicit non-goals

- Smart contracts or arbitrary programs.
- Bridges, wrapped assets, exchanges, or AMMs.
- Confidential asset identifiers.
- Reissuance, freezing, clawback, taxes, or administrator minting.
- A claim that low-activity NFTs have XMZ-sized anonymity sets.
