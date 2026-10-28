#!/usr/bin/env python3
"""Opt-in anonymous mining telemetry reporter for monzero.org."""

from __future__ import annotations

import argparse
import json
import os
import re
import stat
import time
import urllib.error
import urllib.request
import uuid
from pathlib import Path


FOUND_BLOCK = re.compile(r"Found block <?[0-9a-f]+>? at height ", re.IGNORECASE)


def json_request(url: str, body: dict | None = None, token: str = "") -> dict:
    data = None if body is None else json.dumps(body).encode("utf-8")
    headers = {"Accept": "application/json"}
    if data is not None:
        headers["Content-Type"] = "application/json"
    if token:
        headers["Authorization"] = f"Bearer {token}"
    request = urllib.request.Request(url, data=data, headers=headers)
    with urllib.request.urlopen(request, timeout=8) as response:
        return json.load(response)


def installation_id(path: Path) -> str:
    if path.exists():
        return path.read_text(encoding="ascii").strip()
    path.parent.mkdir(parents=True, exist_ok=True)
    value = str(uuid.uuid4())
    path.write_text(value + "\n", encoding="ascii")
    path.chmod(stat.S_IRUSR | stat.S_IWUSR)
    return value


def blocks_found(log_path: Path) -> int:
    try:
        with log_path.open("r", encoding="utf-8", errors="ignore") as log:
            return sum(1 for line in log if FOUND_BLOCK.search(line))
    except FileNotFoundError:
        return 0


def report(args: argparse.Namespace) -> None:
    status = json_request(args.rpc.rstrip("/") + "/mining_status", {})
    payload = {
        "installation_id": installation_id(args.id_file),
        "hashrate": int(status.get("speed", 0)) if status.get("active") else 0,
        "blocks_found": blocks_found(args.log),
    }
    result = json_request(args.endpoint, payload, args.token)
    if result.get("status") != "OK":
        raise RuntimeError(f"telemetry rejected: {result}")
    print(
        f"reported {payload['hashrate']} H/s and "
        f"{payload['blocks_found']} block(s); no wallet or hardware ID sent"
    )


def main() -> None:
    default_data = Path.home() / ".monzero"
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--endpoint", default="https://monzero.org/api/miner-stats/")
    parser.add_argument("--rpc", default="http://127.0.0.1:6175")
    parser.add_argument("--token", default=os.environ.get("MONZERO_STATS_TOKEN", ""))
    parser.add_argument("--id-file", type=Path, default=default_data / "telemetry-id")
    parser.add_argument("--log", type=Path, default=default_data / "monzero.log")
    parser.add_argument("--interval", type=int, default=0, help="repeat every N seconds")
    args = parser.parse_args()
    if not args.token:
        parser.error("set MONZERO_STATS_TOKEN or pass --token")
    if args.interval and args.interval < 30:
        parser.error("--interval must be at least 30 seconds")

    while True:
        try:
            report(args)
        except (OSError, ValueError, urllib.error.URLError, RuntimeError) as error:
            print(f"report failed: {error}")
        if not args.interval:
            break
        time.sleep(args.interval)


if __name__ == "__main__":
    main()
