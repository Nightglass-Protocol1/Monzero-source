# Revised first-release scope — 2026-09-12

User decision: first working BTC/XMZ release may omit mandatory protocol trading
fees. Multi-coin trading remains the product goal, with XMZ among the assets and
XMZ-denominated app utility to be designed separately. This supersedes older
documents that make success-only fee enforcement a prerequisite for every release.
It does not authorize pretending incomplete swaps are executable.

The user's clarified product objective is trading Bitcoin and additional coins,
including XMZ, with XMZ-denominated fees for an explicitly described app service.
XMZ utility remains required for the broader product, rather than being removed
by the zero-app-fee first milestone. The fee-bearing service, recipient and price
must be specified before collecting payments; this clarification does not select
them or authorize collecting a fee for an unimplemented trade.

## Fee boundaries

- First-release app trading fee: zero. Network transaction fees still apply on
  each relevant chain. A zero app fee is not a fee-free transaction.
- Native XMZ transactions use XMZ for their network fee; that is not app revenue
  and does not charge unrelated external/external trades in XMZ.
- A future separately described service could charge XMZ. No service, recipient,
  amount, payment collection or nonrefundable charge is approved by this document.
- Historical V6 signed terms, reservations and recovery evidence retain their
  exact original interpretation. Do not set their fees to zero retrospectively.
  The existing fee-bearing funding planner remains a legacy research API, not the
  first-release execution planner. New signing requires a separate version/domain.

## Implementation direction

Evaluate the existing eigenwallet BTC/Monero swap engine for a BTC/XMZ adaptation
before developing a new consensus mechanism. Evaluation pin:
`0e17c7f7cd8f0657af176c8852aa4c9949586051` from the upstream HEAD lookup.
This is an immutable investigation reference, not a selected production release,
an audit claim or permission to run its defaults against real wallets.

The inspected [environment configuration](https://github.com/eigenwallet/core/blob/0e17c7f7cd8f0657af176c8852aa4c9949586051/swap-env/src/env.rs)
selects Monero network types; even its regtest configuration uses the Monero
mainnet address network. The [dependency manifest](https://github.com/eigenwallet/core/blob/0e17c7f7cd8f0657af176c8852aa4c9949586051/Cargo.toml)
uses Monero address/wallet/RPC crates. Therefore a daemon URL or ticker replacement
alone is not an adaptation.

Local core `58f10f7969b05a12d2e6e733ea5a8883e8b35eee` defines 11 decimal places,
address prefixes 86/87/88, RPC port 6175 and its own network/genesis identity in
`src/cryptonote_config.h`. Port codecs, atomic amounts, genesis checks, address
validation and transaction scanning must be tested against those actual rules.
Inspect upstream amount conversion, wallet backend and pinned dependencies next;
do not assume their internal units or transaction support from the manifest alone.

First acceptance milestone: two disposable participants complete BTC-regtest/XMZ-
fakechain swaps and exercise cancellation/refund and restart paths using the
adapted engine. Then integrate an authenticated local GUI interface, bounded
requests, durable recovery and fresh role-bound quotes. No production keys,
mainnet endpoints or website package replacement during compatibility testing.

The [engine porting assessment](DEX_ENGINE_PORTING_ASSESSMENT.md) records confirmed
amount-scale and native wallet integration obstacles and the next implementation
slice. In particular, raw XMZ atoms must not be rescaled as a blanket workaround.

Other pairs follow individually validated settlement implementations. Existing
LTC/BCH demo tabs do not establish support, and external/external routing through
XMZ must not be advertised as one atomic trade if it consists of sequential swaps.

Live activation remains false until end-to-end settlement/recovery and release
checks pass. Removing the mandatory fee requirement removes one design blocker;
it does not implement signing, prove safety or provide liquidity.

## Transaction fee-estimation boundary

The native wallet's `transfer` RPC can construct a transaction and return its fee
and weight with `do_not_relay=true`. It is not a read-only quote API: full wallets
construct a transaction internally. The flag defaults to false in the native RPC,
so an estimation implementation must explicitly supply true, never rely on omission.
Do not add this operation to the read-only funding observer.

A future estimator needs a caller-owned wallet, explicit account/destination/amount
and priority, serialized wallet access, and a distinct non-executable result.
It must request neither transaction keys, signed blobs nor relay metadata for a
display preview. Bind the result to its construction inputs and freshness; a
separately constructed final transaction can have a different fee and must be
checked and approved separately. The GUI's fixed research estimate is not this API.
