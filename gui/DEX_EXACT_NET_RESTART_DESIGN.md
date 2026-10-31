# Exact-net pre-dispatch restart recovery

Status: research, not live recovery authorization.
Durable key-storage requirements and the pending unlock choice are tracked in
`DEX_RECOVERY_KEY_LIFECYCLE.md`; no production vault has been implemented yet.
The approved principal stays exact; the buyer funds the separate fee reserve
and receives unused reserve. This document does not change signed quote v1.

## Evidence and decision

`wallet_rpc_server.cpp::ptx_to_string` serializes `wallet2::pending_tx`.
That object contains transaction secret keys, destinations and construction data.
Wallet `relay_tx` deserializes it and calls `commit_tx`.
Do not copy this metadata into the public accounting journal or log it.
Unix mode 0600 alone is not an encryption or backup-disclosure policy.

By contrast, `get_tx_hex` returns only the serialized signed transaction.
The disposable `verify_raw_restart_candidate` experiment stores those bytes
privately, closes the source wallet, submits the identical bytes from two new
processes and confirms one transaction. Both destinations receive the expected
amounts and the source balance becomes zero after refresh.
Both funded runs lacked destination bookkeeping in the source wallet.
Consequently raw daemon relay is not a drop-in replacement for the existing
`ExactNetRecovery::reconcile`, which requires exact per-destination evidence.
The experiment is not engine journal restoration, process-kill testing,
independent transaction decoding or complete negotiated swap settlement.

## Next implementation boundary

Two candidate artifact/reconciliation paths were investigated:

1. A bounded raw signed transaction artifact, independently decoded and hashed
   against its bound accounting record, plus sufficient verified evidence to
   reconcile both destinations without trusting missing wallet history; or
2. Authenticated encryption of the wallet's pending metadata with a defined
   local key lifecycle, strict version/binding validation and controlled wallet
   restoration that preserves its bookkeeping.

The encrypted-metadata path is the next implementation candidate because the
disposable close/reopen experiment preserves both destination records, unlike
raw relay. The fixture uses AES-256-GCM with a random temporary key and nonce,
binding its format, wallet and transaction ID as authenticated associated data.
Only ciphertext is written to the private fixture artifact; the key travels to
a fresh worker via stdin. A wrong-key worker must fail before any broadcast.
The parent retains the key in memory, so this does not prove whole-application
restart, durable production key storage, secure erasure in Python or engine
recovery authorization. It is a test of the wallet behavior needed by option 2.

Before implementing native restoration, validate pending input identities before
`wallet2::commit_tx` sends anything. The current normal-wallet path checks output
index bounds only after daemon submission and later indexes the wallet's local
transfer array. That bounds check does not establish that the saved index still
denotes the original input after a rescan or wallet change. A same-wallet reopen
test does not cover those cases. Bind exact input key images and wallet identity,
then test altered indices, wrong wallet, rescans and repeated relay separately.
Do not treat an AEAD tag as proof that saved wallet-local indices remain current.

Neither option is authorized by simply observing an unspent input or a missing
transaction in one node's view. Do not reconstruct a replacement transaction
from those observations or delete the original reservation to permit retry.
Rebroadcast must mean the exact preserved transaction, never a new preparation.

Before integration, test artifact durability before dispatch, truncated or
substituted records, wrong wallet/chain/trade binding, concurrent restarts,
lost acknowledgements, repeated exact-byte submissions and reorg handling.
Keep private artifacts outside GUI/peer messages and public journals.
Do not delete recovery evidence merely because an RPC acknowledges relay;
confirmation and later reorgs remain separate concerns.

## Review limitations

Claude's bounded review is recorded in
`build/claude-exact-net-restart-design.log`.
It is not an independent audit. Its suggestion to persist plaintext metadata
has not been adopted. Its assertion that raw relay leaves the wallet unaware
of spending is too broad: the experiment observes a zero balance and outgoing
transaction after refresh, but missing destination bookkeeping.
The existing one-shot gates remain intact while this boundary is unresolved.

