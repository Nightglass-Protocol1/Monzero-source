# Monzero private DEX protocol draft

Status: inactive design document. No public network accepts DEX orders or
settlements, and the GUI must keep live execution disabled.

## Markets and custody

The initial market family is BTC/XMZ, LTC/XMZ, and BCH/XMZ. Orders are intended
to settle through non-custodial, chain-specific atomic swaps. The exchange must
not accept account deposits, hold customer keys, fabricate balances, or treat
reference-market data as an executable quote.

## Fee policy

Every preview must separately disclose:

1. the gross traded amounts;
2. the XMZ protocol fee;
3. the Monzero network fee;
4. the BTC, LTC, or BCH miner fee; and
5. the net amounts each party receives.

The protocol fee is denominated and collected only in XMZ. There is no
external-coin service or treasury fee. External coin is used only for the trade
principal and its native miner fee. A permanent or reusable BTC, LTC, or BCH
fee address is forbidden.

Buying XMZ must not require an existing XMZ balance. The seller locks enough
XMZ to cover the quoted proceeds and Monzero-side fees. On successful
settlement, the buyer receives the quoted XMZ less the disclosed protocol and
network fees.

Order directions always name the external base coin. A BUY BTC/LTC/BCH order
therefore debits the XMZ principal plus XMZ protocol and network fees. A SELL
BTC/LTC/BCH order credits the XMZ proceeds minus those fees. The preview must
say "pay" or "receive" explicitly; a generic "net" value is not sufficient.

Protocol revenue is earned only by a successfully completed swap. Expired,
cancelled, failed, or refunded swaps pay no protocol fee. Each chain may still
consume an unavoidable miner fee for an already-broadcast lock or refund
transaction; the confirmation screen must explain that risk before signing.

## Safety invariants

- Live trading remains disabled unless every chain adapter is explicitly
  compiled, configured, synchronized, and healthy.
- A quote cannot become an order without explicit confirmation of all fees,
  timelocks, minimum confirmations, and refund deadlines.
- Fees cannot make either net settlement output zero, negative, dust, or below
  the configured minimum trade size.
- The protocol fee output must be bound to the same successful settlement as
  the trade and cannot be paid by a refund path.
- Price feeds are display-only. Signed orders and completed swaps determine the
  DEX price.
- No private key, seed, preimage, adaptor secret, or unsigned transaction may
  be written to logs or market-data storage.

## Work still required

Success-only XMZ fee enforcement remains an unresolved cryptographic requirement
even for BTC/XMZ acquisition, not only external/external pairs. Current evidence
checks recognize a fee-paying transaction; they do not prevent the holder of a
recovered full spend key from choosing a fee-free alternate spend. See
[the fee-enforcement analysis](DEX_FEE_ENFORCEMENT.md) before any signing work.
Positive `feeEarned` observations must not be interpreted as protocol-wide fee
enforcement; `feeEnforcementVerified` remains false.

Canonical maker-signed offer and cancellation evidence plus a validating crash-safe
local order book and deterministic crossing evidence exist, but remain
non-executable and are not connected to network transport. Canonical relay
envelopes authenticate their artifact commitment, sender, deployment domain,
lifetime and sequence. A locked, atomic replay store re-verifies each envelope and
accepts only sequence one followed by exact increments per sender; exact retries
are idempotent, while skipped, stale and conflicting sequences fail closed. This
is a local authenticated ingestion boundary. A coordinator independently
re-verifies both the relay and the maker-signed offer or cancellation, requires the
relay artifact and type to match, persists replay state first, and then performs an
idempotent order-book write. An exact retry can therefore recover a stop between
those writes. Accepted relay evidence also has a canonical, length-delimited binary
wire frame capped at 1,024 bytes; decoding rejects truncation, extension, oversized
or inconsistent lengths, mutation, wrong deployment and expired signatures before
returning opaque evidence. This codec is not peer discovery, socket transport,
availability or execution authority. Crossing binds the compatible quantity and
price interval; the execution price still requires a signed quote. Chain execution
adapters, complete atomic-swap cryptography, peer transport and discovery, explicit
replacement policy and complete crash-safe settlement recovery,
reorganisation handling, fee estimation, refund recovery, adversarial tests,
isolated testnets, independent security review, and applicable legal review
are prerequisites for any public activation.

## Activation contract

The client evaluates readiness fail-closed. Missing values, strings such as
`"true"`, and partial evidence all count as false. Live order placement requires
literal positive evidence for the private runtime gate, an isolated non-mainnet
network, a healthy XMZ adapter, a healthy adapter for the selected market,
authenticated order relay, chain-aware fee
estimation, atomic settlement, timelock refund recovery, crash-safe persistence,
adversarial testnet coverage, and independent security review.

Readiness is evaluated per market. A healthy BTC adapter can never activate an
LTC or BCH market, and unrelated adapters are not required to test BTC/XMZ.

Passing this readiness contract is necessary but not sufficient to broadcast:
the confirmation flow must still validate a fresh quote, display both chains'
fees and deadlines, and obtain explicit wallet authorization. Mainnet activation
requires a separate reviewed release decision and is not controlled by runtime
environment variables.
