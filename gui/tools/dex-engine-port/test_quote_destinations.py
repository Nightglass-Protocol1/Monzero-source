#!/usr/bin/env python3
"""Exercise the real destination-only helper pipes; no wallets, RPC or funds."""
import argparse
import json
import pathlib
import subprocess
import uuid


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--helper", required=True, type=pathlib.Path)
    parser.add_argument("--vectors", required=True, type=pathlib.Path)
    args = parser.parse_args()
    xmz = args.vectors.read_text().splitlines()[0]
    request = dict(schema="monzero-native-destinations-request-v1",
                   requestId=str(uuid.uuid4()), quoteCommitment="a" * 64,
                   btcPayout="1BoatSLRHtKNngkdXEeobR76b53LETtpyT",
                   btcRefund="1BoatSLRHtKNngkdXEeobR76b53LETtpyT",
                   xmzPayout=xmz, xmzRefund=xmz)

    def encode(value):
        return json.dumps(value, sort_keys=True, separators=(",", ":")).encode()

    def run(data, btc="mainnet", network="mainnet"):
        return subprocess.run([str(args.helper.resolve()), "--quote-destinations-stdio-v1",
                               "--bitcoin-network", btc, "--xmz-network", network],
                              input=data, capture_output=True, timeout=15, check=False)

    encoded = encode(request)
    result = run(encoded)
    assert result.returncode == 0 and not result.stderr, "valid destination command failed"
    reply = json.loads(result.stdout)
    expected = {key: value for key, value in request.items() if key != "schema"}
    expected.update(schema="monzero-native-destinations-v1", valid=True, executable=False)
    assert reply == expected and result.stdout == encode(reply), "unbound destination reply"
    invalid = [encoded + b" ", b"x" * 2049, encode(dict(request, unexpected=True)),
               b'{"schema":"ignored",' + encoded[1:]]
    for key in ("btcPayout", "btcRefund", "xmzPayout", "xmzRefund"):
        invalid.append(encode(dict(request, **{key: "invalid"})))
    results = [run(data) for data in invalid]
    results += [run(encoded, btc="regtest"), run(encoded, network="testnet")]
    for rejected in results:
        assert rejected.returncode != 0 and not rejected.stdout, "invalid input produced evidence"
        assert rejected.stderr == b"Native destination check failed\n", "unexpected diagnostic"
    print("PASS: real helper destination pipes bind replies and reject invalid addresses/networks/frames")


if __name__ == "__main__":
    main()
