# Transaction-bound XMZ signing: compatibility checkpoint

Status: research only. No signing or broadcast authority is added.
This follows [the unresolved mandatory-fee requirement](DEX_FEE_ENFORCEMENT.md).

## Inspected local implementation

Core baseline: `58f10f796` (inspect working-tree changes before reproduction).

- `src/multisig/multisig_clsag_context.h`: the CLSAG context binds the ring,
  commitments, message, key images, responses and combined nonces. It computes
  challenges and coefficient values, not an encrypted-signature protocol.
- `src/multisig/multisig_tx_builder_ringct.cpp`: `first_partial_sign`,
  `next_partial_sign` and `finalize_tx` construct ordinary multisig responses.
  Their inspected interfaces do not define adaptor verification, adaptation or
  extraction. A partial signature must not be relabelled an adaptor signature.
- `src/wallet/wallet_rpc_server.cpp`: `on_sign_multisig` delegates to wallet
  multisig signing; it does not expose conditional secret-release semantics.
- GUI `tools/dex-crypto-verifier/src/main.rs`: the pinned verifier implements
  ECDSA adaptor verification and cross-curve DLEQ verification only. Neither
  operation proves an encrypted CLSAG signature valid.

This is a review of those interfaces, not a proof that no compatible construction
can exist. No wallet keys were loaded or signing RPCs invoked.

## Reference limitation

[Atomic Swaps between Bitcoin and Monero, sections 4.2 and 4.4](https://arxiv.org/html/2101.12332)
sketches Monero adaptor signatures for a refund path and explicitly leaves their
cryptographic construction outside its scope. Its happy path still discloses
the seller's spend share. Consequently, our inference is that importing that
sketch alone does not resolve the fee-bypass problem identified here.

## Research deliverable before implementation

Specify a transaction-bound conditional signing protocol for the current CLSAG
format, with independently reproducible verification/adaptation/extraction
vectors. Identify the signer, recipient and exact authority of each secret;
the adaptor secret must not accidentally confer unrestricted XMZ spending.
Prove the delivery and fee destinations are bound to the signature message,
including amount commitments and native fees, rather than just a JSON description.

Define nonce generation, commitment exchange, one-time use, durable consumption
before disclosure, concurrent-session separation and crash recovery. Cover all
inputs and ring members, malformed points, replayed sessions, changed destination
commitments and malicious partial signatures. Do not experiment with real wallet
nonces or keys.

Finally, integrate the construction into a complete two-chain state machine and
prove fee-free refunds, secret-exposure races, reorg behavior and offline recovery.
Ordinary 2-of-2 signing alone leaves a counterparty able to withhold cooperation;
it is not an atomic refund mechanism. Any consensus change, third-party trust or
custody would be a separate design decision, not an implicit workaround.

The existing wire protocol does not admit CLSAG or signing operations. Adding a
new operation requires a reviewed specification, versioned encoding, pinned
implementation and independent vectors; changing an operation string is not
feature implementation.

## Candidate screening (not exhaustive)

| Candidate | Evidence inspected | Integration decision |
| --- | --- | --- |
| [LTRAS, arXiv 2602.05431v1](https://arxiv.org/html/2602.05431v1) | A distinct linkable threshold ring adaptor construction; section 5 reports Java simulation benchmarks. | Research input only. The inspected material does not demonstrate signatures accepted by the current Monzero CLSAG verifier. A claimed Monero application is not a byte-format compatibility test. |
| [Nazgul 1.0.0 documentation](https://docs.rs/nazgul/1.0.0/nazgul/) | Documents ordinary ring-signature schemes on Ristretto with selectable 512-bit hashing. | Not a drop-in Monzero CLSAG adaptor implementation. Curve representation, hashing and transaction integration require explicit compatibility evidence. |
| [Monero CLSAG audit announcement](https://web.getmonero.org/2020/07/31/clsag-audit.html) | Reports review of the ordinary CLSAG construction and implementation. | Does not certify a new adaptor extension or our cross-chain fee protocol. |

These are bounded screening results, not proof that a suitable construction is
impossible or that all available implementations have been examined. No external
code was imported and no dependency or consensus rule was changed.

For a candidate to advance, require a pinned implementation and a positive test
in which its adapted signature verifies under this fork's existing transaction
rules. Pair that with altered-message rejection and extraction tests; then test
the complete transaction, output commitments, fee destination, input ownership
and refund protocol. A standalone mathematical signature test cannot substitute
for that end-to-end evidence. If the construction needs new consensus rules,
stop and request a separate chain-design decision rather than changing the chain
as an incidental DEX implementation step.
