# Monzero release checklist

This checklist applies to every Monzero node, CLI wallet, GUI wallet, miner
control, website download, and consensus release. Native amounts use XMZ.

## Scope and source

- [ ] Release version, codename, source commit, and supported platforms agreed
- [ ] Working tree clean or every included patch documented
- [ ] Submodules pinned and verified
- [ ] Consensus-affecting changes identified explicitly
- [ ] Database or wallet migration requirements documented
- [ ] User-visible changes and known limitations written
- [ ] Independent reviewer approves the release scope

## Consensus and network safety

- [ ] Genesis and protected consensus-vector tests pass
- [ ] Block reward, precision, fees, and amount parsing tests pass
- [ ] Valid and invalid transaction tests pass
- [ ] Synchronization from an empty data directory succeeds
- [ ] Reorganisation and restart tests pass
- [ ] Node interoperates with the currently deployed release
- [ ] Seed list contains no node configured to connect to itself
- [ ] Mainnet, testnet, and stagenet identifiers remain isolated
- [ ] Hard-fork activation height and operator notice reviewed, if applicable

## Wallet safety

- [ ] New wallet creation tested in CLI and GUI
- [ ] Seed restoration reproduces addresses and balances
- [ ] Incoming and outgoing transfers confirmed
- [ ] Pending, failed, pool, and confirmed history tested
- [ ] Coinbase and normal output unlock behaviour tested
- [ ] Wallet shutdown and unclean-restart recovery tested
- [ ] View-only, multisig, hardware, and offline signing marked tested or unsupported
- [ ] No workflow requests a recovery seed except an explicit recovery operation

## Branding and configuration

- [ ] Product name is Monzero and native ticker is XMZ
- [ ] Executable names, titles, prompts, logs, and default paths are correct
- [ ] Ports, address prefixes, network UUIDs, DNS seeds, and website are correct
- [ ] Required upstream attribution and copyright notices are preserved
- [ ] User-facing inherited Monero service links are removed or clearly contextual
- [ ] Warnings prohibit cross-fork key and seed reuse

## Automated testing

- [ ] Unit tests pass from a clean build
- [ ] Core and functional tests pass
- [ ] RPC compatibility tests pass
- [ ] Parser and consensus fuzzing has no unresolved regressions
- [ ] Website and explorer API smoke tests pass
- [ ] Linux launch, mining, wallet, and uninstall scripts pass shell validation
- [ ] Windows launch and mining scripts pass on a clean Windows system

## Build and package

- [ ] Linux build produced by the pinned build environment
- [ ] Windows build produced by the pinned cross-build environment
- [ ] A second builder checks reproducibility
- [ ] Executable formats and required runtime libraries inspected
- [ ] Archives contain only intended files
- [ ] Deterministic source archive includes the exact commit and recursive pinned submodules
- [ ] README and upgrade instructions included
- [ ] SHA-256 manifest generated and verified
- [ ] Release manifest signed with the Monzero release key
- [ ] Packages scanned and tested after extraction from their final archives

The Linux archive layer is generated with `utils/release/package-linux.sh`.
It refuses a dirty tree unless `ALLOW_DIRTY=1` is explicitly supplied, in
which case the filename and embedded manifest are marked development/dirty.
`utils/release/verify-package.sh` verifies archive shape, inner checksums,
safe extraction paths, required release documents, executable presence,
formats, and identical manifest-bound versions across the daemon and wallet.
All binary and source package verifiers also require `RELEASE_NOTES.md` to
declare the exact package release label; stale notes fail verification before
an archive can qualify.
This makes packaging
deterministic; it does not by itself make locally compiled binaries
reproducible. `RELEASE_STRICT=1` additionally rejects dirty manifests,
unverified build reproducibility, unexpected non-system runtime dependencies,
debug information, and unstripped executables. Linux binaries may retain the
minimal glibc-family dynamic dependencies permitted by the strict verifier;
third-party dependencies must be linked into the candidate.
Strict Linux verification also rejects symbol requirements newer than
`GLIBC_2.35` by default. Override `MONZERO_MAX_GLIBC` only when a different
documented compatibility floor has been explicitly approved. Run
`utils/release/smoke-test-linux-package.sh` against the exact final archive in
its digest-pinned, network-isolated Ubuntu 24.04 container before publication;
an allowlisted library name alone does not prove that its symbol versions are
portable.

