#!/usr/bin/env python3
"""Compare selected native integration sources; not a full consensus/build audit."""
import argparse
import hashlib
import pathlib
import sys

SOURCES = (
    "src/cryptonote_config.h",
    "src/cryptonote_basic/hardfork.cpp",
    "src/cryptonote_basic/cryptonote_basic_impl.cpp",
    "src/wallet/wallet2.cpp",
    "src/wallet/wallet_rpc_server.cpp",
    "src/wallet/wallet_rpc_server_commands_defs.h",
)


def check(core, gui_core):
    failures = []
    for source in SOURCES:
        try:
            paths = [base / source for base in (core, gui_core)]
            if any(not path.is_file() or path.is_symlink() for path in paths):
                raise ValueError("missing or symlinked source")
            contents = [path.read_bytes() for path in paths]
            if contents[0] != contents[1]:
                failures.append(source)
                print("DIFFER " + source)
            else:
                print("MATCH " + source + " sha256=" + hashlib.sha256(contents[0]).hexdigest())
        except (OSError, ValueError):
            failures.append(source)
            print("UNAVAILABLE " + source)
    return not failures


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--core", type=pathlib.Path, required=True)
    parser.add_argument("--gui-core", type=pathlib.Path, required=True)
    args = parser.parse_args()
    sys.exit(0 if check(args.core.resolve(strict=True), args.gui_core.resolve(strict=True)) else 1)
