# Offline release signing

The Monzero production release key must be created and retained on a dedicated
offline system. Do not generate it on a development workstation, VPS, CI
runner, web server, or everyday desktop. A release signature proves control of
the key; it does not replace independent builds, audit evidence, or package
verification.

## Preparation

Use a freshly installed, network-disconnected system from trusted installation
media. Record the operating-system image digest and GnuPG version. Prepare two
new encrypted removable drives for redundant backups and one separate transfer
drive for public inputs and signatures. Never store a passphrase in a command,
script, shell history, repository, screenshot, or password file beside the key.

The key custodian should create the key interactively so GnuPG obtains the
passphrase through pinentry:

```bash
export GNUPGHOME="$PWD/monzero-release-keyring"
install -d -m 700 "$GNUPGHOME"
gpg --quick-generate-key \
  'Monzero Release <security@monzero.org>' ed25519 cert 5y
```

On this fresh keyring, obtain and write down the complete primary fingerprint:

```bash
gpg --with-subkey-fingerprints --fingerprint \
  'Monzero Release <security@monzero.org>'
```

Enter that exact 40-character primary fingerprint when adding a one-year
signing subkey:

```bash
gpg --quick-add-key PRIMARY_FINGERPRINT ed25519 sign 1y
```

The certification-only primary key authorizes identity and subkey changes. The
shorter-lived signing subkey signs release metadata. Rotate the signing subkey
before it expires; do not replace the primary identity merely to renew a
subkey.

## Backups and revocation

While still offline, export the public key, a passphrase-protected full secret
backup, and a revocation certificate:

```bash
gpg --armor --export PRIMARY_FINGERPRINT > monzero-release-public.asc
gpg --armor --export-secret-keys PRIMARY_FINGERPRINT \
  > monzero-release-secret-backup.asc
gpg --armor --output monzero-release-revocation.asc \
  --generate-revocation PRIMARY_FINGERPRINT
```

Store the two encrypted secret-key backups in physically separate controlled
locations. Store the revocation certificate separately from both the working
key and public transfer media. Test restoration and fingerprint equality on a
second offline system before trusting the backup. Destroy any failed or
uncontrolled copies securely.

Publish only `monzero-release-public.asc` and the complete primary fingerprint.
Verify the fingerprint through at least two independently controlled channels,
such as the HTTPS website and the public source repository. A key server may be
an additional distribution path but must not be the sole trust source.

## Signing a release

First finish all release metadata on the connected release workstation. Copy
only the final metadata and its independently calculated SHA-256 digest to the
clean transfer drive. On the offline signing system, verify the filename,
contents, source commit, artifact hashes, audit state, reproduction state, and
transfer digest before signing:

```bash
gpg --armor --detach-sign RELEASE.json
gpg --status-fd 1 --verify RELEASE.json.asc RELEASE.json
```

GnuPG should select the valid signing subkey. Copy only `RELEASE.json.asc` back
to the connected workstation. Do not export secret key material as part of the
signing workflow.

Import the public key into a clean verification keyring and run:

```bash
RELEASE_PRODUCTION=1 \
MONZERO_RELEASE_FINGERPRINT=PRIMARY_FINGERPRINT \
MONZERO_REPRODUCER_FINGERPRINT=INDEPENDENT_REPRODUCER_PRIMARY_FINGERPRINT \
  utils/release/verify-publication.sh RELEASE.json artifact-directory
```

The verifier resolves a signature made by a signing subkey back to its trusted
primary fingerprint. It requires the independent reproducer to use a distinct
primary key and rejects missing, malformed, ambiguous, or incorrectly signed
evidence.

## Windows code signing (Authenticode)

GnuPG signatures prove a release to people who check them; Windows checks only
Authenticode signatures, so unsigned executables show "Unknown publisher" and
SmartScreen warnings. Monzero signs Windows executables with a Certum Open Source
Code Signing certificate. The certificate subject names the key holder, and
Windows shows that name to every user. Its private key lives on the Certum card
(or in Certum SimplySign) and never leaves it; the card PIN is entered when the
PKCS#11 module asks for it, never on a command line or in a file.

One-time setup on the signing workstation:

1. Install `osslsigncode` (tested with 2.15), `libp11` (the OpenSSL PKCS#11 engine),
   `p11-kit` and Certum's software for the card or SimplySign; note the path of
   its PKCS#11 library.
2. Find the key and certificate URIs, for example
   `p11tool --provider /path/to/certum-pkcs11.so --list-all-privkeys --login`
   and `--list-all-certs`.
3. Export the signing certificate as PEM and append Certum's code-signing
   intermediate certificate to make `certum-chain.pem`. Record the certificate's
   exact subject; it is the expected signer for verification.

Signing happens after the unsigned packages are built and verified, and before
release metadata is generated, because the metadata must list the signed files:

```bash
export MONZERO_ZIP="$PWD/utils/release/container-zip.sh"
export MONZERO_CODESIGN_PKCS11_MODULE=/path/to/certum-pkcs11.so
export MONZERO_CODESIGN_KEY='pkcs11:...;type=private'
export MONZERO_CODESIGN_CERTS=/path/to/certum-chain.pem
utils/release/sign-windows-package.sh dist/monzero-VERSION-windows-x64.zip dist/signed
utils/release/sign-windows-package.sh dist/monzero-VERSION-windows-gui-x64.zip dist/signed
MONZERO_EXPECTED_SIGNER='CN=...' utils/release/verify-windows-package.sh dist/signed/monzero-VERSION-windows-x64.zip
MONZERO_EXPECTED_SIGNER='CN=...' utils/release/verify-windows-gui-package.sh dist/signed/monzero-VERSION-windows-gui-x64.zip
```

Each executable is signed with SHA-256 and an RFC 3161 timestamp from
`http://time.certum.pl`, so signatures stay valid after the certificate expires.
The signed archive keeps the unsigned file name. Inside, the build manifests are
left exactly as built (the GUI manifest's `code_signed` field describes the
build, not the package) and `CODE-SIGNING.txt` records the signer, the unsigned
package hash and each executable's unsigned hash. Before writing anything, the
signer checks that removing each signature restores the unsigned executable and
that `utils/release/unsign-windows-package.sh` rebuilds the original unsigned
archive byte for byte. Independent builders reproduce the unsigned package; see
`INDEPENDENT_REPRODUCTION.md`. Then generate the release metadata from
`dist/signed` and sign it with GnuPG as above.

The verifiers reject a signed package whose executables do not verify, carry a
different signer from the record (or from `MONZERO_EXPECTED_SIGNER`), lack a
recorded timestamp, or do not strip back to their recorded hashes, and an
unsigned package that contains any signed executable. `RELEASE_STRICT=1` also
requires timestamped signatures.

To rehearse without the card, create a throwaway self-signed certificate,
export it as PKCS#12, and run the same commands with
`MONZERO_CODESIGN_PKCS12=test.p12` and `MONZERO_CODESIGN_CAFILE=test-cert.pem`
instead of the PKCS#11 settings. Never publish a rehearsal package.

Signing does not remove every warning at once. SmartScreen builds reputation per
certificate from downloads, and software that includes a CPU miner is often
flagged by antivirus products. Submit each signed release to the Microsoft
Security Intelligence portal as software developer.

## Incident response

If the offline system, backup, passphrase, transfer process, or signing subkey
may be compromised, stop publication immediately. Preserve evidence, publish
the pre-generated revocation certificate through every fingerprint channel,
investigate affected releases, and establish a new signing identity using a
documented recovery ceremony. Never silently replace a published fingerprint.
