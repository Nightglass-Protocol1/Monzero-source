"""Bounded abstract key-release model, NOT a Monzero cryptographic verifier.

Assumptions: both principal locks exist; BTC redemption grants the XMZ buyer
complete spend authority; that authority permits choosing XMZ outputs. Native
fees, chain validity, refunds and reorgs are deliberately outside this model.
"""
from collections import deque
from dataclasses import dataclass, replace


@dataclass(frozen=True)
class State:
    btc_delivered: bool = False
    buyer_has_full_xmz_authority: bool = False
    xmz_delivered: bool = False
    protocol_fee_paid: bool = False

    @property
    def successful_exchange(self):
        return self.btc_delivered and self.xmz_delivered


def successors(state):
    if not state.btc_delivered:
        yield "seller redeems BTC, exposing spend share", replace(
            state, btc_delivered=True, buyer_has_full_xmz_authority=True)
    if state.buyer_has_full_xmz_authority and not state.xmz_delivered:
        yield "buyer spends XMZ with fee", replace(
            state, xmz_delivered=True, protocol_fee_paid=True)
        yield "buyer spends XMZ without fee", replace(state, xmz_delivered=True)


def explore():
    """Enumerate every reachable state once and retain a shortest witness trace."""
    initial = State()
    traces = {initial: ()}
    pending = deque([initial])
    while pending:
        current = pending.popleft()
        for event, following in successors(current):
            if following not in traces:
                traces[following] = traces[current] + (event,)
                pending.append(following)
    return traces


def counterexamples():
    return [(state, trace) for state, trace in explore().items()
            if state.successful_exchange and not state.protocol_fee_paid]


if __name__ == "__main__":
    for _, trace in counterexamples():
        print("Abstract fee-enforcement counterexample (not a chain execution):")
        for step, event in enumerate(trace, 1):
            print(f"{step}. {event}")
