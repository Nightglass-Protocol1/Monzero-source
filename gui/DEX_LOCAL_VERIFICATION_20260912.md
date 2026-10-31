# Local DEX verification, 2026-09-12

GUI source baseline: `8ea4c5c63641e4ed9af253249b8b516b8e6e663e`, with
uncommitted design documents/models and unrelated pre-existing scratch files.
This is local verification, not an independent reproduction or release approval.

- Rebuilt `monzero-wallet-gui` and every `dex_*` executable declared with
  `add_executable` in `src/CMakeLists.txt`, using `build-dex-private`, `-j2`.
- Ran all 42 `^dex_` CTest suites: 42 passed, zero failed, 127.48 seconds.
- Inspected `Testing/Temporary/LastTest.log`: no `SKIP` or `FAIL!` markers.
- Bitcoin regtest, Monzero fakechain and pinned cryptographic helper paths were
  explicitly supplied; these optional checks were not omitted by missing env vars.
- Inspected `DexEngine::activationReadiness`: `ready` and
  `feeEnforcementVerified` remain false with explicit settlement blockers.

Local logs: `build-dex-private/goal-rebuild.log`,
`build-dex-private/goal-tests.log`, and
`build-dex-private/Testing/Temporary/LastTest.log` (subsequent runs may replace them).

Rebuilt GUI SHA-256:
`c02c038289b490c2041bf24cfcea032fcda8320ec9b62de53352e534845b405f`.
This hash identifies this local executable, not a signed public package.

Test environment:

```sh
MONZERO_DEX_BITCOIN_BIN=/home/pinhead/Projects/Monero-Fork/build/local-bitcoin-lab-31.1/bitcoin-31.1/bin \
MONZERO_DEX_MONZEROD=/home/pinhead/Projects/Monero-Fork/build/bin/monzerod \
MONZERO_DEX_CRYPTO_VERIFIER=/home/pinhead/Projects/Monero-Fork/build/dex-crypto-verifier/monzero-dex-crypto-verifier \
ctest --test-dir build-dex-private -R '^dex_' --output-on-failure
```

The abstract protocol-model suite separately passed 17 tests in the preceding
design step. Those tests retain counterexamples and conditional timing assumptions;
they do not implement or prove cross-chain settlement.

Still missing: verified fee-enforced atomic settlement/refunds, compatible signing
construction or separately approved consensus implementation, external/external
three-leg protocol, independent review and release validation. No website update,
production-node change, wallet replacement or deployment was performed here.
