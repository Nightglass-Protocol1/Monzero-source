# Monzero Phase 0 stabilization plan

Status: active engineering gate
Baseline date: 2026-08-15
Native coin ticker: XMZ

This plan must be completed before any Monzero Assets consensus rules are
activated on a public network. Research, specification, and isolated prototypes
may proceed, but asset transaction formats must not be accepted by mainnet.

## Current baseline

- The independent mainnet identity, genesis, address prefixes, ports, emission,
  precision, and hard-fork schedule are recorded in `MONZERO_CHAIN_SPEC.md`.
- Linux command-line and Windows GUI/CLI prerelease packages exist, but the
  release process is not yet reproducible or signed.
- The official public node is `node.monzero.org:6174`, with restricted RPC on
  port 6175.
- Only one official publicly reachable bootstrap node is currently established.
- Wallet creation, mining, synchronization, transfers, the public explorer, and
  website have received manual testing.
- A dedicated regtest-only HF17 fixture covers NFT issuance, discovery,
  confidential transfer, burn, seed restoration, and reorganisation rollback;
  public-network hard-fork schedules remain unchanged.
- The inherited release checklist and portions of user-facing utility text still
  refer to Monero infrastructure or terminology.
- The working tree contains substantial uncommitted fork work. A full pre-assets
  backup was created before beginning this phase.

## Gate A: source and consensus baseline

- [ ] Commit the current fork as a named, reviewable baseline.
- [x] Record the exact genesis transaction, nonce, hash, and network UUID.
- [x] Record mainnet, testnet, and stagenet address prefixes and ports.
- [x] Generate a machine-readable consensus-parameter manifest.
- [x] Add tests which fail if protected network constants change unexpectedly.
- [ ] Document every intentional divergence from the upstream Monero version.
- [ ] Separate compatibility identifiers that must remain unchanged from
      user-facing branding that should say Monzero.
- [ ] Define the supported database migration and rollback policy.

Acceptance: two clean source checkouts produce nodes that agree on genesis,
hard-fork versions, block rewards, transaction validity, and block hashes.

## Gate B: network reliability

- [ ] Operate at least three independently administered public seed nodes.
- [ ] Place seed nodes in at least two providers and two geographic regions.
- [ ] Confirm a fresh node can bootstrap when any one seed is unavailable.
- [ ] Confirm a seed never attempts to use itself as its bootstrap peer.
- [ ] Test inbound, outbound, IPv4, DNS failure, restart, and peer-cache recovery.
- [ ] Monitor height, peer count, fork divergence, disk, memory, and RPC health.
- [ ] Define an incident response and emergency release procedure.

Acceptance: a new node with an empty data directory synchronizes without manual
peer intervention, and the network continues progressing with one seed offline.

## Gate C: wallet and transaction correctness

- [ ] Create, restore, and rescan wallets from seed at multiple restore heights.
- [ ] Test incoming, outgoing, pending, failed, and replaced transactions.
- [ ] Test coinbase maturity and normal output unlock rules.
- [ ] Test view-only wallets, subaddresses, multisig, and offline signing, or
      explicitly declare unsupported features before launch.
- [ ] Verify CLI and GUI display XMZ consistently.
- [ ] Verify no user-facing command suggests reusing Monero keys.
- [ ] Test wallet recovery after unclean shutdown and daemon reorganisation.

Acceptance: an independently restored wallet reconstructs the same balance and
transaction history as the original wallet.

## Gate D: automated consensus testing

- [x] Unit tests for emission, precision, parsing, address prefixes, and fees.
- [x] Golden vectors for genesis and representative transactions.
- [x] Multi-node integration tests for synchronization and relay.
- [ ] Reorganisation tests across every active hard-fork version.
- [x] Network partition and recovery tests.
- [ ] Transaction and block parser fuzzing.
- [x] Invalid inflation, overflow, duplicate-spend, and malformed-proof tests.
- [ ] Continuous integration for supported Linux and Windows targets.

Acceptance: all protected tests pass from a clean build and intentional
consensus changes require explicit vector updates.

## Gate E: release engineering

