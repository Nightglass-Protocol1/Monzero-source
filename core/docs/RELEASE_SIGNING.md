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

## Incident response

If the offline system, backup, passphrase, transfer process, or signing subkey
may be compromised, stop publication immediately. Preserve evidence, publish
the pre-generated revocation certificate through every fingerprint channel,
investigate affected releases, and establish a new signing identity using a
documented recovery ceremony. Never silently replace a published fingerprint.
