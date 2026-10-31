# Abstract settlement models

## Buyer-funded network reserve

`test_buyer_reserve.py` enumerates small integer allocations after unrestricted
shared-key recovery. A shared buyer-funded reserve admits both honest return and
seller retention on refund; a local signed-destination check does not remove the
alternative spend from the key owner's authority. Passing this test preserves
the counterexample, not a proof that reserve return is enforced.
Keeping the reserve under a separate buyer-only key removes that particular
modeled authority, but does not implement joint-input spending or refund fees.
The model has fixed fees and no real signatures, transactions or chain behavior.

Run `python3 -m unittest discover -s tools/dex-protocol-model -v` from the GUI
repository. `python3 tools/dex-protocol-model/fee_release_model.py` prints the
shortest fee-bypass trace under its stated assumptions.

The key-release model exhaustively enumerates four abstract states. It captures
both an honest fee-paying delivery and a successful fee-free delivery after the
buyer receives unrestricted XMZ spending authority. It does not compute keys,
signatures, amounts or transactions. Thus it is not an exploit demonstration,
consensus test or independent proof of a real implementation's behavior.

The tests deliberately expect the counterexample. Passing them means the model
still exposes the unresolved problem, not that the DEX is safe or complete.
Do not remove the malicious transition merely because the honest client would
not choose it. A replacement construction must justify why the authority it
releases cannot perform that transition, then test real cryptographic/transaction
artifacts. Refunds, reorgs and third-party collusion need separate modeling and
end-to-end evidence; no conclusion about them follows from this model.

## Conditional-output timing model

`python3 tools/dex-protocol-model/conditional_output_model.py` searches a
hypothetical success/refund output whose public success witness also authorizes
BTC redemption. Success enforces the XMZ fee locally, yet its unconfirmed witness
can expose the secret before the output refunds. The four-step counterexample
has BTC redeemed and XMZ refunded: local fee enforcement alone is insufficient.

The model assumes BTC redemption remains available at the XMZ refund boundary.
It allows success/refund competition at that boundary and does not simulate
Bitcoin deadlines, blocks, cryptography, amounts, fees, reorgs or fair scheduling.
It is a counterexample to these particular assumptions, not an impossibility
proof for all conditional outputs or a vulnerability in running Monzero code.
The next design must specify the actual deadline ordering and who may publish
which witness, including how a refund remains safe after public secret exposure.
Merely having the honest client wait for confirmations does not revoke a
malicious observer's knowledge. Passing tests intentionally preserve this loss
trace; they do not authorize activation.

## Role-bound BTC-first disclosure

`python3 tools/dex-protocol-model/role_bound_model.py` screens the
[candidate transcript](../../DEX_CONDITIONAL_TRANSCRIPT.md). S funds XMZ, B funds
BTC, S reveals the secret through BTC, and only the designated actor may exercise
each modeled spend. Three ordered windows distinguish the two refund deadlines.
Both terminal loss directions remain reachable without an inclusion bound: BTC
refund plus XMZ delivery/fee, and BTC delivery plus XMZ refund/no fee.

The optional constrained-finalizer transition adds another way to confirm fixed
XMZ delivery, but cannot force inclusion. Tests verify that adding this permission
alone does not remove either adverse outcome. No fair scheduler, numerical delay
bound, chain clock, reorganization, signature, funding protocol or on-chain
confidential-output proof is modeled. In particular, this model does not refute
conditional safety under a sufficiently strong and justified inclusion bound.

The result changes the next design question: specify and justify that bound and
an enforceable witness-release policy, rather than treating deadline ordering or
an optional finalizer as a complete fix. Fee collection on a failed overall trade
remains a first-class failure, not merely an accounting inconvenience.
