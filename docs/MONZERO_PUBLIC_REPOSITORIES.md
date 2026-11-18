# Monzero source distribution

The former Monzero code-host deployment has been retired. As of 2026-09-13,
the homepage and root `README.md` both link
`https://github.com/Nightglass-Protocol1/monzero` as "the public source
repository," but that mirror currently shows only a handful of commits and
has not been verified against the checklist below (MFA/two-owner
organization, branch protection, signed tags, independent-review gating,
clean recursive-clone verification). Until it passes that checklist, treat
it as a convenience mirror only, not the canonical, reviewable source of
truth — do not describe it as authoritative in release metadata, and do not
assume its history is complete or byte-identical to the working tree. This
inconsistency between the live site and this document needs a deliberate
decision (promote this mirror through the checklist, or stop linking it)
rather than silent drift.

Until a maintained public forge is selected, prerelease source should be
distributed as a versioned archive together with its checksum and signed
release manifest. Recipients should verify those artifacts before building.
The local bare repositories remain development backups, not public release
channels.

`utils/release/package-source.sh` creates that fallback archive from one exact
commit and recursively overlays every pinned submodule commit. Two packaging
runs must match byte for byte, and `utils/release/verify-source-package.sh`
must pass before the artifact is added to release metadata. This source bundle
is a distribution fallback, not a substitute for a maintained, reviewable
public forge.

Before publishing a replacement forge:

- establish a project-owned organization with multifactor authentication and
  at least two owners;
- protect the release branch against force pushes and deletion;
- require passing build checks and independent review;
- publish annotated, signed release tags;
- verify a clean recursive clone, including the GUI core submodule; and
- keep human Gitian signing keys outside automated workflows.

At least two independently administered builders should reproduce Linux and
Windows artifacts before they are described as verified. Each reproducer must
follow `INDEPENDENT_REPRODUCTION.md` and sign the canonical byte-for-byte
comparison evidence with an independently verified key. Same-host repeat
builds do not satisfy this requirement. Asset/NFT consensus
activation additionally remains subject to the review and public-testnet gates
in `MONZERO_PHASE0_STABILIZATION.md`.
