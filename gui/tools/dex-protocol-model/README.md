# Abstract settlement models

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
