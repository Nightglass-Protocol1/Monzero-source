# Native recovery-key lifecycle

Status: implementation requirements, not an implemented vault or live gate.
Unlock preference has been requested from the owner: separate recovery passphrase
(recommended) versus wallet-password unlock. Do not silently export the wallet
password, derive from a wallet seed, or reuse the offer-signing identity.

## Required design

- Generate a random 256-bit recovery master key in the native trusted component.
  Encrypt that key under the chosen unlock mechanism; do not store the plaintext
  key alongside recovery ciphertext. Separate passphrase unlock can follow the
  existing Argon2id/XChaCha20-Poly1305 pattern, with a distinct versioned domain.
- Distinguish the encrypted master-key envelope from per-transaction encrypted
  pending metadata. A passphrase change rewraps the same master key, rather than
  invalidating all pending transactions. Master-key rotation is a separate,
  explicitly versioned migration and must retain access to outstanding records.
- Authenticate the format/version, vault identity, chain genesis, wallet identity,
  stable trade/reservation identity and exact prepared-accounting digest.
  Serialize that context canonically with unambiguous field boundaries.
  Never accept peer-provided context as the trusted expected binding.
- Bound ciphertext and KDF parameters before allocation or expensive work.
  Reject unsupported versions, altered context, truncation, wrong unlock secrets
  and corrupt records without secret-bearing errors or fallback construction.
- Keep metadata and keys outside QML, network RPC, argv, environment variables,
  logs and public journal records. Use a narrowly scoped private native IPC path.
  Decryption must not itself authorize relay; retain trade consent, exclusive
  wallet ownership, input checks and reconciliation as independent requirements.

## Filesystem and crash boundaries

Reuse the native journal's descriptor-anchored ownership, permissions and
no-symlink checks. The Qt identity store's path checks and atomic-save call alone
are not the recovery vault's complete durability model.
File contents and parent-directory changes must be synchronized before any
capability that could broadcast is returned.
Serialize creation and migration across processes. Never overwrite an existing
reservation or interpret a torn record as permission to recreate a transaction.

The crash matrix must cover reservation, preparation, encrypted artifact write,
public-accounting linkage, dispatch, lost reply, confirmation and restart.
A successfully encrypted artifact is not sufficient unless its exact transaction
is durably linked to the retained approved terms before dispatch.
An interrupted migration must leave a deterministically readable old or new
generation; do not delete the last usable key envelope on an RPC acknowledgement.
Never implement retry by constructing a different transaction or clearing a lock.

## Backup and user-facing behavior

Recovery requires the unlock secret **and** the encrypted key envelope,
transaction artifacts and bound execution records. A passphrase alone does not
recreate a prepared transaction. Losing required records or the unlock secret can
make recovery unavailable; warn before funding and provide a tested encrypted
backup/export workflow. Do not imply wallet seed backup alone covers these files.

Keep the GUI locked until an explicit local unlock succeeds. Report locked,
unsupported, corrupt and unresolved states without exposing secrets or offering
an unsafe reset/retry action. Platform-specific secure storage and Windows
durability need their own implementation and validation, not a Unix-only claim.

## Acceptance before integration

Test wrong key/passphrase, altered associated data, ciphertext substitution,
cross-wallet/chain/trade swaps, size/KDF limits, symlinks, hardlinks, ownership,
permissions, concurrent writers and every crash boundary above.
Then terminate the entire application—including the key-holding parent—restart,
unlock from retained encrypted storage and reconcile or relay the exact prepared
transaction on disposable chains. Repeat with a lost reply and wrong open wallet.
The current Python parent-held-key fixture does not meet this requirement.

Source review: `OfferIdentity.cpp`, `OfferIdentityStore.cpp`, native
`spend_journal.rs`, `exact_net_journal.rs` and `DEX_EXACT_NET_RESTART_DESIGN.md`.
Claude review: `build/claude-recovery-key-lifecycle-review.log`.
Its suggested design is advisory, not an independent audit or an implemented
capability. Public accounting already exists; the prohibition here concerns
private metadata, not removing the existing accounting/reconciliation records.