- [x] Replace `docs/RELEASE_CHECKLIST.md` with a Monzero-specific checklist.
- [ ] Produce reproducible Linux builds from a pinned environment.
- [ ] Produce reproducible Windows builds from a pinned environment.
- [ ] Publish source commit, build recipe, file sizes, and SHA-256 hashes.
- [ ] Establish an offline release-signing key and publish its fingerprint.
- [ ] Sign release manifests; pursue Windows code signing separately.
- [ ] Test archives on clean supported operating systems.
- [ ] Document upgrade, downgrade, uninstall, and data-directory behaviour.

Progress note: deterministic Linux archive construction and verification are
implemented and produce byte-identical archives from identical inputs. The
current developer binaries are dynamic, unstripped, and not independently
reproduced, so strict release verification correctly rejects them.

Acceptance: two independent builders produce byte-identical binaries or a
documented, investigated explanation for every difference.

## Gate F: public documentation and disclosure

- [ ] Publish the private-bootstrap height range and total mined allocation.
- [ ] Publish team-controlled addresses or a privacy-compatible accountability
      mechanism agreed before public launch.
- [ ] State clearly that Monzero is independent from the Monero project.
- [ ] Publish supported platforms, known limitations, and audit status.
- [x] Create security contact and responsible-disclosure instructions.
- [ ] Remove inherited links that direct users to unrelated Monero services.

Acceptance: a new operator can verify, install, run, back up, restore, and
upgrade a node or wallet using only public Monzero documentation.

## Asset-development hold point

Mainnet asset implementation remains blocked until Gates A through E are
complete. The Monzero Assets specification may advance in parallel, and an
isolated disposable development network may be used after cryptographic review.

## Inactive asset/NFT prototype progress

- [x] Canonical network-separated fixed-supply asset IDs.
- [x] Explicit fungible, NFT, collection, and edition classes.
- [x] NFT/collection supply-one and zero-decimal invariants.
- [x] Independent XMZ fee and per-asset conservation semantics.
- [x] Domain-separated issuer authorization signatures.
- [x] Domain-separated collection-membership authorization signatures.
- [x] Inactive authenticated issuance registry with deterministic reorg rollback.
- [x] Strict canonical issuance-descriptor decoder with byte-boundary truncation tests.
- [x] Bounded authenticated issuance-payload envelope with strict signature shape rules.
- [x] Fail-closed signed issuance constructor with fresh nonce generation and collection authority checks.
- [x] Network-bound software-wallet issuance authorization with unsupported key modes rejected.
- [x] Deterministic authenticated registry snapshots with atomic restore failure behavior.
- [x] Atomic ordered block-issuance adapter, detach behavior, and deterministic state commitments.
- [x] Detached versioned transaction extension bound to a native prefix commitment.
- [x] Atomic block-extension validation against independently supplied native carrier commitments.
- [x] Persistent LMDB issuance registry with restart reconstruction and native reorg rollback.
- [x] Inactive per-asset Pedersen/Bulletproof+ conservation and burn verifier.
- [x] Inactive network/carrier/asset-bound CLSAG ownership proof verifier.
- [x] Recipient-decodable fixed-supply issuance payload constructor with coordinated commitment masks.
- [x] Pre-signing attachment of a constructed issuance envelope to a native transaction prefix.
- [x] Activation-gated software-wallet and CLI construction of one signed native issuance transaction.
- [x] Inactive v2 transaction serialization with strict parser limits and obsolete-v1 rejection.
- [x] External cryptographic-review brief and mandatory threat cases.
- [ ] Reviewed confidential per-asset commitment and range-proof construction.
- [ ] Production review of transaction serialization, database indexes, and reorganisation-safe state.
- [ ] Isolated asset development network and faucet.
- [ ] Wallet asset workflow: confirmed owned-output scanning and reorg/cache
  handling are implemented; complete spend tracking, restoration fixtures,
  transfer, burn, and metadata UI.
- [x] Explorer registry/detail display with inactive-state and metadata/collection trust distinctions.

All completed items in this section are inactive primitives and unit tests.
They do not enable token or NFT issuance on mainnet, testnet, or stagenet.
