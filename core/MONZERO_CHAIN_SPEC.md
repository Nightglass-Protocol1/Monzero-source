# Monzero (XMZ) chain specification

This document records the initial consensus and network parameters for the
independent Monzero chain. Any change to a consensus value after public launch
requires a coordinated network upgrade.

## Monetary policy

- Name: Monzero
- Ticker: XMZ
- Target primary emission: 100,000,000 XMZ
- Atomic precision: 11 decimal places (1 XMZ = 100,000,000,000 atomic units)
- Emission curve: Monero-style smooth emission, speed factor 20
- Target block time: 120 seconds
- Permanent tail emission: 3.26 XMZ per block
- Hard-coded premine: none
- Pre-release allocation: coins mined normally during the private bootstrap and
  test period; the mined amount and addresses must be disclosed before launch

Eleven decimal places is the greatest precision compatible with a
100,000,000-coin primary emission and the core's unsigned 64-bit amount type.
Twelve decimals would overflow that type.

## Mainnet identity

- Official website: `https://monzero.org` (project metadata; not consensus-critical)
- P2P port: 6174
- RPC port: 6175
- ZMQ RPC port: 6176
- Network UUID: `94834264-d0b2-41dd-b0f2-0ada675c7710`
- Standard/integrated/subaddress prefixes: 86 / 87 / 88
- Genesis nonce: 2271206363
- Genesis block hash: `84f9ebdac8924806f037482ec16fd59b271e954d3e00363dd6c7e4ce9dd659e4`
- Default data directory: `.monzero`
- Executables: `monzerod`, `monzero-wallet-cli`, `monzero-wallet-rpc`

The complete protected mainnet, testnet, and stagenet vectors are available in
the machine-readable `config/monzero-consensus.json` manifest. Compiled unit
tests regenerate each genesis block and fail if monetary policy, address
prefixes, ports, UUIDs, nonces, hashes, transaction version, or hard-fork
schedule drift unexpectedly.

## Bootstrap and launch policy

Monzero does not embed a special premine transaction. The team may privately
mine the initial blocks for network testing. Before public launch, publish:

1. The UTC timestamp and block height at which public launch begins.
2. Every address controlled by the team and its balance at that height.
3. The total privately mined XMZ and its intended use.
4. Stable seed-node hostnames or IP addresses.
5. Reproducible release binaries and the corresponding source commit.

The inherited protocol revisions activate over blocks 1 through 16. Public
launch should occur only after block 16 so all public mining uses v16/RandomX.

## Hard fork 17: LWMA difficulty (v1.0.0)

From block 21,000 on mainnet (stagenet 21,000; private testnet 1,345), the
next block's difficulty is LWMA-1 (zawy12) over the newest 60 solve times:
out-of-order solve times count as 1 second, each solve time is capped at
6 × 120 seconds, the weighted sum is at least 60² × 120 / 20, and the result
is the average difficulty × 0.99 × 120 × 60 × 61 / (2 × weighted sum). From
the same height a block may be timestamped at most 360 seconds after the
validating node's clock (previously 7,200). Blocks before the fork keep the
inherited Monero rule (720-block window, 15-block lag, 60-timestamp cut).
The Monzero Assets research fork is version 18 and is not scheduled.

## Items intentionally pending

- Public launch date and UTC time
- Project domain
- DNS and fixed-IP seed nodes
- Public checkpoints created after the bootstrap period
- Written allocation rationale and vesting/custody commitments
- Independent consensus and cryptography review
