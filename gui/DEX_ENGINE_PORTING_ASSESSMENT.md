# BTC/XMZ engine port: compatibility findings

Date: 2026-09-12. Research against eigenwallet core
`0e17c7f7cd8f0657af176c8852aa4c9949586051`; local Monzero core
`58f10f7969b05a12d2e6e733ea5a8883e8b35eee`.
Scope follows [the approved zero-app-fee first release](DEX_FIRST_RELEASE_SCOPE.md).
No upstream code was built, linked, executed or connected to a wallet in this review.

## Confirmed obstacles, not just ticker changes

| Boundary | Observed upstream behavior | Required Monzero adaptation |
| --- | --- | --- |
| Amounts | `swap-core/src/monero/primitives.rs` defines `PICONERO_OFFSET = 1_000_000_000_000` and uses it for rate conversion | XMZ has 11 decimals (`100_000_000_000` atoms/coin). Preserve integer atom counts, replace asset-specific display/rate conversions, and reject excess precision |
| Fee reserve | The same file reserves `Amount::from_pico(3_000_000_000)` using Monero-specific rationale | Those atoms are 0.03 XMZ, not 0.003 XMZ. Derive estimates from XMZ policy and transaction construction, not relabelled XMR heuristics |
| Address generation | `monero-wallet/src/wallets.rs::swap_wallet_spendable` constructs `MoneroAddress::new(self.network, Legacy, ...)` | Generate/validate XMZ prefixes and network identity with positive and wrong-chain negative fixtures |
| Native wallet | `Wallets` reexports `monero_sys::WalletHandle`; `monero-sys/build.rs` compiles its bundled C++ core and CXX bridge | Review the bridge and patches against a disposable copy of the current XMZ core; ABI/API compatibility is unproved |
| Recovery scan | The spendable swap wallet imports the lock transaction using `scan_transaction` and configures restore height | Prove scanning, output ownership, spendability and recovery under XMZ transaction rules; daemon reachability is insufficient |
| Regtest mode | `Wallets` calls `unsafe_prepare_for_regtest`; environment uses a Monero mainnet address network for regtest | Isolate this behavior behind explicit fakechain/genesis validation; never reuse it for production |

The amount mismatch is a concrete compatibility hazard, not an observed transfer
loss. One XMZ is 100,000,000,000 atoms. Interpreting that integer using the upstream
display scale shows 0.1 coin; parsing a displayed “1” at that upstream scale yields
1,000,000,000,000 atoms, which would be 10 XMZ. Do not globally divide all amounts
by ten: raw RPC atoms must retain their meaning, and display/rate boundaries must
be identified individually. Bitcoin amounts remain satoshis.

## Build isolation requirement

The upstream `.gitmodules` points `monero-sys/monero` at a separate Monero fork.
Its build script groups embedded patches and reconstructs patched files from
their committed contents. **Do not symlink that submodule to our working core.**
Use a disposable isolated checkout with pinned dependencies; review every patch
before application and preserve Monzero genesis/consensus differences. Running
an unmodified upstream executable is not an XMZ compatibility test.

## Next implementation slice

**Checkpoint update:** the detailed chronological evidence now lives in
[the engine-port README](tools/dex-engine-port/README.md). Exact XMZ amount/rate
and address boundaries compile inside the isolated engine; native fakechain
recovery and Bitcoin regtest redeem/full-refund transactions have been joined
through an opt-in trusted fixture coordinator. The combined redeem test now
carries actual native transfer evidence verified by wallet RPC. These results
supersede the initial standalone-only status below, but do not establish a
production wallet adapter. Next replace test coordination with an engine-owned
native funding verification interface, preserving network identity, exact atom
amounts, confirmation/reorg checks and durable recovery state. Do not treat the
fixture's JSON record as independently verified peer evidence. A reusable engine
verifier now performs native observations through the combined fixture's own RPC
adapter; both combined paths pass through it. The native read-only observer has
now moved into `swap_machine::native_funding_rpc`, with mandatory credentials and
loopback-only endpoints. Its use remains experimental and fixture-validated;
the GUI does not yet invoke it. Bitcoin transport and credential-file loading
remain in test support.
Production lifecycle integration and durable verified-state recovery are next,
not treating a point-in-time verification result as permanent authorization.

Native API contract check `tools/dex-engine-port/wallet_api_contract.cpp` now
compiles against the current core header with warnings treated as errors. It
checks eight required method signatures without opening any wallet. Upstream's
[bridge declarations](https://github.com/eigenwallet/core/blob/0e17c7f7cd8f0657af176c8852aa4c9949586051/monero-sys/src/bridge.rs)
also include reorg/pool-removal event callbacks and pending raw-transaction
accessors not established by this check. The inspected local public listener
does not declare corresponding dedicated reorg/pool-removal callbacks. Therefore
the entire upstream bridge cannot yet be assumed source/ABI compatible.

The local wallet RPC exposes `scan_tx` and `generate_from_keys`, but `scan_tx`
can reject rescanning recent transactions against an untrusted daemon. A port
must not globally mark arbitrary public daemons trusted to bypass that check.
Runtime recovery tests need a controlled local fakechain daemon and disposable
wallet data, with explicit rescan/trust semantics.

Initial amount boundary is now in `tools/dex-engine-port/xmz_amount.rs`, with
unsigned atom-string transport and exact 11-decimal parsing. It is standalone,
not linked into upstream; all four standalone tests now pass with the locally
extracted Rust 1.95.0 compiler (see its README for the reproduction command).
The upstream `monero-oxide-ext/src/lib.rs` confirms its amount is a transparent
`u64` with 12-decimal parsing/formatting and a JavaScript-number type annotation.
That combination motivates explicit string transport at the new bridge boundary.

Local `src/wallet/api/wallet2_api.h` exposes `scanTransactions` and
`setRefreshFromBlockHeight`. This narrows the recovery API gap but does not prove
upstream bridge compatibility or equivalent scanning behavior. Native bridge
patches and recovery semantics still require inspection and tests.

1. Inspect `monero-oxide-ext::Amount` and all callers that cross display, quote,
   serialized amount, wallet and RPC boundaries. Introduce an XMZ-specific amount
   type/conversion boundary rather than silently changing a generic Monero type.
2. Extract the minimal required wallet bridge operations and classify which exist
   in our core versus upstream embedded patches. Choose a native bridge or RPC
   adapter based on those actual requirements, not packaging convenience.
3. Build only the minimum engine/wallet compatibility target in isolated storage.
   First prove amount/address vectors, then fakechain lock scanning and sweep
   recovery using disposable keys. Only then attempt a two-party swap.

The current machine has about 6.3 GiB free on the project volume at inspection.
A full Rust plus bundled C++ dependency rebuild has not been sized; do not launch
that build blindly or delete the user's existing artifacts to make room.

## Primary sources inspected

- [Amount/rate primitives](https://github.com/eigenwallet/core/blob/0e17c7f7cd8f0657af176c8852aa4c9949586051/swap-core/src/monero/primitives.rs)
- [Wallet construction and recovery](https://github.com/eigenwallet/core/blob/0e17c7f7cd8f0657af176c8852aa4c9949586051/monero-wallet/src/wallets.rs)
- [Native wallet build and patches](https://github.com/eigenwallet/core/blob/0e17c7f7cd8f0657af176c8852aa4c9949586051/monero-sys/build.rs)
- [Submodule configuration](https://github.com/eigenwallet/core/blob/0e17c7f7cd8f0657af176c8852aa4c9949586051/.gitmodules)

This is a targeted compatibility assessment, not an independent audit or a claim
that the engine is already adapted. Live trading remains disabled.
