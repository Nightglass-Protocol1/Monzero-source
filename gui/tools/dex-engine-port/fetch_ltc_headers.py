#!/usr/bin/env python3
"""Export a Litecoin node's block headers to a flat file (80 bytes each, height 0 first).

Used by the light-wallet testnet harness, which serves headers to the wallet
the way an Electrum server would. Resumes where an earlier run stopped.

    fetch_ltc_headers.py --rpc-port 19332 --cookie COOKIE --out headers.bin [--height N]
"""
import argparse, base64, json, pathlib, time, urllib.request

BATCH = 2000


def batch(port, cookie, calls):
    auth = base64.b64encode(pathlib.Path(cookie).read_bytes().strip()).decode()
    body = json.dumps([{"jsonrpc": "1.0", "id": i, "method": m, "params": p} for i, (m, p) in enumerate(calls)]).encode()
    for attempt in range(6):
        try:
            request = urllib.request.Request(f"http://127.0.0.1:{port}", body,
                                             {"Content-Type": "application/json", "Authorization": "Basic " + auth})
            with urllib.request.urlopen(request, timeout=120) as response:
                replies = sorted(json.load(response), key=lambda r: r["id"])
            if any(r.get("error") for r in replies):
                raise RuntimeError(next(r["error"] for r in replies if r.get("error")))
            return [r["result"] for r in replies]
        except (OSError, ValueError) as error:  # busy node: retry reads
            if attempt == 5:
                raise
            time.sleep(2 * (attempt + 1))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--rpc-port", type=int, required=True)
    ap.add_argument("--cookie", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--height", type=int, help="last height to export (default: current tip - 6)")
    a = ap.parse_args()
    out = pathlib.Path(a.out)
    tip = a.height if a.height is not None else batch(a.rpc_port, a.cookie, [("getblockcount", [])])[0] - 6
    done = out.stat().st_size // 80 if out.exists() else 0
    out.parent.mkdir(parents=True, exist_ok=True)
    started, previous = time.time(), None
    with out.open("ab") as f:
        f.truncate(done * 80)
        for start in range(done, tip + 1, BATCH):
            heights = list(range(start, min(start + BATCH, tip + 1)))
            hashes = batch(a.rpc_port, a.cookie, [("getblockhash", [h]) for h in heights])
            headers = batch(a.rpc_port, a.cookie, [("getblockheader", [h, False]) for h in hashes])
            chunk = b"".join(bytes.fromhex(h) for h in headers)
            assert len(chunk) == 80 * len(heights)
            if previous is not None:  # each header must commit to the one before it
                assert chunk[4:36] == previous, f"header chain broken at {start}"
            f.write(chunk)
            import hashlib
            previous = hashlib.sha256(hashlib.sha256(chunk[-80:]).digest()).digest()
            if start // BATCH % 100 == 0:
                rate = (start - done + len(heights)) / max(time.time() - started, 1)
                print(json.dumps({"height": heights[-1], "tip": tip, "headersPerSecond": int(rate)}), flush=True)
    print(json.dumps({"done": True, "headers": tip + 1, "bytes": (tip + 1) * 80, "seconds": int(time.time() - started)}))


if __name__ == "__main__":
    main()
