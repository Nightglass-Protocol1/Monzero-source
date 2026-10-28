# Independent release reproduction

Independent reproduction is a production release gate, not a flag the release
operator may self-assert. The reproducer must control a separate clean build
host, obtain the source independently, review the exact source commit and
submodule revisions, and build both supported command-line packages with the
pinned dependency environments.

If the canonical public forge is unavailable, first verify the release-bound
source archive with `utils/release/verify-source-package.sh`. Compare its
`SOURCE-MANIFEST.txt` commit and recursive submodule hashes to the signed
release metadata before building. Do not accept a source tree sent only through
the same channel as the reference binaries without independently checking its
published digest.

The release operator supplies reference and independently built Linux and
Windows packages. If official metadata includes the Windows GUI, its reference
and independently built packages are required as an additional pair. On the
reproducer's machine, run:

```bash
export MONZERO_REPRODUCER_ID='public name or organization identifying the builder'
export MONZERO_REPRODUCER_ENVIRONMENT='OS image, architecture, build isolation, and toolchain description'
utils/release/compare-reproduced-packages.sh \
  reference/monzero-VERSION-linux-x86_64.tar.gz \
  reproduced/monzero-VERSION-linux-x86_64.tar.gz \
  reference/monzero-VERSION-windows-x64.zip \
  reproduced/monzero-VERSION-windows-x64.zip \
  RELEASE.reproduction.json
gpg --armor --detach-sign RELEASE.reproduction.json
```

For a release that includes the GUI, insert the reference and reproduced GUI
ZIP paths immediately before `RELEASE.reproduction.json`.

The comparator runs the normal package verifier against every input and
requires byte-for-byte equality for each published platform, one exact clean source
commit, and safe package structure before writing canonical JSON. A mismatch
is a stop condition; do not normalize timestamps, rebuild an archive from
extracted files, or compare only executable version strings.

Send `RELEASE.reproduction.json`, its detached signature, and the full public
fingerprint of the signing key to the release operator through an authenticated
channel. The operator must verify the builder's identity and fingerprint out
of band. The reproducer key must be distinct from the release-signing key.

For production publication, place the evidence beside the release metadata:

```text
RELEASE.json
RELEASE.json.asc
RELEASE.reproduction.json
RELEASE.reproduction.json.asc
```

Then run the production verifier with both pinned fingerprints:

```bash
RELEASE_PRODUCTION=1 \
MONZERO_RELEASE_FINGERPRINT=40_CHARACTER_RELEASE_FINGERPRINT \
MONZERO_REPRODUCER_FINGERPRINT=40_CHARACTER_REPRODUCER_FINGERPRINT \
  utils/release/verify-publication.sh RELEASE.json artifact-directory
```

The verifier requires stable metadata, completed audit state, exact final
artifact hashes in the signed reproduction evidence, two distinct trusted
keys, and strict verification of both packages. Same-host candidate A/B builds
are useful determinism tests but are not independent evidence and must never be
identified as such.
