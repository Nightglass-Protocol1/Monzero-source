# Monzero DEX crypto-verifier protocol

Status: validation-only V1 boundary. No implementation of this interface is
authorized to hold private keys, sign, decrypt adaptor signatures, recover swap
secrets, construct transactions, broadcast, or set `executable=true`.

## Pinned implementation profile

The initial interoperability profile is identified byte-for-byte as:

`comit-dc6ba84bbb1fe5ecc69581fec7dd8529567c4e32:ecdsa_fun-0.10.0:sigma_fun-0.7.0:bincode-1.3.3`

This names the inspected COMIT source revision and the exact serialization and
cryptography crate versions in its lockfile. An upgrade is a protocol change and
requires new independently generated vectors; accepting a compatible-looking
unversioned object is prohibited.

## Operations

`verify_ecdsa_adaptor` accepts a 32-byte Bitcoin digest, distinct compressed
secp256k1 signing/encryption keys, and the exact 162-byte `ecdsa_fun` encrypted
signature encoding.

`verify_cross_curve_dleq` accepts a compressed secp256k1 public key, a 32-byte
Ed25519 encoding, and an opaque proof for the pinned `sigma_fun` profile. Proof
deserialization, canonical reserialization, point/subgroup checks, the 252-pair
commitment count, transcript verification, and trailing-byte rejection belong in
the verifier implementation. The GUI does not interpret the proof. Before invoking
the verifier, it requires the proof to match the exact 32-byte commitment signed in
V6 settlement terms:

`SHA256("MONZERO-DEX-DLEQ-PROOF-V1\\n" || len32be(implementation) || implementation || len32be(proof) || proof)`

Every request contains a caller-generated 32-byte nonce and a domain-separated
SHA-256 commitment over the implementation, operation, nonce, and length-prefixed
binary inputs. Every response must echo all four values, carry a literal Boolean
result, use an empty error on success or a constrained machine-readable error code
on failure, and contain literal `executable=false`. Unknown response fields are
rejected.

## Prototype verifier

`tools/dex-crypto-verifier` now implements both verification operations using the
pinned libraries and a locked dependency graph. Deterministic Rust tests generate
and verify a real adaptor signature and a real 252-commitment cross-curve proof;
they also reject commitment mutation and trailing proof bytes. A C++/Rust process
test proves both sides calculate the same request commitment and accept the same
response envelope. The locally built binary is private test evidence only.
The pinned crate's published DLC adaptor vectors are also exercised: its valid
fixture verifies and its deliberately wrong-proof fixture fails. Two clean locked
builds on one host produced the same binary hash, which is repeatability evidence
but not independent reproduction.

## Trust boundary still required

`CryptoVerifierProtocol` alone intentionally returns `responseAuthenticated=false`.
`CryptoVerifierClient` may elevate that flag only after matching the configured
SHA-256, requiring a non-writable regular executable owned by the current user,
launching it as a direct Bubblewrap child with an empty environment, no network,
no home/wallet mount, no capabilities and no nested user namespaces, enforcing
bounded time and I/O, accepting no stderr, rechecking the binary hash, and
validating the bound response. Wrong hashes, writable binaries, missing sandbox
support and abnormal processes fail closed.

Independent reproduction and externally sourced cross-curve vectors remain
required before a positive result may influence settlement. Self-generated DLEQ
vectors establish internal consistency, not independent cryptographic correctness.
Even an authenticated result remains `executable=false`.

`CrossCurveProofEvidence` closes the caller-side binding gap: it rebuilds valid,
unexpired V6 settlement terms, selects the signed maker or taker cross-curve
identity, requires a fresh request nonce distinct from the settlement nonce, and
requires the supplied proof bytes to reproduce that party's signed proof
commitment before invoking the sandbox. Its result carries the settlement, proof,
and request commitments for journaling. Authentication or successful verification
still cannot set `executable=true`.
