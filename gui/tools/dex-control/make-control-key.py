#!/usr/bin/env python3
"""Create the DEX operator control signing key (run by the operator, offline).

Writes <dir>/control-signing.key (hex seed, mode 0600) and prints the public key
to pass to CMake as -DMONZERO_DEX_CONTROL_PUBKEY=<hex>. Refuses to overwrite.
Keep the signing key offline; anyone holding it can pause or resume new trades
and change limits for every user of builds that pin its public key.
"""
import os, sys
import nacl.signing

def main():
    if len(sys.argv) != 2:
        sys.exit("usage: make-control-key.py <private-directory>")
    directory = sys.argv[1]
    os.makedirs(directory, mode=0o700, exist_ok=True)
    path = os.path.join(directory, "control-signing.key")
    fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    key = nacl.signing.SigningKey.generate()
    with os.fdopen(fd, "w") as out:
        out.write(bytes(key).hex())
    print(key.verify_key.encode().hex())

if __name__ == "__main__":
    main()
