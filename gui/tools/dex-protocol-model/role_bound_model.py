"""Abstract BTC-first disclosure candidate; NOT consensus or signature code.

Both locks are assumed validated and funded. Three ordered time windows replace
actual chain clocks. Inclusion can be withheld indefinitely; no reorgs are modeled.
S initially knows the secret; B learns it from S's published BTC witness. Signature
authority is an explicit transition restriction, not a cryptographic verification.
"""
from collections import deque
from dataclasses import dataclass, replace


@dataclass(frozen=True)
class State:
    window: int = 0  # 0: before BTC refund; 1: BTC refund; 2: both refunds
    public_secret: bool = False
    btc: str = "locked"
    xmz: str = "locked"

    @property
    def loss(self):
        if self.btc == "refund" and self.xmz == "success":
            return "XMZ funder loses principal; fee collected on failed exchange"
        if self.btc == "success" and self.xmz == "refund":
            return "BTC funder loses principal; no XMZ fee collected"
        return None


@dataclass(frozen=True)
class Event:
    actor: str
    action: str


def successors(state, public_finalizer=False):
    if state.window < 2:
        yield Event("clock", "advance refund window"), replace(
            state, window=state.window + 1)
    if state.btc == "locked":
        if not state.public_secret:
            yield Event("S", "publish signed BTC success witness"), replace(
                state, public_secret=True)
        if state.public_secret:
            yield Event("S", "confirm BTC success"), replace(state, btc="success")
        if state.window >= 1:
            yield Event("B", "confirm BTC refund"), replace(state, btc="refund")
    if state.xmz == "locked":
        if state.public_secret:
            yield Event("B", "confirm fixed XMZ delivery plus fee"), replace(
                state, xmz="success")
            if public_finalizer:
                # Hypothetical constrained authority; never arbitrary destinations.
                yield Event("finalizer", "confirm fixed XMZ delivery plus fee"), replace(
                    state, xmz="success")
        if state.window == 2:
            yield Event("S", "confirm XMZ refund"), replace(state, xmz="refund")


def explore(public_finalizer=False):
    initial = State()
    traces = {initial: ()}
    pending = deque([initial])
    while pending:
        state = pending.popleft()
        for event, following in successors(state, public_finalizer):
            if following not in traces:
                traces[following] = traces[state] + (event,)
                pending.append(following)
    return traces


if __name__ == "__main__":
    examples = {}
    states = explore()
    for state, trace in states.items():
        if state.loss and state.loss not in examples:
            examples[state.loss] = trace
    print(f"Explored {len(states)} abstract states; not an on-chain test.")
    for loss, trace in examples.items():
        print(loss)
        for index, event in enumerate(trace, 1):
            print(f"  {index}. {event.actor}: {event.action}")
