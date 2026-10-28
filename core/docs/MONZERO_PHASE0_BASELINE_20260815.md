# Monzero Phase 0 Baseline — 2026-08-15

This report records the state recovered after the development machine restart. It is a baseline, not a release approval.

## Recovery and backup

- Source commit: `4f92268d7c16741cfb41e5bbe2aa46cc260a9ea5`
- Working tree: 56 changed or untracked paths at the end of this baseline run. These include pre-existing project work and the Phase 0 documents; nothing was discarded or reset.
- Full pre-change archive: `/home/pinhead/Projects/Monzero-Fork-backups/Monero-Fork-before-assets-phase0-20260815.tar.zst`
- SHA-256: `40f9d5f4ba04cfd948dd6356cdb648253966ce3186ce8ac9b86af75010c46fb1`
- Archive integrity: passed `zstd -t`.

## Build baseline

- Target: `unit_tests`
- Build command: `cmake --build build --target unit_tests -j2`
- Result: passed; the current source produced `build/tests/unit_tests/unit_tests`.
- Parallelism was deliberately limited to two jobs after the earlier machine crash.

## Unit-test baseline

The binary discovered 1,231 tests from 152 test cases. The run progressed through the test inventory but did not produce a clean final result, so Phase 0 remains open.

### Confirmed failure: DNSSEC-dependent URL test

`AddressFromURL.Failure` fails because DNSSEC validation returns false through the configured local resolver. A focused rerun reproduced the failure:

```text
Value of: dnssec_result
  Actual: false
Expected: true
```

The resolver reported that no signatures were available while building the DNSSEC chain of trust. This may be environment-dependent, but it must be isolated or made deterministic before it can be accepted in release CI.

### Confirmed incomplete test: protocol-handler race test

`cryptonote_protocol_handler.race_condition` remained blocked in a futex wait for more than ten minutes. The test runner was then interrupted with SIGINT; no daemon or wallet process was terminated. This requires a focused, time-bounded reproduction and diagnosis.

Because the run was interrupted, no pass count is claimed. Passing output was observed across substantial wallet/crypto utility, mnemonic, LMDB, networking, serialization, HTTP, SOCKS, ZMQ, and node-server groups, but that is not a substitute for a complete suite result.

## Local network observation

The already-running local daemon on RPC port 16175 was left untouched and reported:

```json
{
  "height": 807,
  "target_height": 0,
  "incoming_connections_count": 0,
  "outgoing_connections_count": 0,
  "difficulty": 167813,
  "synchronized": true,
  "status": "OK"
}
```

The daemon considers its local chain synchronized, but zero peer connections means this observation does not demonstrate live network convergence or propagation.

## Phase 0 disposition

Asset consensus implementation remains on hold. The next engineering actions are:

1. Reproduce and diagnose the protocol-handler race-test stall under a strict timeout.
2. Replace external DNS assumptions in unit tests with deterministic fixtures, or document and enforce a validated CI resolver.
3. Establish a multi-node automated chain test covering peer discovery, block propagation, reorg handling, wallet refresh, and transaction relay.
4. Produce reproducible, checksummed Linux and Windows release artifacts from a clean source checkout.

Only after the stabilization gates in `MONZERO_PHASE0_STABILIZATION.md` are satisfied should the asset design move from research specification to consensus implementation.

## Follow-up remediation

The fast gate is now deterministic and clean:

- `tests/phase0/run-fast-unit-tests.sh build` passed all 1,230 selected tests from 151 test cases in 174.317 seconds.
- DNSSEC-dependent tests use an explicitly configured validating TCP resolver (`DNS_PUBLIC`, defaulting to `tcp://1.1.1.1`).
- Fork-specific address, emission, amount-precision, wallet-cache, serialization, multisig, URI, HTTP, and notifier fixtures were corrected and passed.
- The foreign Monero wallet fixture is now explicitly expected to fail the Monzero genesis check; it is not accepted as a Monzero wallet.
- The initial `tests/phase0/monzero_multinode.py --build-dir build` harness passed twice, demonstrating three-node peer connectivity, block propagation, restart/catch-up, and a common tip in isolated regtest directories.

The 1,230-test result excludes only
`cryptonote_protocol_handler.race_condition`. Its handcrafted miner
transaction always used the legacy `txout_to_key` output, even after the test
chain activated hard-fork v16. Consensus correctly rejected that alternate
chain because v16 requires `txout_to_tagged_key`, leaving the test waiting for
an impossible synchronization event. The fixture now selects the output type
from the active hard-fork version.

`tests/phase0/run-stress-tests.sh build` subsequently passed the focused
approximately 9,600-block concurrency test in 9.582 seconds. Phase 0 and asset
activation remain open until the wallet-restoration, double-spend/adversarial
reorg, and reproducible-release checks pass.

