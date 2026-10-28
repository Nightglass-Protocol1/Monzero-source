# Monzero DEX crypto verifier

This private prototype performs only two public-input verification operations:

- pinned `ecdsa_fun` ECDSA adaptor-signature verification;
- pinned `sigma_fun` secp256k1/Ed25519 cross-curve DLEQ verification.

It reads one bounded JSON request from standard input, writes one bound JSON
response to standard output, and exits. It contains no wallet integration,
private-key input, signer, secret recovery, network client, transaction builder,
or broadcaster. See `../../DEX_CRYPTO_VERIFIER_PROTOCOL.md` for the wire and trust
boundary.

The checked-in `Cargo.lock` is mandatory. The first isolated build/test used
Ubuntu image `ubuntu@sha256:33ceb71981b602c1a7443a53469e4dba065f7503eab3078a2d7a57a2ab987517`,
Ubuntu's Rust/Cargo 1.75 packages, and `cargo test --locked` followed by
`cargo build --release --locked`. Four tests cover the pinned crate's published
DLC valid/invalid adaptor-vector pair, a deterministically generated adaptor
signature, a generated 252-commitment cross-curve proof, request mutation, and
trailing proof rejection. The vector provenance and 0BSD notice are retained in
`THIRD_PARTY_NOTICES.md`.

The resulting local test binary is deliberately outside the GUI source tree at
`../../../build/dex-crypto-verifier/monzero-dex-crypto-verifier` and has SHA-256
`eda07a44e9ce9dd2d5a3a3265081e266ea4da73350921a9c64c7f5a81ee6d9a5`.
This is build evidence, not a distributable or independently reproduced artifact.
Two clean locked builds on this same host/container image produced the same hash;
that establishes local repeatability, not independent reproduction.

The test-only `emits_local_cross_curve_regression_fixture` generator now emits
the deterministic DLEQ proof used by the C++ success-path regression at
`src/dex/tests/fixtures/local-cross-curve-dleq.json`. It uses the existing fixed
test scalar and RNG seed and never enters the production verifier executable.
The fixture is locally generated, not an independently sourced cryptographic
vector. The C++ test verifies it through the hash-pinned sandboxed binary, checks
the opaque evidence commitment and rejects a mutated proof. Run that test with
`MONZERO_DEX_CRYPTO_VERIFIER` pointing to the pinned executable; without it the
cryptographic portion is explicitly skipped. This does not close the independent
vector or independent reproduction gates.
