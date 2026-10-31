"""Counterexample search for a hypothetical hash-conditioned XMZ output.

NOT a consensus implementation. Both locks already exist. Publishing the XMZ
success witness exposes the BTC redemption secret before XMZ confirmation.
XMZ success pays delivery and fee together; XMZ refund pays neither. At the
XMZ deadline, an unconfirmed success may lose to refund. BTC redemption is
assumed still available during this interval. These assumptions are the design
being screened, not facts established about a deployed protocol.
"""
from collections import deque
from dataclasses import dataclass, replace


@dataclass(frozen=True)
class State:
    secret_public: bool = False
    xmz_deadline_reached: bool = False
    xmz_outcome: str = "locked"
    btc_redeemed: bool = False

    @property
    def fee_paid(self):
        return self.xmz_outcome == "success"

    @property
    def atomicity_violation(self):
        return self.btc_redeemed and self.xmz_outcome == "refund"


def successors(state):
    if state.xmz_outcome == "locked":
        if not state.secret_public and not state.xmz_deadline_reached:
            yield "publish XMZ success witness (not confirmed)", replace(
                state, secret_public=True)
        if state.secret_public:
            yield "confirm XMZ delivery and fee together", replace(
                state, xmz_outcome="success")
        if state.xmz_deadline_reached:
            yield "confirm XMZ refund without protocol fee", replace(
                state, xmz_outcome="refund")
    if not state.xmz_deadline_reached:
        yield "XMZ refund deadline passes", replace(state, xmz_deadline_reached=True)
    if state.secret_public and not state.btc_redeemed:
        yield "redeem BTC using public secret", replace(state, btc_redeemed=True)


def explore():
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
            if state.atomicity_violation]


if __name__ == "__main__":
    for _, trace in counterexamples():
        print("Abstract cross-chain loss counterexample (not a chain execution):")
        for index, event in enumerate(trace, 1):
            print(f"{index}. {event}")