Validation: `build/exact-net-raw-restart-research.log` passed all 89 library
tests, three compile-fail tests and both funded scenarios, including the normal
relay, dropped-reply and raw restart experiments. Ordered patch reconstruction
and the two acceptance-runner unit tests passed. Only the Python fixture and
documentation changed for this experiment; native relay behavior is unchanged.

Encrypted comparison validation: `build/exact-net-sealed-restart-research.log`
passed 89 library tests, three compile-fail tests and both funded scenarios.
Both encrypted-metadata runs rejected the wrong key without broadcast and
confirmed the original transaction with destination bookkeeping preserved.
The two acceptance-runner unit tests also passed. This step changes only the
Python fixture and docs; no production encryption or relay code was introduced.
Claude review: `build/claude-sealed-metadata-candidate-review.log`.
Its absolute claim of no index validation is qualified by the actual source:
there is a post-submission bounds check, but not a pre-submission input identity
check. That distinction determines the next implementation task.

## Pre-submission RPC input validation (isolated integration verified)

`on_relay_tx` now invokes `pending_inputs_match_wallet` before `commit_tx`.
It requires a nonempty, one-to-one match between unique valid selected output
indices, known complete nonzero wallet key images and transaction input key
images. Invalid types, duplicates and stale identities fail before submission.
The call uses existing transfer accessors, not a copy of all private wallet data.
The RPC server explicitly runs one handler thread; other submission paths have
not been changed. This is input identity validation, not complete consent,
metadata accounting validation, repeated-relay safety or key management.

Four focused tests passed using the bundled GoogleTest sources under both Clang
and GCC/C++14: `build/pending-input-unit-results.log` and
`build/pending-input-unit-gcc-results.log`.
Claude review: `build/claude-pending-input-review.log`.
The populated-wrong-wallet rejection case passed in both funded scenarios
against the rebuilt wallet binary. The wrong wallet has a valid saved output
index but a different key image; rejection leaves the mempool empty and its
balance unchanged. The same encrypted artifact subsequently relays successfully
with the correct wallet, preserving both destination records after confirmation.
The full wallet build first hit stale generated Trezor protobuf files (regenerated
with the project generator), then a full `/tmp` partition. The failed build was
stopped and retried using a private temporary directory on the project drive.
Successful build log: `build/pending-input-wallet-build-final.log`.
Hardware-wallet support remained compiled in, but hardware-device and
cold/multisig compatibility beyond known complete key images is not verified.

Final acceptance: `build/pending-input-funded-acceptance-final.log`, exit zero;
89 library tests, three compile-fail tests and both funded scenarios passed.
The two runner unit tests and ordered patch reconstruction also passed.
Tested `monzero-wallet-rpc` SHA-256:
`f251ce6cb4d8cf6619e52270a5ad67d4ef71e7a3d91da858fff15dadb29ddedf`.
The first acceptance attempt did not reach funding: its long TMPDIR exceeded
the Unix peer socket path limit (`build/pending-input-bitcoin-startup-diagnostic.log`).
The final run used a short private TMPDIR on the same spacious home filesystem.
Do not count the earlier startup failure as a settlement result.
No public deployment, real-wallet migration or live-trading activation occurred.

## Sealed-context tamper coverage

The disposable AES-GCM comparison now binds format, wallet, genesis, funding
transaction, prepared transaction, payout destination/principal, reserve-return
destination/amount and network fee as associated data. Each field is mutated
independently; nonce modification, ciphertext modification and truncation are
also tested. Each rejection must be the child's explicit authentication-failure
exit, occurring before its RPC call—not merely any nonzero exit status.
The original artifact, source balance and empty mempool remain unchanged before
the subsequent correct-wallet success case.

Both funded scenarios passed in `build/sealed-context-tamper-acceptance.log`,
alongside 89 library tests, three compile-fail tests and patch reconstruction.
The two runner tests passed. Claude checked mutation coverage and argument
indexing in `build/claude-sealed-context-tamper-review.log`.
The fixture key remains parent-held; this does not validate a production vault,
whole-application restart, negotiated funding or a different encryption format.
Its disposable public accounting context is passed in argv; production context
must use the private native IPC path instead, since trade metadata can be private.
The owner unlock preference remains pending; neither option has been enabled.
