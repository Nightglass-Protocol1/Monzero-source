# Active LTC/XMZ backup acceptance

Status: implementation and acceptance still required. Existing VM process
restart and full refund evidence do not establish backup restoration.

## Evidence and gaps

| Current source | What it establishes | What is missing |
| --- | --- | --- |
| `src/dex/LightWalletBackupRestore.h` | Restore reviews require scope `seed-and-address-allocations`, `activeSwapRecovery=false`, `tradingEnabled=false` | Active trade state is deliberately excluded |
| `tools/dex-trade-kit/trade_kit.py` | App manifests bind local helper, service, identity, quote and state paths | Copying only the app folder omits external identity, quote, wallet and service dependencies |
| `src/dex/LitecoinAppConfig.cpp` | Related phases must agree on trade, role, policy, pins and state paths | Relocation must preserve those bindings and require review of changed local services |
| `src/dex/LitecoinLightFundingContext.h` | Retained payment is bound to trade and configuration digest | Rewriting paths/configuration after restoring cannot silently bypass this binding |
| `remote_light_recovery.py` | Same-machine restart verifies wallet and protected metadata hashes and clears approval | A live hash walk is not a transactional backup of all writers |
| `DEX_RECOVERY_KEY_LIFECYCLE.md` | Required encrypted recovery-key lifecycle and crash tests | Requirements are not evidence that a production vault exists |

## Required implementation

1. Inventory the complete local dependency set from trusted configuration:
   signed quote and terms, local identity/TLS material, journals and recovery
   intent, secret snapshots, reservations, retained signed spends and broadcast
   intents, wallet vault/address allocations, prepared native transactions and
   required unlock envelopes. Never import the other trader's private state.
2. Obtain a consistent snapshot across app, helper and wallet writers. A
   directory walk or copying only after the GUI closes does not establish that
   every helper stopped. Preserve transaction ownership and fail closed when
   quiescence or a stable generation cannot be established.
3. Encrypt the complete private bundle with a separately reviewed local unlock
   mechanism. Bound format sizes and KDF work; authenticate network/genesis,
   role, trade identity and every retained record. Do not treat a stored seed
   backup as an active-trade backup or put plaintext keys alongside ciphertext.
4. Publish an immutable archive with durable contents and parent directories.
   Never replace an existing backup silently. An interrupted export must retain
   the last usable generation and report any uncertain publication outcome.
5. Restore into a new private staging location; reject traversal, links,
   unexpected owners/modes, duplicate entries, altered files and substituted
   trade/chain/wallet context. Do not execute helpers merely because an archive
   names them. Revalidate installed helper digests and local service settings.
6. Preserve immutable signed terms and retained transaction bytes. If relocation
   changes a configuration binding, implement an explicit authenticated migration;
   editing JSON to get past a mismatch is not recovery. Reject old approvals.
7. Reconcile both chains before offering any fresh send/retry approval. Restore
   must not start a replacement trade, sign replacement funding or relay by itself.
   Monitor the original block-height deadlines throughout recovery.

## Acceptance evidence required

- Wrong unlock secret, corrupt/truncated archive, substituted network/trade or
  wallet, excessive lengths/KDF, duplicate/path-traversal/link entries.
- Concurrent writers and interrupted writes/publication at every durable boundary.
- Restore after removing access to original files, then settle or refund on both
  test chains with exact accounting and unchanged retained signed bytes.
- Signed-but-unsent, accepted-but-unacknowledged, both-locks-confirmed and
  asymmetric-settlement cases; stale backups must be detected or reconciled
  conservatively rather than interpreted as permission to repeat funding.
- Entire-app and VM power-loss cases, including key-holding parents and helpers.
- Desktop export/review/unlock/restore/reconcile workflow and supported-platform
  durability. A lab archive restored to its original absolute path is useful
  evidence but does not close portable recovery or the user-facing workflow.

No current result closes this gate. Record artifact paths, source hashes,
retained-byte comparisons, final chain accounting and cleanup for each run.
