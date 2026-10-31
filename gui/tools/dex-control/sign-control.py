#!/usr/bin/env python3
"""Sign a DEX operator control document (kill switch and new-trade limits).

  sign-control.py KEYFILE --network ltc-testnet --sequence N --days D \
      --enabled|--disabled --max-ltc-per-trade ATOMS --max-xmz-per-trade ATOMS \
      --max-ltc-per-day ATOMS > control.json

Sequence must increase with every document: the app ignores older sequences,
so a replayed "enabled" document cannot undo a later stop. Validity is at most
30 days; on testnet/mainnet an expired document pauses new trades.
"""
import argparse, json, time
import nacl.signing

def main():
    p = argparse.ArgumentParser()
    p.add_argument("keyfile")
    p.add_argument("--network", required=True, choices=["ltc-regtest", "ltc-testnet", "ltc-mainnet"])
    p.add_argument("--sequence", type=int, required=True)
    p.add_argument("--days", type=float, default=7)
    state = p.add_mutually_exclusive_group(required=True)
    state.add_argument("--enabled", action="store_true")
    state.add_argument("--disabled", action="store_true")
    for name in ("max-ltc-per-trade", "max-xmz-per-trade", "max-ltc-per-day"):
        p.add_argument("--" + name, type=int, required=True)
    a = p.parse_args()
    if not 0 < a.days <= 30 or a.sequence < 0:
        raise SystemExit("days must be in (0, 30] and sequence non-negative")
    now = int(time.time())
    payload = {
        "schema": "monzero-dex-control-v1", "network": a.network, "sequence": a.sequence,
        "issuedAt": now, "expiresAt": now + int(a.days * 86400), "newTradesEnabled": bool(a.enabled),
        "maxLtcAtomsPerTrade": str(a.max_ltc_per_trade), "maxXmzAtomsPerTrade": str(a.max_xmz_per_trade),
        "maxLtcAtomsPerDay": str(a.max_ltc_per_day),
    }
    body = json.dumps(payload, sort_keys=True, separators=(",", ":"))
    with open(a.keyfile) as f:
        key = nacl.signing.SigningKey(bytes.fromhex(f.read().strip()))
    signature = key.sign(b"monzero-dex-control-v1\0" + body.encode()).signature.hex()
    print(json.dumps({"payload": body, "signature": signature}, sort_keys=True, separators=(",", ":")), end="")

if __name__ == "__main__":
    main()