An official GUI artifact must pass `verify-windows-gui-package.sh`. Stable
verification requires its manifest to record native Windows testing,
independent reproduction, completed security review, and release-test
execution. A GUI artifact listed in release metadata must also be included in
the independent reproduction comparison and signed attestation.

`BINARY_BUILD_REPRODUCIBILITY=verified` may be supplied to the packager only
after an independent builder has reproduced the exact packaged binaries. The
default is `unverified`; setting the variable is an attestation input, not a
reproducibility test. Follow `INDEPENDENT_REPRODUCTION.md`; the independent
builder must run `utils/release/compare-reproduced-packages.sh` and sign its
canonical evidence with a separately trusted key.

Before publication, run `utils/release/verify-publication.sh` against the
machine-readable release metadata and final artifact directory. For a stable
release, set `RELEASE_PRODUCTION=1`, `MONZERO_RELEASE_FINGERPRINT`, and
`MONZERO_REPRODUCER_FINGERPRINT` to the exact trusted 40-character uppercase
OpenPGP fingerprints. Production mode requires stable/production metadata,
completed security audit and independent reproduction states, a detached
`.json.asc` release signature, a separately signed reproduction attestation
that binds the exact artifacts, distinct release and reproducer keys, and
strict verification of both Linux and Windows archives. The verifier never
downloads or implicitly trusts a key.

Create and operate the release key using the offline procedure in
`RELEASE_SIGNING.md`. The trusted fingerprint is the certification-only primary
key fingerprint even though the detached signature is made by its rotating
signing subkey.

When no canonical public forge can provide the exact release commit, build the
complete source archive twice with `utils/release/package-source.sh`, compare
the archives byte for byte, and validate it with
`utils/release/verify-source-package.sh`. Production metadata must bind exactly
one verified source archive in addition to the Linux and Windows binaries.

Gitian builds must be given the explicit Monzero source URL with `--url`.
There is deliberately no implicit upstream fallback. During initial setup,
provide the independent Monzero signatures repository through
`MONZERO_GITIAN_SIGS_URL`, or place an existing verified checkout at `sigs/`.
The descriptor's `example.invalid` source is a fail-closed placeholder that is
overridden by Gitian's `--url monero=<explicit-url>` input. Do not replace it
with an upstream project or publish unsigned Gitian results.

## Deployment

Run `utils/release/verify-network-readiness.sh http://127.0.0.1:6175` on the
public-node host against its operator-local RPC before promotion. The gate
requires mainnet, status `OK`, current synchronization, a fresh tip, internally
consistent heights, and at least two peers by default. A restricted public RPC
may hide peer information and is not sufficient evidence for this gate.

- [ ] Public node binaries backed up before replacement
- [ ] Public node database backup or recovery procedure confirmed
- [ ] New node starts, binds expected ports, and reports correct version
- [ ] Peer counts, height, difficulty, synchronization, and RPC checked
- [ ] At least one independent node successfully handshakes and synchronizes
- [ ] Explorer and website display current chain data
- [ ] Download files, sizes, MIME types, checksums, and links verified publicly
- [ ] Rollback binary and rollback instructions remain available

## Publication and monitoring

- [ ] Release notes identify experimental and unaudited components
- [ ] Source commit and signed hashes published
- [ ] Node, miner, wallet, and service operators receive upgrade instructions
- [x] Security contact and disclosure process published
- [ ] Height, fork divergence, peers, RPC, CPU, memory, disk, and logs monitored
- [ ] Post-release review performed after 24 hours and seven days

## Assets-specific gate

For any future Monzero Assets release:

- [ ] Phase 0 stabilization gates are complete
- [ ] Consensus and cryptographic specifications are public
- [ ] Independent cryptographic and implementation audits are resolved
- [ ] Inflation and cross-asset conversion tests pass
- [ ] Asset wallet restoration tests pass
- [ ] Extended public testnet has completed a planned upgrade
- [ ] Mainnet activation height has advance operator coordination