The extended three-node harness passed again at final height 84. It verified
competing three- and five-block forks, convergence on the longer fork after
reconnection, one-XMZ transaction relay through every node, confirmation by a
different node, and the exact confirmed receiver balance. It then restored the
receiver from its temporary mnemonic seed, rescanned from height zero, and
recovered the identical address and balance.

The focused native adversarial gate initially found that the inherited
`FIRST_BLOCK_REWARD` in `tests/core_tests/double_spend.h` still used Monero's
subsidy. Monzero's larger genesis subsidy introduced an unexpected miner change
output, so the same-transaction fixture tried to derive Bob's key image from
the miner's output and failed before reaching consensus validation. Updating
the fixture to the already verified Monzero subsidy fixed the test setup.

`tests/phase0/run-adversarial-core-tests.sh build` then passed all 16 selected
cases with zero failures. Coverage includes transaction-pool public/all-key
spends, no-relay/local/key-image conflicts, duplicate inputs in one
transaction, double spends across the same or different blocks, competing
chains, alternate-chain rollback, and both kept-by-block modes.

## Existing artifact audit

The existing `genesis-pre2` Linux and Windows outer checksums and every inner
package checksum pass. Archive listings contain the intended executables,
launch scripts, documentation, license, and checksum manifests.

These packages are nevertheless **stale baseline artifacts**, not candidates
for republication after the Phase 0 fixes. The current native Linux binaries
are dynamically linked, unstripped developer builds with host-library
dependencies (including Boost 1.90), and the existing Windows binaries predate
the stabilization changes. New artifacts must be produced from an immutable,
documented source snapshot in pinned Linux and Windows build environments and
reproduced by a second build before their filenames or website links change.

## Protected manifest and inactive asset identity prototype

`config/monzero-consensus.json` now records the monetary policy, transaction
version, asset-acceptance state, hard-fork schedule, and protected vectors for
all three public networks. Three compiled `monzero_consensus` tests regenerate
the genesis blocks and lock the corresponding hashes, UUIDs, nonces, prefixes,
ports, emission constants, precision, and hard-fork schedule.

The research-only `cryptonote::assets::issuance_descriptor` provides bounded
validation, canonical encoding, network domain separation, and a pinned
asset-ID vector. It is compiled for testing but is not referenced by
transaction serialization, consensus validation, the mempool, blocks, RPC, or
wallets. `CURRENT_TRANSACTION_VERSION` remains 2 and the manifest explicitly
records `public_asset_transactions_accepted: false`.

After these additions, the fast unit gate passed 1,237 of 1,237 selected tests
from 153 test cases in 170.050 seconds. The separate pruning concurrency gate
also passed in 10.220 seconds.

The inactive asset research code now additionally includes a transparent
semantic conservation validator. It separates XMZ fees from issued-asset
balances, requires issuance outputs to equal the declared fixed supply, and
enforces independent input/output/burn equality for every known asset. Focused
tests cover valid issuance, transfers and burns plus duplicate issuance,
unknown assets, cross-asset conversion, native inflation, noncanonical values,
and integer-overflow attempts. This validator remains disconnected from all
transaction and consensus paths and is not a privacy construction.

With the six additional conservation tests included, the deterministic fast
gate passed 1,243 of 1,243 selected tests from 153 test cases in 181.415
seconds on 2026-08-15. The separate approximately 9,600-block pruning
concurrency stress test also passed in 14.564 seconds.

The provisional descriptor was advanced to version 2 before activation to add
an explicit asset class and collection ID. Fungible, non-fungible, collection,
and edition identities are now unambiguous. NFT and collection descriptors
require one indivisible unit, while edition units are indivisible. Issuer and
collection-membership messages have separate hash domains and signature
verification. Fifteen focused asset/consensus tests pass; no public transaction
format or asset acceptance rule is enabled.

After the descriptor-v2 and authorization additions, the deterministic fast
gate passed 1,245 of 1,245 selected tests from 153 test cases in 152.606
seconds on 2026-08-15.

An inactive reference registry now exercises authenticated issuance state,
verified collection membership, ordering by issuance height, duplicate
rejection, and deterministic removal/reissuance across simulated chain
detachments. Seventeen focused asset and protected-consensus checks pass. The
registry is intentionally in-memory and is not connected to LMDB or block
validation.

The exact staged Phase 0 source snapshot subsequently passed 1,247 of 1,247
selected deterministic tests from 153 test cases in 162.024 seconds.

## Deterministic packaging checkpoint

`utils/release/package-linux.sh` and `verify-package.sh` now construct and
validate normalized Linux archives. Two development archives made from the
same input binaries and epoch were byte-identical, with SHA-256
`a47ae087706f52a96829a83b3e4bd8be722663eb6ca3a95743526e4005314a41`.
The test archive passed extraction, inner-manifest, executable-format, and
version checks.

This does not promote the test artifact to a release. Strict verification
correctly rejects it because the source tree is dirty, binary reproducibility
is unverified, and the developer executables are dynamically linked,
unstripped, and contain debug information. The generated archives remain in
temporary directories and were not copied to `dist/` or the website.
